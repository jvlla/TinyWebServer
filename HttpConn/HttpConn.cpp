// -------------------------------记得删---------------------------------
#include <iostream>

#include <string>
#include <vector>
#include <algorithm>
#include <regex>
#include <cstdlib>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "HttpConn.h"
using namespace std;

void addfd(int epollfd, int fd);
void removefd(int epollfd, int fd);
void modfd(int epollfd, int fd, int ev);

/* static成员变量在类定义外进行初始化 */
int HttpConn::epoll_fd_ = -1;
const unordered_map<enum HttpConn::HTTP_CODE, string> HttpConn::RESPONSE_ERROR_CODE_TO_CONTENT = {
    {BAD_REQUEST_400, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Bad Request</title></head>" \
        "<body><p><b>Your request has bad syntax or is inherently impossible to satisfy.\n</b></p></body></html>"},
    {FORBIDDEN_403, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Forbidden</title></head>" \
        "<body><p><b>You do not have permission to get file from this server.\n</b></p></body></html>"},
    {NOT_FOUND_404, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Not Found</title></head>" \
        "<body><p><b>The requested file was not found on this server.\n</b></p></body></html>"},
    {INTERNAL_SERVER_ERROR_500, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Internal Error</title></head>" \
        "<body><p><b>There was an unusual problem serving the requested file.\n</b></p></body></html>"},
};

HttpConn::HttpConn()
{
    socket_fd_ = -1;
    address_ = {0};
}

void HttpConn::init(int fd, sockaddr_in address, string path_resource)
{
    cout << "in init" << endl;
    socket_fd_ = fd;
    address_ = address;
    path_resource_ = path_resource;
    /* 允许端口复用 */
    int error = 0;
    socklen_t len = sizeof( error );
    getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &error, &len);
    int reuse = 1;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    //-------------------------照着人家继续完善---------------------------
    /* 将socket文件描述符加入epoll事件表 */
    addfd(epoll_fd_, socket_fd_);
    /* 将响应报文发送文件stat结构体置零，避免使用socket时出错 */
    memset(&response_file_stat_, 0, sizeof(response_file_stat_));
    cout << "after init" << endl;
    init();
}

void HttpConn::init()
{
    is_linger = false;
    read_buffer_.resize(READ_BUFFER_SIZE);
    response_file_ = nullptr;
    memset(&response_file_stat_, 0, sizeof(response_file_stat_));
    check_state_ = CHECK_REQUEST_LINE;
    response_code_ = OK_200;
}

void HttpConn::close_conn()
{
    if (socket_fd_ != -1)
    {
        removefd(epoll_fd_, socket_fd_);
        socket_fd_ = -1;
    }
}

void HttpConn::clear_conn()
{
    /* 清空缓存中内容 */
    vector<char>().swap(read_buffer_);
    vector<char>().swap(write_buffer_);
    init();
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
    cout << "write buffer: " << endl;
    string buf(write_buffer_.begin(), write_buffer_.end());
    cout << buf << endl;
    printf("iv ct %d, iv len1 %d, iv len2 %d\n", iv_count_
        , (int) iv_[0].iov_len, (int) iv_[1].iov_len);

    while (true)
    {
        int bytes_sended = writev(socket_fd_, iv_, iv_count_);
        cout << "bytes_sended: " << bytes_sended << endl;
        if (bytes_sended <= -1)
        {
            cout << "write() errono " << errno << endl;
            /* 如果错误为EAGAIN，重复设置EPOLLOUT事件等待触发 */
            if (errno == EAGAIN)
            {
                cout << "write() eagain" << endl;
                modfd(epoll_fd_, socket_fd_, EPOLLOUT);
            }
            /* 否则说明出错，关闭连接 */
            else
            {
                unmap_file();
                close_conn();
            }
            return;
        }

        printf("send %d, base: %ld, len: %d\n", bytes_sended
            , (long) iv_[1].iov_base, (int) iv_[1].iov_len);
        /* 处理未发送完情况下的地址和长度 */
        if (static_cast<size_t>(bytes_sended) >= iv_[0].iov_len)
        {
            iv_[1].iov_base = (uint8_t *) iv_[1].iov_base + bytes_sended - iv_[0].iov_len;
            iv_[1].iov_len -= bytes_sended - iv_[0].iov_len;
            iv_[0].iov_len = 0;
        }
        else
        {
            iv_[0].iov_len -= bytes_sended;
            iv_[0].iov_base = (uint8_t *) iv_[0].iov_base + bytes_sended;
        }
        /* 如果发送完全*/
        if (iv_[1].iov_len <= 0)
        {
            cout << "发送完" << endl;
            unmap_file();
            if (is_linger)
            {
                cout << "is linger, reset epollin" << endl;
                modfd(epoll_fd_, socket_fd_, EPOLLIN);
                clear_conn();
            }
            else
                close_conn();
            return;
        }
    }
}

void HttpConn::read_and_process()
{
    /* 如果未完全读入请求报文，等待下次RPOLLIN事件，继续调用read()函数 */
    if (!read())
        return;
    
    cout << "after read" << endl;
    switch(process_read())
    {
        //----------------这个如果404,403什么的，不知道传个o的iovec行不行-----------------
        case READ_COMPLETE:
            cout << "read_complete" << endl;
            cout << response_code_ << endl;
            iv_count_ = 2;
            response_code_ = process_file();
            cout << response_code_ << endl;
            process_write();
            break;
        /* 如果调用process_read()函数中出错，响应报文不需要content */
        case READ_ERROR:
            iv_count_ = 1;
            process_write();
            break;
        /* read_state_状态出错，设置http状态码500 */
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
            /* check_state_状态出错，设置http状态码500 */
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
        cout << "url: " << request_url_ << endl;
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

    check_state_ = CHECK_HEADER;  // check_state_状态转移
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
        {
            if (value == "keep-alive")
                is_linger = true;
            else if (value == "close")
                is_linger = false;
            else
                is_legal = false;
        }   
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

enum HttpConn::HTTP_CODE HttpConn::process_file()
{
    if (response_code_ != OK_200)
        return response_code_;
    string path_file;
    char path[1000];
    int fd;

    /* 处理文件名 */
    if (!getcwd(path, sizeof(path)))
        return INTERNAL_SERVER_ERROR_500;
    path_file = path;
    if (request_url_ == "/")
        path_file += path_resource_ + "/welcome.html";
    else
        path_file += path_resource_ + request_url_;
    printf("%s\n", path_file.data());

    /* 处理错误情况 */
    if (stat(path_file.data(), &response_file_stat_) < 0)
    {
        cout << "in 404" << endl;
        return NOT_FOUND_404;
    }
    if (!(response_file_stat_.st_mode & S_IROTH))
        return FORBIDDEN_403;
    if (S_ISDIR(response_file_stat_.st_mode))
        return BAD_REQUEST_400;
    
    /* 进行文件映射 */
    fd = open(path_file.data(), O_RDONLY);
    response_file_ = (char *) mmap(0, response_file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return OK_200;
}

void HttpConn::process_write()
{
    add_response_status_line();
    add_response_header();
    iv_[0].iov_base = write_buffer_.data(); 
    iv_[0].iov_len = write_buffer_.size();
    if (response_code_ == OK_200)
    {
        cout << "in add file" << endl;
        cout << "file size: " << response_file_stat_.st_size << endl;

        iv_[1].iov_base = response_file_; 
        iv_[1].iov_len = response_file_stat_.st_size;
    }
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
        /* response_code_状态出错，默认http 500 */
        default:
            status += "500 Internal Server Error";
    }
    add_to_response(status);
    add_response_crlf();
}

void HttpConn::add_response_header()
{
    add_response_content_length();
    add_response_linger();
    add_response_crlf();
    if (response_code_ != OK_200)
        add_response_error_content();
}

void HttpConn::add_response_content_length()
{
    string content_length = "Content-Length: ";

    if (response_code_ == OK_200)
        content_length += to_string(response_file_stat_.st_size);
    else
        content_length += 
            to_string(RESPONSE_ERROR_CODE_TO_CONTENT.at(response_code_).size());
    add_to_response(content_length);
    add_response_crlf();

    cout << content_length << endl;
}

void HttpConn::add_response_linger()
{
    string linger;

    linger = "Connection: ";
    if (is_linger && response_code_ == OK_200)
        linger += "keep-alive";
    else
        linger += "close";
    add_to_response(linger);
    add_response_crlf();
}

void HttpConn::add_response_crlf()
{
    HttpConn::add_to_response("\r\n");
}

void HttpConn::add_response_error_content()
{
    add_to_response(RESPONSE_ERROR_CODE_TO_CONTENT.at(response_code_));
}

void HttpConn::add_to_response(std::string str)
{
    write_buffer_.insert(write_buffer_.end(), str.begin(), str.end());
}

void HttpConn::unmap_file()
{
    if(response_file_)
    {
        munmap(response_file_, response_file_stat_.st_size);
        response_file_ = 0;
    }
}

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

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}
