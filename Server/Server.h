#ifndef __SERVER__
#define __SERVER__

#include <string>
#include <cstring>
#include <unordered_map>
#include <memory>
#include <sys/epoll.h>
#include "../HttpConn/HttpConn.h"
#include "../ThreadPool/ThreadPool.h"
#include "../Timer/Timer.h"

class Server {
public:
    Server(std::string listen_ip, int listen_port, std::string path_resources
        , int time_out_ms);
    ~Server();
    /* 服务器运行，循环监听事件 */ 
    void run();

private:
    void deal_listen();
    void deal_read_to();
    void deal_write_from();
    // 这得看一下啊，怎么弄，这封装不对劲啊
    // void sig_handler(int sig);
    // void addsig(int sig);

    static const int MAX_FD = 65536;
    static const int MAX_EVENT_NUMBER = 30000;
    bool stop_server_;
    const std::string listen_ip_;                       // IP
    const int listen_port_;                             // 端口
    int listen_fd_;                                     // 监听端口文件描述符
    // int signal_fd_[2];                               // 用来接收中断的
    int epoll_fd_;                                      // 事件指针
    std::shared_ptr<ThreadPool<HttpConn>> thread_pool_; // 线程池指针
    Timer<HttpConn> timer_;                             // 定时器指针
    int time_out_ms_;                                   // 定时器间隔，以毫秒为单位
    std::string path_resource_;                        // 资源路径
    std::unordered_map<int, HttpConn> connections;      // http连接
    epoll_event epoll_events_[MAX_EVENT_NUMBER];        // epoll监听事件
    // 后面还得有和数据库相关的
};

#endif