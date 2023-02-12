#include "Server/Server.h"
#include "HttpConn/HttpConn.h"

int main(int argc, char * argv[])
{
    Server server("127.0.0.1", 12345, 5000);
    server.run();

    return 0;
}

// 或者想折在主函数里进行信号处理