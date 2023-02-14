#include "Server/Server.h"
#include "HttpConn/HttpConn.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

int main(int argc, char * argv[])
{
    Server server("172.26.181.77", 10086, 5000);
    server.run();

    return 0;
}
