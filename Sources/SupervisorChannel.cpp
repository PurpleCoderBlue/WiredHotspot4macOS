// SPDX-License-Identifier: GPL-3.0-or-later
#include "SupervisorChannel.hpp"

#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <csignal>
#include <sys/wait.h>
#include <thread>
#include <sys/socket.h>
#include <algorithm>
#include <climits>
#include <fcntl.h>

namespace horndis {
namespace {

constexpr uint8_t kSupervisorSuccess = 1;
constexpr uint8_t kSupervisorFailure = 2;

bool writeByte(int descriptor, uint8_t value, std::string& error) {
    const int flags = fcntl(descriptor, F_GETFL);
    if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0) {
        error = "cannot make the supervisor channel nonblocking";
        return false;
    }
    int noSignal = 1;
    (void)setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal));
    ssize_t count = -1;
    do {
        count = ::send(descriptor, &value, sizeof(value), MSG_DONTWAIT);
    } while (count < 0 && errno == EINTR);
    if (count != static_cast<ssize_t>(sizeof(value))) {
        error = "cannot write to the root supervisor channel: " +
                std::string(count < 0 ? std::strerror(errno) : "short write");
        return false;
    }
    return true;
}

bool readByte(int descriptor, uint8_t& value, bool& closed, std::string& error) {
    closed = false;
    ssize_t count = -1;
    do {
        count = ::read(descriptor, &value, sizeof(value));
    } while (count < 0 && errno == EINTR);
    if (count == 0) {
        closed = true;
        return true;
    }
    if (count != static_cast<ssize_t>(sizeof(value))) {
        error = "cannot read from the root supervisor channel: " +
                std::string(count < 0 ? std::strerror(errno) : "short read");
        return false;
    }
    return true;
}

} // namespace

static bool requestOperation(int descriptor, SupervisorRequest operation, std::string& error,
                             std::chrono::milliseconds timeout) {
    error.clear();
    if (descriptor < 0) {
        error = "the root supervisor network channel is unavailable";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    auto ready = [&](short events) {
        for (;;) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) { error = "root supervisor network request timed out"; return false; }
            pollfd channel{descriptor, events, 0};
            int result = poll(&channel, 1, static_cast<int>(std::min<int64_t>(remaining, INT_MAX)));
            if (result < 0 && errno == EINTR) continue;
            if (result > 0 && (channel.revents & events)) return true;
            error = result == 0 ? "root supervisor network request timed out"
                                : "root supervisor network channel closed or failed";
            return false;
        }
    };
    // A late one-byte acknowledgement cannot be associated with a new request.
    // Poison a failed/timed-out connection so the root monitor performs cleanup
    // and the agent cannot accidentally consume that acknowledgement later.
    auto fail = [&] { (void)shutdown(descriptor, SHUT_RDWR); return false; };
    const int flags = fcntl(descriptor, F_GETFL);
    if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0) {
        error = "cannot make the supervisor channel nonblocking";
        return fail();
    }
    int noSignal = 1;
    (void)setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal));
    uint8_t request = static_cast<uint8_t>(operation), response = 0;
    for (;;) {
        if (!ready(POLLOUT)) return fail();
        const auto written = send(descriptor, &request, 1, MSG_DONTWAIT);
        if (written == 1) break;
        if (written < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        error = "cannot send root supervisor network request";
        return fail();
    }
    for (;;) {
        if (!ready(POLLIN)) return fail();
        const auto received = recv(descriptor, &response, 1, MSG_DONTWAIT);
        if (received == 1) break;
        if (received < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        error = "root supervisor network channel closed or failed";
        return fail();
    }
    if (response == kSupervisorSuccess) return true;
    if (response == kSupervisorFailure) {
        error = operation == SupervisorRequest::suspendNetwork
            ? "the root supervisor could not suspend network; see /var/log/horndis.log"
            : "the root supervisor could not start DHCP; see /var/log/horndis.log";
    } else error = "the root supervisor returned an invalid network response";
    return fail();
}

bool requestDHCPRefresh(int descriptor, std::string& error, std::chrono::milliseconds timeout) {
    return requestOperation(descriptor, SupervisorRequest::refreshDHCP, error, timeout);
}
bool requestNetworkSuspend(int descriptor, std::string& error, std::chrono::milliseconds timeout) {
    return requestOperation(descriptor, SupervisorRequest::suspendNetwork, error, timeout);
}

bool receiveSupervisorRequest(int descriptor,
                              SupervisorRequest& request,
                              bool& closed,
                              std::string& error) {
    uint8_t value = 0;
    if (!readByte(descriptor, value, closed, error) || closed) {
        return error.empty();
    }
    if (value != static_cast<uint8_t>(SupervisorRequest::refreshDHCP) &&
        value != static_cast<uint8_t>(SupervisorRequest::suspendNetwork)) {
        error = "the data agent sent an invalid supervisor request";
        return false;
    }
    request = static_cast<SupervisorRequest>(value);
    return true;
}

bool sendSupervisorResponse(int descriptor, bool success, std::string& error) {
    return writeByte(descriptor, success ? kSupervisorSuccess : kSupervisorFailure, error);
}

SupervisorActivity waitSupervisorActivity(int descriptor,
                                         const std::function<bool()>& sessionCurrent,
                                         const std::function<bool()>& running,
                                         std::string& error) {
    while (running()) {
        if (!sessionCurrent()) return SupervisorActivity::sessionEnded;
        pollfd channel{descriptor, POLLIN, 0};
        const int result = poll(&channel, 1, 250);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0 || (channel.revents & (POLLNVAL | POLLERR))) {
            error = "cannot monitor the supervisor channel";
            return SupervisorActivity::failed;
        }
        if (result > 0) {
            if (!running()) return SupervisorActivity::stopped;
            if (!sessionCurrent()) return SupervisorActivity::sessionEnded;
            return SupervisorActivity::requestReady;
        }
    }
    return SupervisorActivity::stopped;
}

bool stopDataAgent(pid_t agent, int& status, std::string& error,
                   std::chrono::milliseconds grace) {
    if (agent <= 1) { error = "invalid data agent PID"; return false; }
    (void)kill(agent, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + grace;
    for (;;) {
        const auto result = waitpid(agent, &status, WNOHANG);
        if (result == agent) return true;
        if (result < 0 && errno != EINTR) {
            error = "cannot reap data agent: " + std::string(std::strerror(errno));
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    (void)kill(agent, SIGKILL);
    pid_t result;
    do { result = waitpid(agent, &status, 0); } while (result < 0 && errno == EINTR);
    if (result != agent) {
        error = "cannot reap terminated data agent: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

} // namespace horndis
