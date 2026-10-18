// SPDX-License-Identifier: GPL-3.0-or-later
#include "AgentSandbox.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <net/bpf.h>
#include <sandbox.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace horndis {
bool restrictDescriptorIOCTL(int descriptor, std::string& error) {
    char path[PATH_MAX]{};
    struct stat metadata{};
    if (fstat(descriptor, &metadata) != 0 || !S_ISCHR(metadata.st_mode) ||
        fcntl(descriptor, F_GETPATH, path) != 0) {
        error = "cannot identify the inherited BPF device for sandboxing";
        return false;
    }
    // The production descriptor comes from /dev/bpf<number>. Using the
    // kernel-resolved vnode path also covers dup() and /dev/fd aliases.
    const std::string devicePath(path);
    if (!devicePath.starts_with("/dev/") ||
        devicePath.find_first_not_of("/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
            std::string::npos) {
        error = "the inherited device has an unsafe sandbox path";
        return false;
    }
    const std::string profile =
        "(version 1)(allow default)"
        "(deny file-ioctl (require-all (literal \"" + devicePath +
        "\") (require-not (require-any (ioctl-command " + std::to_string(BIOCFLUSH) +
        ") (ioctl-command " + std::to_string(BIOCGBLEN) + ")))))";
    char* failure = nullptr;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const int result = sandbox_init(profile.c_str(), 0, &failure);
#pragma clang diagnostic pop
    if (result != 0) {
        error = "cannot restrict BPF reconfiguration: " +
            std::string(failure != nullptr ? failure : std::strerror(errno));
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        sandbox_free_error(failure);
#pragma clang diagnostic pop
        return false;
    }
    return true;
}
}
