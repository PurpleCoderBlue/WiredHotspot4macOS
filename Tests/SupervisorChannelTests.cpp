// SPDX-License-Identifier: GPL-3.0-or-later
#include "SupervisorChannel.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <atomic>
#include <fcntl.h>
#include <csignal>
#include <sys/wait.h>

namespace {

void testResponse(bool supervisorSuccess, bool suspend = false) {
    int descriptors[2]{-1, -1};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0);

    bool agentResult = false;
    std::string agentError;
    std::thread agent([&] {
        agentResult = suspend ? horndis::requestNetworkSuspend(descriptors[1], agentError)
                              : horndis::requestDHCPRefresh(descriptors[1], agentError);
        close(descriptors[1]);
    });

    horndis::SupervisorRequest request{};
    bool closed = false;
    std::string supervisorError;
    assert(horndis::receiveSupervisorRequest(
        descriptors[0], request, closed, supervisorError));
    assert(!closed);
    assert(request == (suspend ? horndis::SupervisorRequest::suspendNetwork
                               : horndis::SupervisorRequest::refreshDHCP));
    assert(horndis::sendSupervisorResponse(
        descriptors[0], supervisorSuccess, supervisorError));
    close(descriptors[0]);
    agent.join();

    assert(agentResult == supervisorSuccess);
    if (supervisorSuccess) {
        assert(agentError.empty());
    } else {
        assert(agentError.find(suspend ? "could not suspend network" : "could not start DHCP") != std::string::npos);
    }
}

} // namespace

int main() {
    testResponse(true);
    testResponse(false);
    testResponse(true, true);
    testResponse(false, true);

    int descriptors[2]{-1, -1};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0);
    close(descriptors[1]);
    horndis::SupervisorRequest request{};
    bool closed = false;
    std::string error;
    assert(horndis::receiveSupervisorRequest(descriptors[0], request, closed, error));
    assert(closed);
    assert(error.empty());
    close(descriptors[0]);

    // An idle channel must not hide logout/user switching. No system account
    // changes are needed: the OS identity provider is an injected predicate.
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0);
    std::atomic_bool sameSession{true};
    std::thread switched([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        sameSession.store(false);
    });
    auto activity = horndis::waitSupervisorActivity(descriptors[0],
        [&] { return sameSession.load(); }, [] { return true; }, error);
    switched.join();
    assert(activity == horndis::SupervisorActivity::sessionEnded);
    uint8_t operation = static_cast<uint8_t>(horndis::SupervisorRequest::refreshDHCP);
    assert(write(descriptors[1], &operation, 1) == 1);
    activity = horndis::waitSupervisorActivity(descriptors[0], [] { return false; },
        [] { return true; }, error);
    assert(activity == horndis::SupervisorActivity::sessionEnded);
    // A queued command from the old session was rejected, not consumed/run.
    assert(horndis::receiveSupervisorRequest(descriptors[0], request, closed, error));
    assert(request == horndis::SupervisorRequest::refreshDHCP);
    activity = horndis::waitSupervisorActivity(descriptors[0], [] { return true; },
        [] { return false; }, error);
    assert(activity == horndis::SupervisorActivity::stopped);
    close(descriptors[0]); close(descriptors[1]);

    // A peer that consumes a request but never acknowledges it must not block
    // disconnect. Timeout poisons the stream, preventing stale-ACK reuse.
    for (bool suspend : {false, true}) {
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0);
        const auto start = std::chrono::steady_clock::now();
        bool result = suspend
            ? horndis::requestNetworkSuspend(descriptors[1], error, std::chrono::milliseconds(40))
            : horndis::requestDHCPRefresh(descriptors[1], error, std::chrono::milliseconds(40));
        assert(!result && error.find("timed out") != std::string::npos);
        assert(std::chrono::steady_clock::now()-start < std::chrono::seconds(1));
        error.clear();
        assert(horndis::receiveSupervisorRequest(descriptors[0], request, closed, error));
        assert(!closed);
        assert(horndis::receiveSupervisorRequest(descriptors[0], request, closed, error) && closed);
        // macOS may accept a final peer write after shutdown. It must never
        // turn into a successful acknowledgement of a subsequent operation.
        (void)horndis::sendSupervisorResponse(descriptors[0], true, error);
        const auto retryStart = std::chrono::steady_clock::now();
        assert(!horndis::requestNetworkSuspend(descriptors[1], error, std::chrono::milliseconds(40)));
        assert(std::chrono::steady_clock::now()-retryStart < std::chrono::seconds(1));
        close(descriptors[0]); close(descriptors[1]); error.clear();
    }
    // Even a full send buffer obeys the same deadline.
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0);
    assert(fcntl(descriptors[1], F_SETFL, O_NONBLOCK) == 0);
    char fill[4096]{};
    while (send(descriptors[1], fill, sizeof(fill), MSG_DONTWAIT) > 0) {}
    assert(!horndis::requestNetworkSuspend(descriptors[1], error, std::chrono::milliseconds(40)));
    assert(error.find("timed out") != std::string::npos);
    close(descriptors[0]); close(descriptors[1]); error.clear();

    int ready[2]; assert(pipe(ready) == 0);
    const pid_t stubborn = fork(); assert(stubborn >= 0);
    if (stubborn == 0) {
        close(ready[0]); signal(SIGTERM, SIG_IGN);
        char byte = 1; (void)write(ready[1], &byte, 1);
        for (;;) pause();
    }
    close(ready[1]); char byte = 0; assert(read(ready[0], &byte, 1) == 1); close(ready[0]);
    int status = 0;
    assert(horndis::stopDataAgent(stubborn, status, error, std::chrono::milliseconds(50)));
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    std::cout << "Supervisor channel tests passed\n";
    return 0;
}
