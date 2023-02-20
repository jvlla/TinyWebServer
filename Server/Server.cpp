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
using namespace std;

#include <iostream>
#include <cstdio>

extern int setnonblocking(int fd);
extern void addfd(int epollfd, int fd);
extern void modfd(int epollfd, int fd, int ev);

Server::Server(string listen_ip, int listen_port, string path_resource, int time_out_ms)
        : listen_ip_(listen_ip), listen_port_(listen_port), time_out_ms_(time_out_ms)
{
    path_resource_ = path_resource;
    stop_server_ = false;
}

Server::~Server()
{
    this->stop_server_ = true;
    close(listen_fd_);
    // close(signal_fd_[0]);
    // close(signal_fd_[1]);
}

void Server::run()
{
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

    // ret = socketpair(PF_UNIX, SOCK_STREAM, 0, signal_fd_);
    // assert(ret != -1);
    // setnonblocking(signal_fd_[1]);
    // addfd(epoll_fd_, signal_fd_[0]);
    // addsig(SIGHUP);
    // addsig(SIGPIPE);
    // addsig(SIGTERM);
    // addsig(SIGINT);
    signal(SIGPIPE, SIG_IGN);
    // signal(SIGHUP, SIG_IGN);

    while(!stop_server_)
    {
        cout << "wait epoll" << endl;
        int number = epoll_wait(epoll_fd_, epoll_events_, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EAGAIN)
        {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; ++i)
        {
            int fd = epoll_events_[i].data.fd;
            if (fd == listen_fd_)
            {

                cout << "get listen" << endl;
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int conn_fd = accept(listen_fd_
                    , (struct sockaddr *) &client_address, &client_addrlength);
                if (conn_fd < 0)
                {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                connections[conn_fd].init(conn_fd, client_address, path_resource_);
            }
            else if (epoll_events_[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                connections[fd].close_conn();
            }
            else if (epoll_events_[i].events & EPOLLIN)
            {
                printf("%d in\n", fd);
                printf("read and process\n");
                ThreadPool<HttpConn>::get_instance().add_task(&connections[fd]);
                // connections[fd].read_and_process();
            }
            else if (epoll_events_[i].events & EPOLLOUT)
            {
                printf("%d out\n", fd);
                connections[fd].write();
            }
        }
        modfd(epoll_fd_, listen_fd_, EPOLLIN);
    }
}

// 不行，这信号处理配上对象怎么想怎么都不对，再议再议
// void Server::sig_handler(int sig)
// {
//     int save_errno = errno;
//     int msg = sig;
//     send(signal_fd_[1], (char *)&msg, 1, 0);
//     errno = save_errno;
// }

// void Server::addsig(int sig)
// {
//     struct sigaction sa;
//     memset(&sa, '\0', sizeof(sa));
//     sa.sa_handler = Server::sig_handler;
//     sa.sa_flags |= SA_RESTART;
//     sigfillset(&sa.sa_mask);
//     assert(sigaction(sig, &sa, NULL) != -1);
// }
