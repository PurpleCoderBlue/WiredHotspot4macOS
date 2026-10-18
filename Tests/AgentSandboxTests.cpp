// SPDX-License-Identifier: GPL-3.0-or-later
#include "AgentSandbox.hpp"
#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <net/if.h>
#include <net/bpf.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sandbox.h>

static int rebind(int fd) {
    ifreq request{};
    errno = 0;
    const int result = ioctl(fd, BIOCSETIF, &request);
    return result < 0 ? errno : 0;
}
int main(int argc, char* argv[]) {
    if (argc == 3) return rebind(std::stoi(argv[2])) == EPERM ? 0 : 1;
    // /dev/null makes all binding requests harmless. Check the real kernel
    // policy rather than a source-string assertion or a fake ioctl function.
    const int fd = open("/dev/null", O_RDWR);
    assert(fd >= 0);
    assert(rebind(fd) != EPERM);
    std::string error;
    assert(horndis::restrictDescriptorIOCTL(fd, error));
    assert(rebind(fd) == EPERM);
    std::cout << "Expected denial: a permissive sandbox reinitialization must not remove the BPF restriction\n" << std::flush;
    char* sandboxError = nullptr;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    (void)sandbox_init("(version 1)(allow default)", 0, &sandboxError);
    sandbox_free_error(sandboxError);
#pragma clang diagnostic pop
    assert(rebind(fd) == EPERM); // a later permissive profile cannot undo it
    const int copy = dup(fd);
    assert(copy >= 0 && rebind(copy) == EPERM);
    const int alias = open(("/dev/fd/" + std::to_string(fd)).c_str(), O_RDWR);
    assert(alias >= 0 && rebind(alias) == EPERM);
    assert(ioctl(fd, BIOCFLUSH) < 0 && errno != EPERM);
    unsigned length = 0;
    assert(ioctl(fd, BIOCGBLEN, &length) < 0 && errno != EPERM);
    char byte = 0;
    assert(read(fd, &byte, 1) == 0);
    assert(write(fd, &byte, 1) == 1);
    const auto descriptor = std::to_string(fd);
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        execl(argv[0], argv[0], "--inherited", descriptor.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(alias); close(copy); close(fd);
    std::cout << "Agent sandbox blocks rebind, descriptor duplication and exec escape; read/write/flush remain allowed\n";
}
