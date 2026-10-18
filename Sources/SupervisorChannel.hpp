// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <chrono>
#include <sys/types.h>

namespace horndis {

enum class SupervisorRequest : uint8_t {
    refreshDHCP = 1,
    suspendNetwork = 2,
};

bool requestDHCPRefresh(int descriptor, std::string& error,
                        std::chrono::milliseconds timeout = std::chrono::seconds(30));
bool requestNetworkSuspend(int descriptor, std::string& error,
                        std::chrono::milliseconds timeout = std::chrono::seconds(30));
bool receiveSupervisorRequest(int descriptor,
                              SupervisorRequest& request,
                              bool& closed,
                              std::string& error);
bool sendSupervisorResponse(int descriptor, bool success, std::string& error);

enum class SupervisorActivity { requestReady, sessionEnded, stopped, failed };
SupervisorActivity waitSupervisorActivity(int descriptor,
                                         const std::function<bool()>& sessionCurrent,
                                         const std::function<bool()>& running,
                                         std::string& error);
bool stopDataAgent(pid_t agent, int& status, std::string& error,
                   std::chrono::milliseconds grace = std::chrono::seconds(2));

} // namespace horndis
