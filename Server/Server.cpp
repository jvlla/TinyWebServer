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
using namespace std;

#include <iostream>

int setnonblocking(int fd);
void addfd(int epollfd, int fd);

Server::Server(string listen_ip, int listen_port, int time_out_ms)
        : listen_ip_(listen_ip), listen_port_(listen_port)
        , time_out_ms_(time_out_ms), stop_server_(false) {cout << "in" << endl;}

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
    assert(this->listen_fd_ > 0);
    struct linger tmp = {1, 0};
    setsockopt(listen_fd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, listen_ip_.data(), &address.sin_addr);
    address.sin_port = listen_port_;

    int ret = 0;
    ret = bind(listen_fd_, (struct sockaddr *) &address, sizeof(address));
    assert(ret >= 0);

    ret = listen(listen_fd_, 5);
    assert(ret >= 0);

    epoll_fd_ = epoll_create(5);
    assert(epoll_fd_ > 0);
    addfd(epoll_fd_, listen_fd_);

    // ret = socketpair(PF_UNIX, SOCK_STREAM, 0, signal_fd_);
    // assert(ret != -1);
    // setnonblocking(signal_fd_[1]);
    // addfd(epoll_fd_, signal_fd_[0]);
    // addsig(SIGHUP);
    // addsig(SIGPIPE);
    // addsig(SIGTERM);
    // addsig(SIGINT);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    while(!stop_server_)
    {
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

            }
            else if (epoll_events_[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // connections[fd].close();
            }
            else if (epoll_events_[i].events & EPOLLIN)
            {

            }
            else if (epoll_events_[i].events & EPOLLOUT)
            {

            }
        }
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

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}
