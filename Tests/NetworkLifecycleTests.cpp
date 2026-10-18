// Execute production lifecycle code with process/network operations intercepted.
#include "../Sources/VirtualEthernet.hpp"
#include "../Sources/NetworkService.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <net/bpf.h>
#include <net/if.h>
#include <poll.h>
#include <spawn.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <set>
#include <sstream>
#include <chrono>
#include <thread>

static std::vector<std::string> commands;
static bool storedMember = false, failDetach = false, failRefresh = false, failAttach = false;
static unsigned detachCalls = 0, detachFailures = 0;
static size_t failCommand = 0;
static bool stallChild = false, killedChild = false;
static uid_t auditUID() { return 0; }
static int auditSpawn(pid_t* pid, const char* executable,
                      const posix_spawn_file_actions_t*, const posix_spawnattr_t*,
                      char* const argv[], char* const[]) {
    std::string command = executable;
    for (size_t i = 1; argv[i]; ++i) command += " " + std::string(argv[i]);
    commands.push_back(command);
    *pid = 12345; // synthetic; never passed to real waitpid or kill
    return 0;
}
static pid_t auditWait(pid_t pid, int* status, int) { *status = failCommand == commands.size() ? (1 << 8) : 0; return stallChild && !killedChild ? 0 : pid; }
static int auditKill(pid_t pid, int signal) { if (pid != 12345 || signal != SIGKILL) std::abort(); killedChild = true; return 0; }

#define geteuid auditUID
#define posix_spawn auditSpawn
#define waitpid auditWait
#define kill auditKill
#include "../Sources/VirtualEthernet.cpp"
#undef geteuid
#undef posix_spawn
#undef waitpid
#undef kill

namespace horndis {
bool isBridgeName(const std::string& s) { return s.starts_with("bridge"); }
bool ensureNetworkService(std::string&, std::string&) { return false; }
bool setNetworkServiceMember(const std::string&, const std::string&, bool attached,
                             std::string& error) {
    if (!attached) ++detachCalls;
    if (!attached && (failDetach || detachCalls <= detachFailures)) { error = "injected preferences apply failure"; return false; }
    storedMember = attached;
    if (attached && failAttach) { error = "injected commit success / apply failure"; return false; }
    return true;
}
bool refreshNetworkService(const std::string&, std::string& error) {
    if (failRefresh) { error = "injected DHCP refresh failure"; return false; }
    return true;
}
}

static void setup(horndis::VirtualEthernet& ethernet, bool attached) {
    ethernet.hostInterface_ = "bridge1";
    ethernet.transportInterface_ = "feth98";
    ethernet.memberInterface_ = "feth99";
    ethernet.memberAttached_ = attached;
    ethernet.ownsInterfaces_ = false; // destructor cannot destroy anything
    storedMember = attached;
    commands.clear();
}

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char* name) {
        std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
        failures += !condition;
    };
    // Each command can fail after a different amount of activation work.
    for (size_t command = 0; command <= 4; ++command) {
        horndis::VirtualEthernet ethernet; setup(ethernet, false);
        failCommand = command; failRefresh = command == 0;
        detachCalls = detachFailures = 0;
        std::string error;
        const bool result = ethernet.refreshDHCP(error);
        check(!result && !storedMember && !ethernet.memberAttached_,
              "failed activation rolls back configured and runtime membership");
        check(!error.empty(), "activation failure keeps its cause");
        for (const auto& cmd : commands) {
            check(cmd.find("bridge1") != std::string::npos ||
                  cmd.find("feth98") != std::string::npos ||
                  cmd.find("feth99") != std::string::npos,
                  "commands address only this bridge and feth pair");
        }
        failCommand = 0; failRefresh = false;
    }
    {
        horndis::VirtualEthernet ethernet; setup(ethernet, false);
        failAttach = true; std::string error;
        check(!ethernet.refreshDHCP(error) && !storedMember && !ethernet.memberAttached_,
              "membership commit/apply failure is rolled back");
        failAttach = false;
    }
    {
        horndis::VirtualEthernet ethernet; setup(ethernet, true);
        failCommand = 1; std::string error;
        check(ethernet.suspendNetwork(error) && !storedMember && !ethernet.memberAttached_,
              "transient runtime detach failure is retried");
        failCommand = 0;
    }
    {
        horndis::VirtualEthernet ethernet; setup(ethernet, true);
        detachCalls = 0; detachFailures = 2;
        std::string error;
        check(ethernet.suspendNetwork(error) && detachCalls == 3 && !storedMember &&
              !ethernet.memberAttached_, "suspension recovers transient preferences failure");
    }
    {
        horndis::VirtualEthernet ethernet; setup(ethernet, true);
        detachCalls = 0; detachFailures = 0; failDetach = true;
        std::string error;
        check(!ethernet.suspendNetwork(error) && detachCalls == 3 && !error.empty(),
              "persistent cleanup failure is bounded and reported");
        check(std::find(commands.begin(), commands.end(), "/sbin/ifconfig bridge1 down") != commands.end(),
              "failed cleanup still lowers the owned host bridge");
        failDetach = false;
        check(ethernet.suspendNetwork(error) && !storedMember,
              "subsequent cleanup finishes pending preferences removal");
    }
    {
        horndis::VirtualEthernet ethernet; setup(ethernet, false);
        detachCalls = detachFailures = 0;
        std::string error;
        check(ethernet.refreshDHCP(error) && storedMember && ethernet.memberAttached_,
              "successful activation remains functional");
        check(ethernet.suspendNetwork(error) && !storedMember && !ethernet.memberAttached_,
              "successful activation can be suspended");
    }
    {
        std::string error; stallChild = true;
        const auto start = std::chrono::steady_clock::now();
        check(!horndis::runCommand("/sbin/ifconfig", {"bridge1", "down"}, false, error,
                                  std::chrono::milliseconds(20)) && killedChild &&
              error.find("timed out") != std::string::npos &&
              std::chrono::steady_clock::now()-start < std::chrono::seconds(1),
              "a stalled owned-interface command is terminated and reported");
        stallChild = false;
    }
    std::cout << "Network lifecycle failures=" << failures
              << "; all commands and preferences are intercepted\n";
    return failures ? 1 : 0;
}
