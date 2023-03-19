#ifndef __SERVER__
#define __SERVER__

#include <string>
#include <unordered_map>
#include <sys/epoll.h>
#include "../HttpConn/HttpConn.h"
#include "../Pool/ThreadPool.h"
#include "../TimerWheel/TimerWheel.h"

class Server {
public:
    enum PATTERN {REACTOR, PROACTOR};               // 事件处理模式

    /* 构造函数，参数分别为监听IP、监听端口、资源路径、非活动连接超时时间和服务器事件处理模式 */
    Server(std::string listen_ip, int listen_port, std::string path_resources, int timeout = 300, enum PATTERN pattern = REACTOR);
    /* 析构函数 */
    ~Server();
    /* 服务器运行，循环监听事件 */ 
    void run();
    
    static bool stop_server_;                       // 是否停止服务器运行
    static int signal_fd_[2];                       // 接收终端信号的管道

private:
    /* 处理监听socket文件描述符上EPOLLIN事件 */
    void deal_listen();
    /* 处理信号处理函数发送回信号 */
    void deal_signal(bool &is_timeout);
    /* 处理连接socket文件描述符上EPOLLIN事件 */
    void deal_epollin(int fd);
    /* 处理连接socket文件描述符上EPOLLOUT事件 */
    void deal_epollout(int fd);

    static const int MAX_FD = 65536;                // 支持最大文件描述符数
    static const int MAX_EVENT_NUMBER = 30000;      // 支持最大EPOLL事件数
    const enum PATTERN pattern_;                    // 服务器事件处理模式
    const std::string listen_ip_;                   // 监听IP
    const int listen_port_;                         // 监听端口
    std::string path_resource_;                     // 资源路径
    int listen_fd_;                                 // 监听端口文件描述符
    int epoll_fd_;                                  // EPOLL文件描述符
    epoll_event epoll_events_[MAX_EVENT_NUMBER];    // EPOLL监听事件
    std::unordered_map<int, HttpConn> connections_; // socket文件描述符对应连接
    struct timer_cb_arg * timer_cb_args_;           // socket文件描述符对应回调函数参数数组,
                                                    // 由于定时器中使用了unordered_map，需要传递原始指针用于判断
    TimerWheel<struct timer_cb_arg *> timer_;       // 时间轮定时器
    int timeout_;                                   // 非活动连接超时时间，以秒为单位
};

struct timer_cb_arg {
    int epoll_fd;
    int socket_fd;
};

#endif