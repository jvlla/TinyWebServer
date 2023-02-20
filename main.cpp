#include <stdlib.h>
#include "Server/Server.h"
#include "HttpConn/HttpConn.h"
#include "ThreadPool/ThreadPool.h"

int main(int argc, char * argv[])
{
    Server server(argv[1], atoi(argv[2]), "/resources", 5000);  // 相对路径
    server.run();

    return 0;
}
