#include <iostream>
#include <unistd.h>
#include <libgen.h>
#include "Server/Server.h"
#include "Pool/SQLConnPool.h"
#include "Log/Log.h"
using namespace std;

int main(int argc, char * argv[])
{
    if(argc <= 2)
    {
        cerr << "usage: " + string(basename(argv[0])) + " ip_address port_number" << endl;
        return 1;
    }

    string path(getcwd(nullptr, 0));  // 当前路径
    /* 日志初始化 */
    Log::get_instance().init(getpid(), path + "/log", "webServer", Log::INFO);
    /* mysql连接池初始化 */
    SQLConnPool::get_instance().init("127.0.0.1", 3306, "root", "jvlla", "webServer");
    /* 服务器初始化并运行 */
    Server server(argv[1], atoi(argv[2]), path + "/resources", 300, Server::REACTOR);
    server.run();

    return 0;
}
