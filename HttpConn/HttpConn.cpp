// -------------------------------记得删---------------------------------
#include <iostream>

#include <string>
#include <algorithm>
#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "HttpConn.h"
using namespace std;

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
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd( int epollfd, int fd )
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd( int epollfd, int fd, int ev )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

/* static成员变量epoll_fd_在类定义外进行初始化 */
int HttpConn::epoll_fd_ = -1;

HttpConn::HttpConn()
{
    socket_fd_ = -1;
    address_ = {0};
    is_keep_alive_ = false;
    read_buffer_.resize(READ_BUFFER_SIZE);
    read_buffer_.resize(WRITE_BUFFER_SIZE);
    check_state_ = HttpConn::CHECK_HEADER;
}

void HttpConn::init(int fd, sockaddr_in address)
{
    socket_fd_ = fd;
    address_ = address;
    /* 允许端口复用 */
    int error = 0;
    socklen_t len = sizeof( error );
    getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &error, &len);
    int reuse = 1;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    /* 将socket文件描述符加入epoll事件表 */
    addfd(epoll_fd_, socket_fd_);
}

void HttpConn::close_conn()
{
    if (socket_fd_ != -1)
    {
        removefd(epoll_fd_, socket_fd_);
        socket_fd_ = -1;
    }
}

/* 首先先读取一部分，然后靠查找\r\n\r\n找到content开始的部分，拷贝到打开的mmap地址。
 * 靠查找"Content-Length"确定长度，直到把内容读完才可以返回true，搞定
 */
bool HttpConn::read()
{
    int bytes_read = 0;
    while((bytes_read = recv(socket_fd_, &read_buffer_[0], read_buffer_.size(),0)) > 0)
    {
        // 先这样，假设不管content而且一次能读完
        break;
    }
    // 这里先假设没问题，但是要是出错了没有\r\n\r\n怎么办？
    // 删除content前\r\n\r\n
    read_buffer_.resize(bytes_read - 4);  
    header_size_ = bytes_read - 4;

//-------------------------------------------------------------------------
    cout << "received length: " << bytes_read << endl;
    string header(read_buffer_.begin(), read_buffer_.end());
    cout << header << endl << endl;

    return true;
}

void HttpConn::write()
{

}

void HttpConn::read_and_process()
{
    /* 如果未完全 */
    if (!read())
        return;
    
    switch(process_read())
    {
        case READ_COMPLETE:
            process_file();
            process_write();
            break;
        /* 如果调用process_read()函数中出错，响应报文不需要content */
        case READ_ERROR:
            iv_count_ = 1;  // 只需要一个iovec结构体
            process_write();
            break;
        /* read_state状态出错，设置http状态码500 */
        default:
            http_code_ = INTERNAL_SERVER_ERROR_500;
            iv_count_ = 1;
            process_write();
            break;
    }
}

enum HttpConn::READ_STATE HttpConn::process_read()
{
    HttpConn::READ_STATE read_state = HttpConn::READ_INCOMPLETE;

    line_iterator_ = read_buffer_.begin();
    while(read_state == READ_INCOMPLETE)
    {
        std::string line;

        /* 当非CHECK_CONTENT时，解析下一行，调用parse_line获得请求头中一行.
         * 如果该行出错，退出循环，设置http状态码400 
         */
        if(check_state_ != HttpConn::CHECK_CONTENT && !parse_line(line))
        {
            read_state = READ_ERROR;
            http_code_ = BAD_REQUEST_400;
            break;  // 跳出while，避免对出错行调用parse_*函数
        }
        cout << "line:  " << line << endl;

        /* check_state_状态转移在各个parse_*函数中完成 */ 
        switch(check_state_)
        {
            case HttpConn::CHECK_HEADER:
                read_state = parse_header(line);
                break;
            case HttpConn::CHECK_REQUEST_LINE:
                read_state = parse_request_line(line);
                break;
            case HttpConn::CHECK_CONTENT:
                read_state = parse_content();
                break;
            /* check_state状态出错，设置http状态码500 */
            default:
                read_state = READ_ERROR;
                http_code_ = HttpConn::INTERNAL_SERVER_ERROR_500;
        }
    }

    return read_state;
}

bool HttpConn::parse_line(std::string &line)
{
    vector<char>::iterator end;

    end = std::find(line_iterator_, read_buffer_.end(), '\r');
    /* 如果"r"后不存在请求头或"\r\n"不连续，报文错误 */
    if (line_iterator_ == read_buffer_.end() || *(end + 1) != '\n')
        return false;
    else
    {
        line.assign(line_iterator_, end);
        line_iterator_ = end + 2;
        return true;
    }
}

enum HttpConn::READ_STATE HttpConn::parse_header(std::string &line)
{

    check_state_ = HttpConn::CHECK_REQUEST_LINE;
    return HttpConn::READ_INCOMPLETE;
}

enum HttpConn::READ_STATE HttpConn::parse_request_line(std::string &line)
{
    if (line_iterator_ >= read_buffer_.end())
        check_state_ = HttpConn::CHECK_CONTENT;
    
    return HttpConn::READ_INCOMPLETE;
}

enum HttpConn::READ_STATE HttpConn::parse_content()
{
    return HttpConn::READ_COMPLETE;
}

void HttpConn::process_file()
{

}

void HttpConn::process_write()
{

}
