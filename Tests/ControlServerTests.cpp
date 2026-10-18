// SPDX-License-Identifier: GPL-3.0-or-later
#include "ControlServer.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <filesystem>
int main() {
    char pattern[]="/private/tmp/horndis-control-tests.XXXXXX";
    auto root=mkdtemp(pattern); assert(root);
    const std::string directory=std::string(root)+"/runtime", path=directory+"/control.sock";
    horndis::ControlServer server(directory, [] { return getuid(); });
    std::string error; assert(server.start(error));
    auto connectClient=[&] {
        int fd=socket(AF_UNIX,SOCK_STREAM,0); assert(fd>=0);
        int one=1; assert(setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one))==0);
        sockaddr_un address{}; address.sun_family=AF_UNIX;
        assert(path.size()<sizeof(address.sun_path)); strcpy(address.sun_path,path.c_str());
        assert(connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
        return fd;
    };
    int delayed=connectClient();
    assert(server.pollCommands().empty());
    assert(write(delayed,"dis",3)==3);
    assert(server.pollCommands().empty());
    assert(write(delayed,"connect\n",8)==8);
    auto commands=server.pollCommands();
    assert(commands.size()==1 && commands[0]==horndis::ControlCommand::disconnect);
    close(delayed);
    int invalid=connectClient(); assert(write(invalid,"disconnect-evil\n",16)==16);
    assert(server.pollCommands().empty()); close(invalid);
    int idle=connectClient(); assert(server.pollCommands().empty());
    int normal=connectClient(); assert(write(normal,"connect\n",8)==8);
    commands=server.pollCommands();
    assert(commands.size()==1 && commands[0]==horndis::ControlCommand::connect);
    close(normal); close(idle);
    server.close();
    std::filesystem::remove_all(root);
    std::cout<<"Control server preserves delayed/fragmented commands, rejects invalid prefixes and never waits for idle clients\n";
}
