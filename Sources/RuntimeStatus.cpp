// SPDX-License-Identifier: GPL-3.0-or-later
#include "RuntimeStatus.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace horndis {
namespace {

constexpr const char* kStatusDirectory = "/var/run/horndis";

std::string escapeJSON(const std::string& value) {
    std::ostringstream stream;
    stream << std::hex;
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            stream << "\\\"";
            break;
        case '\\':
            stream << "\\\\";
            break;
        case '\b':
            stream << "\\b";
            break;
        case '\f':
            stream << "\\f";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (character < 0x20) {
                constexpr char digits[] = "0123456789abcdef";
                stream << "\\u00" << digits[(character >> 4) & 0x0f]
                       << digits[character & 0x0f];
            } else {
                stream << character;
            }
            break;
        }
    }
    return stream.str();
}

uint64_t currentTimestamp() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

bool writeAll(int descriptor, const std::string& contents, std::string& error) {
    size_t offset = 0;
    while (offset < contents.size()) {
        const ssize_t count = write(descriptor, contents.data() + offset, contents.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = "cannot write runtime status: " + std::string(std::strerror(errno));
            return false;
        }
        offset += static_cast<size_t>(count);
    }
    return true;
}

std::string serialize(const RuntimeStatus& status) {
    std::ostringstream stream;
    stream << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"state\": \"" << escapeJSON(status.state) << "\",\n"
           << "  \"device\": \"" << escapeJSON(status.device) << "\",\n"
           << "  \"device_alias\": \"" << escapeJSON(status.deviceAlias) << "\",\n"
           << "  \"device_address\": \"" << escapeJSON(status.deviceAddress) << "\",\n"
           << "  \"host_interface\": \"" << escapeJSON(status.hostInterface) << "\",\n"
           << "  \"detail\": \"" << escapeJSON(status.detail) << "\",\n"
           << "  \"received_bytes\": " << status.receivedBytes << ",\n"
           << "  \"transmitted_bytes\": " << status.transmittedBytes << ",\n"
           << "  \"connected_since\": " << status.connectedSince << ",\n"
           << "  \"control_available\": "
           << (status.controlAvailable ? "true" : "false") << ",\n"
           << "  \"updated_at\": " << currentTimestamp() << ",\n"
           << "  \"process_id\": " << getpid() << "\n"
           << "}\n";
    return stream.str();
}

} // namespace

std::string serializeRuntimeStatus(const RuntimeStatus& status) {
    return serialize(status);
}

bool hasRuntimeStatusTransition(const RuntimeStatus& previous,
                                const RuntimeStatus& current) {
    return previous.state != current.state ||
           previous.device != current.device ||
           previous.deviceAlias != current.deviceAlias ||
           previous.deviceAddress != current.deviceAddress ||
           previous.hostInterface != current.hostInterface ||
           previous.detail != current.detail ||
           previous.connectedSince != current.connectedSince ||
           previous.controlAvailable != current.controlAvailable;
}

RuntimeStatusPublicationPolicy::RuntimeStatusPublicationPolicy(
    std::chrono::steady_clock::duration periodicInterval)
    : periodicInterval_(periodicInterval) {}

bool RuntimeStatusPublicationPolicy::shouldPublish(
    const RuntimeStatus& status,
    std::chrono::steady_clock::time_point now,
    bool force) const {
    if (force || !lastPublishedStatus_.has_value() ||
        hasRuntimeStatusTransition(lastPublishedStatus_.value(), status)) {
        return true;
    }
    return now - lastPublishedAt_ >= periodicInterval_;
}

void RuntimeStatusPublicationPolicy::didPublish(
    const RuntimeStatus& status,
    std::chrono::steady_clock::time_point now) {
    lastPublishedStatus_ = status;
    lastPublishedAt_ = now;
}

RuntimeStatusPublisher::RuntimeStatusPublisher(
    std::chrono::steady_clock::duration periodicInterval, std::string directory)
    : policy_(periodicInterval), directory_(std::move(directory)) {}

bool RuntimeStatusPublisher::publish(const RuntimeStatus& status,
                                     std::string& error,
                                     bool force,
                                     std::optional<std::pair<uid_t, gid_t>> owner) {
    const auto now = std::chrono::steady_clock::now();
    if (!policy_.shouldPublish(status, now, force || owner != lastOwner_)) {
        return true;
    }
    if (!publishRuntimeStatusInDirectory(status, directory_, error, owner)) {
        return false;
    }
    policy_.didPublish(status, now);
    lastOwner_ = owner;
    return true;
}

bool publishRuntimeStatus(const RuntimeStatus& status, std::string& error) {
    return publishRuntimeStatusInDirectory(status, kStatusDirectory, error);
}

bool publishRuntimeStatusInDirectory(const RuntimeStatus& status,
                                    const std::string& directory,
                                    std::string& error,
                                    std::optional<std::pair<uid_t, gid_t>> owner) {
    error.clear();
    const bool created = mkdir(directory.c_str(), geteuid() == 0 ? 0755 : 0700) == 0;
    if (!created && errno != EEXIST) {
        error = "cannot create runtime status directory: " + std::string(std::strerror(errno));
        return false;
    }
    const int directoryFD = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directoryFD < 0) {
        error = "cannot securely open runtime status directory: " + std::string(std::strerror(errno));
        return false;
    }
    struct stat metadata{};
    if (fstat(directoryFD, &metadata) != 0 || metadata.st_uid != geteuid() ||
        (metadata.st_mode & (S_IWGRP | S_IWOTH)) != 0 ||
        (created && fchmod(directoryFD, geteuid() == 0 ? 0755 : 0700) != 0)) {
        error = "runtime status directory is not owned exclusively by its publisher";
        close(directoryFD);
        return false;
    }
    // Random exclusive names prevent a user-controlled stale temp entry from
    // redirecting writes. Every metadata operation uses the opened inode.
    const std::string temporary = "status.json.tmp." + std::to_string(getpid()) + "." +
        std::to_string(arc4random()) + "." + std::to_string(arc4random());
    const int descriptor = openat(directoryFD, temporary.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0) {
        error = "cannot create runtime status: " + std::string(std::strerror(errno));
        close(directoryFD);
        return false;
    }
    bool ok = writeAll(descriptor, serialize(status), error);
    if (ok && owner) ok = fchown(descriptor, owner->first, owner->second) == 0;
    ok = ok && fchmod(descriptor, 0600) == 0 && fsync(descriptor) == 0;
    const int closeResult = close(descriptor);
    ok = ok && closeResult == 0;
    if (ok) ok = renameat(directoryFD, temporary.c_str(), directoryFD, "status.json") == 0;
    if (!ok) {
        if (error.empty()) error = "cannot publish runtime status: " + std::string(std::strerror(errno));
        (void)unlinkat(directoryFD, temporary.c_str(), 0);
    }
    close(directoryFD);
    return ok;
}

bool prepareUserRuntimeDirectory(const std::string& directory, uid_t user, gid_t group,
                                 std::string& error) {
    if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
        error = "cannot create user runtime directory: " + std::string(std::strerror(errno));
        return false;
    }
    const int descriptor = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (descriptor < 0) {
        error = "cannot securely open user runtime directory: " + std::string(std::strerror(errno));
        return false;
    }
    // Revoke the previous user's write access before touching directory entries.
    bool ok = fchown(descriptor, geteuid(), getegid()) == 0 && fchmod(descriptor, 0700) == 0;
    for (const char* entry : {"control.sock", "status.json"}) {
        if (ok && unlinkat(descriptor, entry, 0) != 0 && errno != ENOENT) ok = false;
    }
    ok = ok && fchown(descriptor, user, group) == 0 && fchmod(descriptor, 0700) == 0;
    if (!ok) error = "cannot prepare user runtime directory: " + std::string(std::strerror(errno));
    close(descriptor);
    return ok;
}

std::string readLatestRuntimeStatus(const std::vector<std::string>& directories) {
    std::string latest;
    timespec latestTime{};
    for (const auto& directory : directories) {
        const int parent = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (parent < 0) continue;
        const int descriptor = openat(parent, "status.json", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        close(parent);
        if (descriptor < 0) continue;
        struct stat metadata{};
        if (fstat(descriptor, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
            metadata.st_size <= 0 || metadata.st_size > 64 * 1024) {
            close(descriptor); continue;
        }
        std::string contents;
        char buffer[4096];
        while (contents.size() < 64 * 1024) {
            const auto count = read(descriptor, buffer, std::min(sizeof(buffer), 64 * 1024 - contents.size()));
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            contents.append(buffer, static_cast<size_t>(count));
        }
        close(descriptor);
        const auto modified = metadata.st_mtimespec;
        if (!contents.empty() && (latest.empty() || modified.tv_sec > latestTime.tv_sec ||
            (modified.tv_sec == latestTime.tv_sec && modified.tv_nsec > latestTime.tv_nsec))) {
            latest = std::move(contents); latestTime = modified;
        }
    }
    return latest;
}

void removeRuntimeStatus() {
    for (const char* directory : {kStatusDirectory, "/var/run/horndis-supervisor"}) {
        const int parent = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (parent < 0) continue;
        (void)unlinkat(parent, "status.json", 0);
        close(parent);
    }
}

} // namespace horndis
