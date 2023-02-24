#include <iostream>
#include <stdlib.h>
#include "Server/Server.h"
#include "HttpConn/HttpConn.h"
#include "ThreadPool/ThreadPool.h"
using namespace std;

int main(int argc, char * argv[])
{
    Server server(argv[1], atoi(argv[2]), "/resources", 5000);  // 相对路径
    server.run();

    return 0;
}

enum indent {BEGIN, END, MID};
int indent = -1;
void debug(string str, enum indent indent_change = MID)
{
    if (indent_change == BEGIN)
        ++indent;
    
    for (int i = 0; i < indent; ++i)
        cout << "  ";
    if (indent_change == BEGIN)
        cout << "[begin] ";
    else if (indent_change == END)
        cout << "[end  ] ";
    cout << str << endl;

    if (indent_change == END)
        --indent;
}
