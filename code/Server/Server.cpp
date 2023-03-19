#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include "Server.h"
#include "../Log/Log.h"
using namespace std;

extern int setnonblocking(int fd);
extern void addfd(int epollfd, int fd);
extern void modfd(int epollfd, int fd, int ev);
void addsig(int sig, void (*sig_handler)(int));
void sig_handler(int sig);
void timer_cb(struct timer_cb_arg * arg);

/* 静态变量在类外定义 */
bool Server::stop_server_ = false;
int Server::signal_fd_[2] = {0, 0};

Server::Server(string listen_ip, int listen_port, string path_resource, int timeout, enum PATTERN pattern)
    : pattern_(pattern), listen_ip_(listen_ip), listen_port_(listen_port), path_resource_(path_resource), timer_(), timeout_(timeout)
{
    log(Log::DEBUG, "[begin] Server::Server(string, int, string, int)");
    timer_cb_args_ = new struct timer_cb_arg[MAX_FD];
    log(Log::DEBUG, "[end  ] Server::Server(string, int, string, int)");
}

Server::~Server()
{
    log(Log::DEBUG, "[begin] Server::~Server()");
    stop_server_ = true;
    close(listen_fd_);
    close(epoll_fd_);
    for (pair<const int, HttpConn> connection: connections_)
    {
        close(connection.first);
        log(Log::INFO, "fd " + to_string(connection.first) + " closed");
    }
    delete [] timer_cb_args_;
    log(Log::DEBUG, "[end  ] Server::~Server()");
}

/* 首先进行socket监听标准流程，其次准备epoll、信号处理，最后循环监听epoll事件 */
void Server::run()
{
    log(Log::DEBUG, "[begin] Server::run()");

    /* TCP socket监听标准流程，socket(), bind()和listen() */
    /* 创建监听端口描述符 */
    listen_fd_ = socket(PF_INET, SOCK_STREAM, 0);
    assert(listen_fd_ > 0);
    struct linger tmp = {1, 0};
    setsockopt(listen_fd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    /* 处理socket地址 */
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, listen_ip_.c_str(), &address.sin_addr);
    address.sin_port = htons(listen_port_);
    /* socket绑定、监听 */
    ret = bind(listen_fd_, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0); 
    ret = listen(listen_fd_, 5);
    assert(ret >= 0);

    /* 创建EPOLL文件描述符 */
    epoll_fd_ = epoll_create(5);
    assert(epoll_fd_ > 0);
    addfd(epoll_fd_, listen_fd_);
    // HttpConn::epoll_fd_ = epoll_fd_;

    /* 建立用于接收信号处理结果的管道 */
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, signal_fd_);
    assert(ret != -1);
    setnonblocking(signal_fd_[1]);
    addfd(epoll_fd_, signal_fd_[0]);

    /* 为信号设置信号处理函数 */
    addsig(SIGPIPE, sig_handler);
    addsig(SIGHUP , sig_handler);
    addsig(SIGTERM, sig_handler);
    addsig(SIGINT , sig_handler);
    addsig(SIGTERM, sig_handler);
    addsig(SIGALRM, sig_handler);
    alarm(timer_.get_slot_interval());
    bool is_timeout = false;

    while (!stop_server_)
    {
        log(Log::INFO, "wait epoll");
        int number = epoll_wait(epoll_fd_, epoll_events_, MAX_EVENT_NUMBER, -1);
        /* 如果epoll_wait出错，且不是因为需要再次尝试(EAGAIN)或捕获信号(EINTR)，停止服务器 */
        if (number < 0 && errno != EAGAIN && errno != EINTR)
        {
            log(Log::FATAL, "epoll failure, errno: " + to_string(errno));
            stop_server_ = true;
            break;
        }

        /* 依次处理EPOLL事件 */
        for (int i = 0; i < number; ++i)
        {
            int fd = epoll_events_[i].data.fd;
            /* 处理连接请求 */
            if (fd == listen_fd_)
                deal_listen();
            /* 处理信号 */
            else if(fd == signal_fd_[0] && epoll_events_[i].events & EPOLLIN)
                deal_signal(is_timeout);
            /* 处理EPOLLIN事件 */
            else if (epoll_events_[i].events & EPOLLIN)
                deal_epollin(fd);
            /* 处理EPOLLOUT事件 */
            else if (epoll_events_[i].events & EPOLLOUT)
                deal_epollout(fd);
            /* 处理socket关闭等异常等情况 */
            else if (epoll_events_[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                log(Log::INFO, "fd " + to_string(fd) + " closed because of epoll error");
                connections_[fd].close_conn();
                timer_.del(&timer_cb_args_[fd]);
            }
        }
        modfd(epoll_fd_, listen_fd_, EPOLLIN);  // epoll事件设置EPOLLONESHOT，再次重置

        /* 最后处理定时事件 */
        if (is_timeout)
        {
            timer_.tick();
            alarm(timer_.get_slot_interval());
            is_timeout = false;
        }
    }

    log(Log::DEBUG, "[end  ] Server::run()");
}

void Server::deal_listen()
{
    log(Log::INFO, "listened fd trigger EPOLLIN");
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    int conn_fd = accept(listen_fd_, (struct sockaddr *) &client_address, &client_addrlength);
    if (conn_fd < 0)
    {
        log(Log::WARN, "accept() returned fd < 0, errno: " + to_string(errno));
        return;
    }
    /* 初始化HttpConn连接 */
    connections_[conn_fd].init(epoll_fd_, conn_fd, client_address, path_resource_);
    /* 添加定时器 */
    timer_cb_args_[conn_fd] = {epoll_fd_, conn_fd};
    timer_.add(timeout_, &timer_cb_args_[conn_fd], timer_cb);
}

void Server::deal_signal(bool &is_timeout)
{
    modfd(epoll_fd_, signal_fd_[0], EPOLLIN);  // epoll事件设置EPOLLONESHOT，再次重置
    char signals[1024];
    int ret;

    ret = recv(signal_fd_[0], signals, sizeof(signals), 0);
    if(ret == -1 || ret == 0)
    {
        log(Log::WARN, "receive signals failed, recv() return " + to_string(ret));
        return;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            log(Log::INFO, "catch signal " + to_string(signals[i]));
            switch (signals[i])
            {
                /* 忽略SIGPIPE, SIGHUP */
                case SIGPIPE:
                case SIGHUP:
                    continue;
                    break;
                /* 接收SIGALRM，设置is_timeout，最后处理 */
                case SIGALRM:
                    log(Log::INFO, "catch SIGALRM, trigger timer");
                    is_timeout = true;
                    break;
                /* 接收SIGTERM, SIGINT, SIGSEGV，停止服务器 */
                case SIGTERM:
                    log(Log::FATAL, "catch SIGTERM, stop server");
                    stop_server_ = true;
                    break;
                case SIGINT:
                    log(Log::FATAL, "catch SIGINT, stop server");
                    stop_server_ = true;
                    break;
                case SIGSEGV:
                    log(Log::FATAL, "catch SIGSEGV, stop server");
                    stop_server_ = true;
                    break;
            }
        }
    }
}

void Server::deal_epollin(int socket_fd)
{
    log(Log::INFO, "fd " + to_string(socket_fd) + " trigger EPOLLIN");
    /* 根据事件处理模式调用不同函数 */
    switch (pattern_)
    {
        case REACTOR:
            if (ThreadPool::get_instance().add_task([this, socket_fd](){
                this->connections_[socket_fd].read_and_process();
            }))
                log(Log::WARN, "thread pool add task failed");
            break;
        case PROACTOR:
            if (connections_[socket_fd].read())
                if (ThreadPool::get_instance().add_task([this, socket_fd](){
                    this->connections_[socket_fd].process();
                }))
                    log(Log::WARN, "thread pool add task failed");
            break;
        default:
            log(Log::FATAL, "unknown server event handling pattern " + to_string(pattern_));
            stop_server_ = true;
            break;
    }
    // connections[fd].read_and_process();  // 单线程，用于调试

    /* 重置定时器 */
    timer_.del(&timer_cb_args_[socket_fd]);
    timer_.add(timeout_, &timer_cb_args_[socket_fd], timer_cb);
}

void Server::deal_epollout(int socket_fd)
{
    log(Log::INFO, "fd " + to_string(socket_fd) + " trigger EPOLLOUT");
    /* 根据事件处理模式调用不同函数 */
    switch (pattern_)
    {
        case REACTOR:
            if (ThreadPool::get_instance().add_task([this, socket_fd](){
                this->connections_[socket_fd].write();
            }))
                log(Log::WARN, "thread pool add task failed");
            break;
        case PROACTOR:
            connections_[socket_fd].write();
            break;
        default:
            log(Log::FATAL, "unknown server event handling pattern " + to_string(pattern_));
            stop_server_ = true;
            break;
    }
    // connections[socket_fd].write();  // 单线程，用于调试
    
    /* 重置定时器 */
    timer_.del(&timer_cb_args_[socket_fd]);
    timer_.add(timeout_, &timer_cb_args_[socket_fd], timer_cb);
}

void addsig(int sig, void (*sig_handler)(int))
{
    log(Log::DEBUG, "[begin] addsig(int, void *(int))");
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
    log(Log::DEBUG, "[end  ] addsig(int, void *(int))");
}

void sig_handler(int sig)
{
    log(Log::DEBUG, "[begin] sig_handler(int)");
    int save_errno = errno;
    int msg = sig;
    send(Server::signal_fd_[1], (char *)&msg, 1, 0);
    errno = save_errno;
    log(Log::DEBUG, "[end  ] sig_handler(int)");
}

void timer_cb(struct timer_cb_arg * arg)
{
    log(Log::DEBUG, "[begin] timer_cb(struct timer_cb_arg)");
    log(Log::INFO, "callback function is called in fd " + to_string(arg->socket_fd));
    epoll_ctl(arg->epoll_fd, EPOLL_CTL_DEL, arg->socket_fd, nullptr);
    close(arg->socket_fd);
    log(Log::DEBUG, "[end  ] timer_cb(struct timer_cb_arg)");
}
