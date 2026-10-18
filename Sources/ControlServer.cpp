// SPDX-License-Identifier: GPL-3.0-or-later
#include "ControlServer.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <SystemConfiguration/SystemConfiguration.h>

namespace horndis {
namespace {

uid_t consoleUserID() {
    uid_t user = static_cast<uid_t>(-1); gid_t group = 0;
    auto name = SCDynamicStoreCopyConsoleUser(nullptr, &user, &group);
    if (name) CFRelease(name);
    return user;
}

bool isConsoleUser(int descriptor, uid_t consoleUser) {
    uid_t peerUser = 0;
    gid_t peerGroup = 0;
    if (getpeereid(descriptor, &peerUser, &peerGroup) != 0) {
        return false;
    }
    if (peerUser == 0) {
        return true;
    }
    return peerUser == consoleUser;
}

} // namespace

ControlServer::ControlServer(std::string directory, std::function<uid_t()> consoleUser)
    : directory_(std::move(directory)), path_(directory_ + "/control.sock"),
      consoleUser_(consoleUser ? std::move(consoleUser) : consoleUserID) {}

ControlServer::~ControlServer() {
    close();
}

bool ControlServer::start(std::string& error) {
    close();
    if (mkdir(directory_.c_str(), 0700) != 0 && errno != EEXIST) {
        error = "cannot create control socket directory: " + std::string(std::strerror(errno));
        return false;
    }
    const int directoryFD = open(directory_.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat metadata{};
    const bool secure = directoryFD >= 0 && fstat(directoryFD, &metadata) == 0 &&
        metadata.st_uid == geteuid() && (metadata.st_mode & (S_IWGRP | S_IWOTH)) == 0;
    if (directoryFD >= 0) ::close(directoryFD);
    if (!secure) { error = "control socket directory is unsafe"; return false; }
    socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_ < 0) {
        error = "cannot create control socket: " + std::string(std::strerror(errno));
        return false;
    }
    if (fcntl(socket_, F_SETFL, O_NONBLOCK) != 0 || fcntl(socket_, F_SETFD, FD_CLOEXEC) != 0) {
        error = "cannot configure control socket: " + std::string(std::strerror(errno));
        close();
        return false;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    address.sun_len = sizeof(address);
    if (path_.size() >= sizeof(address.sun_path)) {
        error = "control socket path is too long"; close(); return false;
    }
    std::strncpy(address.sun_path, path_.c_str(), sizeof(address.sun_path) - 1);
    (void)unlink(path_.c_str());
    if (bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        error = "cannot bind control socket: " + std::string(std::strerror(errno));
        close(); return false;
    }
    ownsPath_ = true;
    if (chmod(path_.c_str(), 0600) != 0 || listen(socket_, 16) != 0) {
        error = "cannot publish control socket: " + std::string(std::strerror(errno));
        close();
        return false;
    }
    return true;
}

std::vector<ControlCommand> ControlServer::pollCommands() {
    std::vector<ControlCommand> commands;
    if (socket_ < 0) {
        return commands;
    }

    constexpr size_t kMaximumClients = 16;
    const auto now = std::chrono::steady_clock::now();
    const uid_t consoleUser = consoleUser_();
    for (size_t accepted = 0; accepted < kMaximumClients && clients_.size() < kMaximumClients; ++accepted) {
        const int client = accept(socket_, nullptr, nullptr);
        if (client < 0) break;
        const int flags = fcntl(client, F_GETFL);
        if (!isConsoleUser(client, consoleUser) || flags < 0 ||
            fcntl(client, F_SETFL, flags | O_NONBLOCK) != 0 ||
            fcntl(client, F_SETFD, FD_CLOEXEC) != 0) {
            ::close(client); continue;
        }
        clients_.push_back({client, {}, now + std::chrono::seconds(2)});
    }
    for (auto iterator = clients_.begin(); iterator != clients_.end();) {
        bool finished = now >= iterator->deadline || !isConsoleUser(iterator->descriptor, consoleUser);
        if (!finished) {
            char buffer[32];
            const auto count = read(iterator->descriptor, buffer, sizeof(buffer));
            if (count > 0) {
                iterator->input.append(buffer, static_cast<size_t>(count));
                const auto newline = iterator->input.find('\n');
                if (newline != std::string::npos) {
                    const auto command = iterator->input.substr(0, newline);
                    if (command == "connect") commands.push_back(ControlCommand::connect);
                    else if (command == "disconnect") commands.push_back(ControlCommand::disconnect);
                    else if (command == "observe") commands.push_back(ControlCommand::observe);
                    finished = true;
                } else if (iterator->input.size() >= sizeof(buffer)) {
                    finished = true;
                }
            } else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                finished = true;
            }
        }
        if (finished) {
            ::close(iterator->descriptor);
            iterator = clients_.erase(iterator);
        } else ++iterator;
    }
    return commands;
}

void ControlServer::close() {
    if (socket_ >= 0) {
        (void)::close(socket_);
        socket_ = -1;
    }
    for (const auto& client : clients_) (void)::close(client.descriptor);
    clients_.clear();
    if (ownsPath_) (void)unlink(path_.c_str());
    ownsPath_ = false;
}

} // namespace horndis
