#include <string.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include "Server.h"
#include "../HttpConn/HttpConn.h"
#include "../Pool/ThreadPool.h"
using namespace std;

enum indent {BEGIN, END, MID};
extern void debug(string str, enum indent indent_change = MID);
extern int setnonblocking(int fd);
extern void addfd(int epollfd, int fd);
extern void modfd(int epollfd, int fd, int ev);
void addsig(int sig, void (*sig_handler)(int));
void sig_handler(int sig);

/* 静态变量在类外定义 */
bool Server::stop_server_ = false;
int Server::signal_fd_[2] = {0, 0};

Server::Server(string listen_ip, int listen_port, string path_resource, int time_out_ms)
    : listen_ip_(listen_ip), listen_port_(listen_port), time_out_ms_(time_out_ms)
{
    debug("Server::Server(string, int, string, int)", BEGIN);
    char *path = NULL;
    path = getcwd(NULL, 0);
    path_resource_ =  path + path_resource;
    free(path);
    debug("Server::Server(string, int, string, int)", END);
}

Server::~Server()
{
    debug("Server::~Server()", BEGIN);
    stop_server_ = true;
    close(listen_fd_);
    close(epoll_fd_);
    for (pair<const int, HttpConn> connection: connections)
    {
        close(connection.first);
        debug("fd " + to_string(connection.first) + " closed");
    }
    debug("Server::~Server()", END);
}

void Server::run()
{
    debug("Server::run()", BEGIN);
    listen_fd_ = socket(PF_INET, SOCK_STREAM, 0);
    assert(listen_fd_ > 0);
    struct linger tmp = {1, 0};
    setsockopt(listen_fd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, listen_ip_.c_str(), &address.sin_addr);
    address.sin_port = htons(listen_port_);

    ret = bind(listen_fd_, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0); 
    ret = listen(listen_fd_, 5);
    assert(ret >= 0);

    epoll_fd_ = epoll_create(5);
    assert(epoll_fd_ > 0);
    addfd(epoll_fd_, listen_fd_);
    /* 设置HttpConn类的epoll事件文件描述符 */
    HttpConn::epoll_fd_ = epoll_fd_;

    /* 建立用于接收信号处理结果的管道 */
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, signal_fd_);
    assert(ret != -1);
    setnonblocking(signal_fd_[1]);
    addfd(epoll_fd_, signal_fd_[0]);
    /* 为信号设置信号处理函数 */
    addsig(SIGPIPE, sig_handler);
    addsig(SIGHUP, sig_handler);
    addsig(SIGTERM, sig_handler);
    addsig(SIGINT, sig_handler);

    while(!stop_server_)
    {
        debug("wait epoll");
        int number = epoll_wait(epoll_fd_, epoll_events_, MAX_EVENT_NUMBER, -1);
        /* 如果epoll_wait出错，且不是因为需要再次尝试(EAGAIN)或捕获信号(EINTR) */
        if (number < 0 && errno != EAGAIN && errno != EINTR)
        {
            debug("epoll failure, errno: " + to_string(errno));
            break;
        }

        for (int i = 0; i < number; ++i)
        {
            int fd = epoll_events_[i].data.fd;
            if (fd == listen_fd_)
            {
                debug("get listen");
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int conn_fd = accept(listen_fd_, (struct sockaddr *) &client_address, &client_addrlength);
                if (conn_fd < 0)
                {
                    debug("errno: " + to_string(errno));
                    continue;
                }
                connections[conn_fd].init(conn_fd, client_address, path_resource_);
            }
            /* 处理信号 */
            else if(fd == signal_fd_[0] && epoll_events_[i].events & EPOLLIN)
            {
                char signals[1024];
                ret = recv(signal_fd_[0], signals, sizeof(signals), 0);
                if(ret == -1 || ret == 0)
                    continue;
                else
                {
                    for(int i = 0; i < ret; ++i)
                    {
                        debug("catch signal " + to_string(signals[i]));
                        switch(signals[i])
                        {
                            case SIGPIPE:
                            case SIGHUP:
                                continue;
                                break;
                            case SIGTERM:
                            case SIGINT:
                                debug("get sig, stop server");
                                stop_server_ = true;
                                break;
                        }
                    }
                }
            }
            else if (epoll_events_[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                connections[fd].close_conn();
            }
            else if (epoll_events_[i].events & EPOLLIN)
            {
                debug("fd " + to_string(fd) + " trigger EPOLLIN");
                ThreadPool<HttpConn>::get_instance().add_task(&connections[fd]);
                // connections[fd].read_and_process();
            }
            else if (epoll_events_[i].events & EPOLLOUT)
            {
                debug("fd " + to_string(fd) + " trigger EPOLLOUT");
                connections[fd].write();
            }
        }
        modfd(epoll_fd_, listen_fd_, EPOLLIN);
    }

    debug("Server::run()", END);
}

void addsig(int sig, void (*sig_handler)(int))
{
    debug("addsig(int, void *(int))", BEGIN);
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
    debug("addsig(int, void *(int))", END);
}

void sig_handler(int sig)
{
    debug("sig_handler(int)", BEGIN);
    int save_errno = errno;
    int msg = sig;
    send(Server::signal_fd_[1], (char *)&msg, 1, 0);
    errno = save_errno;
    debug("sig_handler(int)", END);
}
