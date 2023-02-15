// -------------------------------记得删---------------------------------
#include <iostream>

#include <string>
#include <algorithm>
#include <regex>
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
    check_state_ = CHECK_REQUEST_LINE;
    response_code_ = OK_200;
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
    // 这里读的这个长度也要改，反正这个在看了
    while((bytes_read = recv(socket_fd_, read_buffer_.data(), read_buffer_.size(),0)) > 0)
    {
        // 先这样，假设不管content而且一次能读完
        break;
    }
    // 这里先假设没问题，但是要是出错了没有\r\n\r\n怎么办？
    // 删除content前\r\n\r\n
    header_size_ = bytes_read - 4;
    read_buffer_.resize(header_size_);  

//-------------------------------------------------------------------------
    cout << "received length: " << bytes_read << endl;
    cout << read_buffer_.size() << endl;
    string header(read_buffer_.begin(), read_buffer_.end());
    cout << header << endl << endl;

    return true;
}

void HttpConn::write()
{
    cout << "in write()" << endl;
    string out(write_buffer_.begin(), write_buffer_.end());
    cout << out << endl;
    int temp = writev(socket_fd_, iv_, iv_count_);
    cout << temp << endl;
}

void HttpConn::read_and_process()
{
    /* 如果未完全 */
    if (!read())
        return;
    
    //-----------------------------------------------------
    // switch(process_read())
    response_code_ = NOT_FOUND_404;
    switch(READ_ERROR)
    {
        case READ_COMPLETE:
            process_file();
            process_write();
            break;
        /* 如果调用process_read()函数中出错，响应报文不需要content */
        case READ_ERROR:
            iv_count_ = 1;
            process_write();
            break;
        /* read_state状态出错，设置http状态码500 */
        default:
            response_code_ = INTERNAL_SERVER_ERROR_500;
            iv_count_ = 1;
            process_write();
            break;
    }
    // 这里是不是还得加个变量看能不能写的
    modfd(epoll_fd_, socket_fd_, EPOLLOUT);
}

enum HttpConn::READ_STATE HttpConn::process_read()
{
    READ_STATE read_state = READ_INCOMPLETE;

    cout << "here" << endl;
    line_iterator_ = read_buffer_.begin();
    while(read_state == READ_INCOMPLETE)
    {
        std::string line;

        /* 当非CHECK_CONTENT时，解析下一行，调用parse_line获得请求头中一行.
         * 如果该行出错，退出循环，设置http状态码400 
         */
        if(check_state_ != CHECK_CONTENT && !parse_line(line))
        {
            read_state = READ_ERROR;
            response_code_ = BAD_REQUEST_400;
            break;  // 跳出while，避免对出错行调用parse_*函数
        }
        cout << "line:  " << line << endl;

        /* check_state_状态转移在各个parse_*函数中完成 */ 
        switch(check_state_)
        {
            case CHECK_REQUEST_LINE:
                read_state = parse_request_line(line);
                break;
            case CHECK_HEADER:
                read_state = parse_header(line);
                break;
            case CHECK_CONTENT:
                read_state = parse_content();
                break;
            /* CHECK_STATE状态出错，设置http状态码500 */
            default:
                read_state = READ_ERROR;
                response_code_ = INTERNAL_SERVER_ERROR_500;
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

enum HttpConn::READ_STATE HttpConn::parse_request_line(std::string &line)
{
    regex pattern("^([A-Z]{3,7}) (/.*) HTTP/(\\d\\.\\d)");
    smatch results;
    bool is_legal = true;  // 请求头是否合法

    if (regex_match(line, results, pattern))
    {
        /* 检查请求方法 */
        string method = results[1];
        if (method == "GET")
            request_method_ = GET;
        else if (method == "POST")
            request_method_ = POST;
        else if (method == "HEAD")
            request_method_ = HEAD;
        else if (method == "PUT")
            request_method_ = PUT;
        else if (method == "DELETE")
            request_method_ = DELETE;
        else if (method == "TRACE")
            request_method_ = TRACE;
        else if (method == "OPTIONS")
            request_method_ = OPTIONS;
        else if (method == "CONNECT")
            request_method_ = CONNECT;
        else if (method == "PATCH")
            request_method_ = PATCH;
        else
            is_legal = false;
        /* 转换请求URL */
        request_url_ = results[2];
        /* 检查请求版本 */
        string version = results[3];
        if (version == "0.9")
            request_version_ = HTTP_09;
        else if (version == "1.0")
            request_version_ = HTTP_10;
        else if (version == "1.1")
            request_version_ = HTTP_11;
        else if (version == "2.0")
            request_version_ = HTTP_20;
        else
            is_legal = false;
    }
    else
        is_legal = false;

    check_state_ = CHECK_HEADER;  // CHECK_STATE状态转移
    /* 当请求行合法时，继续处理，否则停止处理，返回http400报文 */
    if (is_legal)
        return READ_INCOMPLETE;  // READ_STATE状态转移
    else
    {
        response_code_ = BAD_REQUEST_400;
        return READ_ERROR;  // READ_STATE状态转移
    }
}

enum HttpConn::READ_STATE HttpConn::parse_header(std::string &line)
{
    regex pattern("([a-zA-Z-]+): (.*)");  // 注意"-"
    smatch results;
    bool is_legal = true;

    if (regex_match(line, results, pattern))
    {
        string field = results[1];
        string value = results[2];

        if (field == "Host")
            request_host_ = value;
        else if (field == "Connection")
            is_keep_alive_ = true;
        else if (field == "User-Agent")
            request_user_agent_ = value;
        else if (field == "Content-Length")  // Content-Length在read()中已处理
            ;
        else if (field == "Accept")
            request_accept_ = value;
        else if (field == "Accept-Encoding")
            request_accept_encoding = value;
        else if (field == "Accept-Language")
            request_accept_language = value;
        else
            cout << "unknown header:  " << line << endl;
    }
    else 
    {
        cout << "illegal:  " << line << endl;
        is_legal = false;
    }

    /* line_iterator_超过read_buffer_尾后迭代器说明已经处理完全部请求头，可以开始处理content */
    if (line_iterator_ >= read_buffer_.end())
    {
        cout << "to content" << endl;
        check_state_ = CHECK_CONTENT;  // CHECK_STATE状态转移
    }
                
    /* 当请求头合法时，继续处理，否则停止处理，返回http400报文 */
    if (is_legal)
        return READ_INCOMPLETE;  // READ_STATE状态转移
    else
    {
        response_code_ = BAD_REQUEST_400;
        return READ_ERROR;  // READ_STATE状态转移
    }
}

enum HttpConn::READ_STATE HttpConn::parse_content()
{
    cout << "in parse_content()" << endl;
    return READ_COMPLETE;
}

void HttpConn::process_file()
{

}

void HttpConn::process_write()
{
    add_response_status_line();
    add_response_header();
    if (response_code_ != OK_200)
    {

    }
    iv_[0].iov_base = write_buffer_.data(); 
    iv_[0].iov_len = write_buffer_.size();
}

void HttpConn::add_response_status_line()
{
    string status;

    status += "HTTP/1.1 ";  // 默认使用htpp 1.1
    switch(response_code_)
    {
        case OK_200:
            status += "200 OK";
            break;
        case BAD_REQUEST_400:
            status += "400 Bad Request";
            break;
        case FORBIDDEN_403:
            status += "403 Forbidden";
            break;
        case NOT_FOUND_404:
            status += "404 Not Found";
            break;
        case INTERNAL_SERVER_ERROR_500:
            status += "500 Internal Server Error";
            break;
        /* RESPONSE_CODE状态出错，默认http 500 */
        default:
            status += "500 Internal Server Error";
    }
    write_buffer_.insert(write_buffer_.end(), status.begin(), status.end());
    add_response_crlf();
}

void HttpConn::add_response_header()
{
    add_response_content_length();
    add_response_linger();
    add_response_crlf();
    //-----------------------------------------------------------------
    string content = "404 Not Found";
    write_buffer_.insert(write_buffer_.end(), content.begin(), content.end());
}

void HttpConn::add_response_content_length()
{
    // ----------------------------先凑合一下----------------------
    string length = "Content-Length: 13";
    write_buffer_.insert(write_buffer_.end(), length.begin(), length.end());
    add_response_crlf();
}

void HttpConn::add_response_linger()
{
    string connection = "Connection: ";

    connection += (is_keep_alive_ == true ) ? "keep-alive" : "close";
    write_buffer_.insert(write_buffer_.end(), connection.begin(), connection.end());
    add_response_crlf();
}

void HttpConn::add_response_crlf()
{
    write_buffer_.push_back('\r');
    write_buffer_.push_back('\n');
}