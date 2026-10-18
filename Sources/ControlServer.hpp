// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <sys/types.h>

namespace horndis {

enum class ControlCommand {
    connect,
    disconnect,
    observe,
};

class ControlServer {
public:
    explicit ControlServer(std::string directory = "/var/run/horndis",
                           std::function<uid_t()> consoleUser = {});
    ~ControlServer();

    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    bool start(std::string& error);
    std::vector<ControlCommand> pollCommands();
    void close();

private:
    struct Client {
        int descriptor;
        std::string input;
        std::chrono::steady_clock::time_point deadline;
    };
    int socket_ = -1;
    bool ownsPath_ = false;
    std::string directory_;
    std::string path_;
    std::function<uid_t()> consoleUser_;
    std::vector<Client> clients_;
};

} // namespace horndis
