// SPDX-License-Identifier: GPL-3.0-or-later
#include "RuntimeStatus.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>

int main() {
    char pattern[] = "/private/tmp/horndis-runtime-tests.XXXXXX";
    const char* path = mkdtemp(pattern); assert(path);
    const std::string root(path), directory=root+"/runtime", victim=root+"/victim";
    std::ofstream(victim) << "do not modify";
    assert(chmod(victim.c_str(), 0644) == 0);
    assert(mkdir(directory.c_str(),0700)==0);
    assert(symlink(victim.c_str(),(directory+"/status.json").c_str())==0);
    auto trap=directory+"/status.json.tmp."+std::to_string(getpid());
    assert(mkdir(trap.c_str(),0700)==0);
    horndis::RuntimeStatus status; status.state="waiting";
    std::string error;
    assert(horndis::publishRuntimeStatusInDirectory(status,directory,error,{{getuid(),getgid()}}));
    struct stat information{};
    assert(lstat((directory+"/status.json").c_str(),&information)==0 && S_ISREG(information.st_mode));
    assert((information.st_mode&0777)==0600);
    assert(stat(victim.c_str(),&information)==0 && (information.st_mode&0777)==0644);
    std::string text; std::ifstream(victim)>>text; assert(text=="do");
    const std::string alias=root+"/alias";
    assert(symlink(directory.c_str(),alias.c_str())==0);
    assert(!horndis::publishRuntimeStatusInDirectory(status,alias,error));
    assert(!horndis::prepareUserRuntimeDirectory(alias,getuid(),getgid(),error));
    assert(chmod(directory.c_str(),0777)==0);
    assert(!horndis::publishRuntimeStatusInDirectory(status,directory,error));
    assert(chmod(directory.c_str(),0700)==0);
    unlink((directory+"/status.json").c_str());
    assert(symlink(victim.c_str(),(directory+"/status.json").c_str())==0);
    assert(horndis::prepareUserRuntimeDirectory(directory,getuid(),getgid(),error));
    assert(lstat((directory+"/status.json").c_str(),&information)!=0);
    assert(stat(directory.c_str(),&information)==0 && (information.st_mode&0777)==0700);
    assert(stat(victim.c_str(),&information)==0 && (information.st_mode&0777)==0644);
    const std::string supervisor=root+"/supervisor";
    horndis::RuntimeStatus failed; failed.state="error";
    assert(horndis::publishRuntimeStatusInDirectory(failed,supervisor,error));
    auto latest=horndis::readLatestRuntimeStatus({directory,supervisor});
    assert(latest.find("\"state\": \"error\"")!=std::string::npos);
    assert(horndis::publishRuntimeStatusInDirectory(status,directory,error));
    latest=horndis::readLatestRuntimeStatus({directory,supervisor});
    assert(latest.find("\"state\": \"waiting\"")!=std::string::npos);
    std::filesystem::remove_all(root);
    std::cout << "Runtime files reject symlink directories, preserve targets and publish securely through descriptors\n";
}
