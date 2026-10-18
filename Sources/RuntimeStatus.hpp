// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <sys/types.h>
#include <vector>

namespace horndis {

struct RuntimeStatus {
    std::string state;
    std::string device;
    std::string deviceAlias;
    std::string deviceAddress;
    std::string hostInterface;
    std::string detail;
    uint64_t receivedBytes = 0;
    uint64_t transmittedBytes = 0;
    uint64_t connectedSince = 0;
    bool controlAvailable = false;
};

// Byte counters do not constitute a state transition, so they may be
// coalesced without delaying device, service, or user-requested changes.
bool hasRuntimeStatusTransition(const RuntimeStatus& previous,
                                const RuntimeStatus& current);

class RuntimeStatusPublicationPolicy {
public:
    explicit RuntimeStatusPublicationPolicy(
        std::chrono::steady_clock::duration periodicInterval =
            std::chrono::seconds(5));

    bool shouldPublish(const RuntimeStatus& status,
                       std::chrono::steady_clock::time_point now,
                       bool force = false) const;
    void didPublish(const RuntimeStatus& status,
                    std::chrono::steady_clock::time_point now);

private:
    std::chrono::steady_clock::duration periodicInterval_;
    std::optional<RuntimeStatus> lastPublishedStatus_;
    std::chrono::steady_clock::time_point lastPublishedAt_{};
};

class RuntimeStatusPublisher {
public:
    explicit RuntimeStatusPublisher(
        std::chrono::steady_clock::duration periodicInterval =
            std::chrono::seconds(5),
        std::string directory = "/var/run/horndis");

    bool publish(const RuntimeStatus& status, std::string& error, bool force = false,
                 std::optional<std::pair<uid_t, gid_t>> owner = std::nullopt);

private:
    RuntimeStatusPublicationPolicy policy_;
    std::string directory_;
    std::optional<std::pair<uid_t, gid_t>> lastOwner_;
};

bool publishRuntimeStatus(const RuntimeStatus& status, std::string& error);
bool publishRuntimeStatusInDirectory(const RuntimeStatus& status,
                                    const std::string& directory,
                                    std::string& error,
                                    std::optional<std::pair<uid_t, gid_t>> owner = std::nullopt);
bool prepareUserRuntimeDirectory(const std::string& directory, uid_t user, gid_t group,
                                 std::string& error);
std::string readLatestRuntimeStatus(const std::vector<std::string>& directories =
    {"/var/run/horndis", "/var/run/horndis-supervisor"});
void removeRuntimeStatus();

// Exposed for tests: the exact JSON document published to status.json.
// Device-controlled strings (product name, detail) must arrive escaped.
std::string serializeRuntimeStatus(const RuntimeStatus& status);

} // namespace horndis
