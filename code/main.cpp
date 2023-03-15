#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <unistd.h>
#include "Server/Server.h"
#include "Pool/SQLConnPool.h"
#include "Log/Log.h"
using namespace std;

fstream of;

int main(int argc, char * argv[])
{
    /* 日志初始化 */
    Log::get_instance().init(getpid(), string(getcwd(nullptr, 0)) + "/log", "webServer", Log::INFO);
    /* mysql连接池初始化 */
    SQLConnPool::get_instance().init("127.0.0.1", 3306, "root", "jvlla", "webServer");
    Server server(argv[1], atoi(argv[2]), "/resources", 5000);  // 相对路径
    server.run();

    return 0;
}
