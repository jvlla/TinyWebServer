#include <stdlib.h>
#include "Server/Server.h"

int main(int argc, char * argv[])
{
    
    Server server(argv[1], atoi(argv[2]), "/resource", 5000);  // 相对路径
    server.run();

    return 0;
}
