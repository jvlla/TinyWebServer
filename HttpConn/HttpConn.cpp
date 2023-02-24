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
#include "../FunctionImplementation/FunctionImplementation.h"
#include "../FunctionImplementation/json.hpp"

using namespace std;
using json = nlohmann::json;

enum indent {BEGIN, END, MID};
extern void debug(string str, enum indent indent_change = MID);
void addfd(int epollfd, int fd);
void removefd(int epollfd, int fd);
void modfd(int epollfd, int fd, int ev);

/* static成员变量在类定义外进行初始化 */
int HttpConn::epoll_fd_ = -1;
const unordered_map<enum HTTP_CODE, string> HttpConn::RESPONSE_ERROR_CODE_TO_CONTENT = {
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
    debug("HttpConn::HttpConn()", BEGIN);
    socket_fd_ = -1;
    address_ = {0};
    debug("HttpConn::HttpConn()", END);
}

void HttpConn::init(int fd, sockaddr_in address, string path_resource)
{
    debug("HttpConn::init(int, sockaddr_in, string)", BEGIN);
    socket_fd_ = fd;
    address_ = address;
    path_resource_ = path_resource;
    /* 允许端口复用 */
    int error = 0;
    socklen_t len = sizeof( error );
    getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &error, &len);
    int reuse = 1;
    setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    /* 将socket文件描述符加入epoll事件表 */
    addfd(epoll_fd_, socket_fd_);
    init();
    debug("HttpConn::init(int, sockaddr_in, string)", END);
}

/* 除在上一init()函数中传递变量，其余变量全部赋初值 */
void HttpConn::init()
{
    debug("HttpConn::init()", BEGIN);
    check_state_ = CHECK_REQUEST_LINE;
    response_code_ = OK_200;
    request_method_ = GET;
    request_url_ = "";
    request_version_ = HTTP_11;
    is_linger_ = false;
    request_is_image_use_ = "";
    request_host_ = "";
    request_user_agent_ = "";
    request_accept_ = "";
    request_accept_encoding_ = "";
    request_accept_language_ = "";
    /* 清空缓存中内容 */
    vector<char>().swap(read_buffer_);
    vector<char>().swap(write_buffer_);
    read_buffer_.resize(DEFAULT_READ_BUFFER_SIZE);
    request_header_ = "";
    request_content_ = "";
    request_json_ = json::parse("{}");
    response_json_ = json::parse("{}");
    request_header_size_ = 0;
    request_content_size_ = 0;
    request_line_iterator_ = request_header_.end();
    is_first_read_ = true;
    is_first_parse_line_ = true;
    function_implementation_.clear();
    response_file_ = nullptr;
    /* 结构体置0 */
    memset(&response_file_stat_, 0, sizeof(response_file_stat_));
    memset(&iv_, 0, sizeof(iv_));
    iv_count_ = 1;
    debug("HttpConn::init()", END);
}

void HttpConn::close_conn()
{
    debug("HttpConn::close_conn()", BEGIN);
    if (socket_fd_ != -1)
    {
        removefd(epoll_fd_, socket_fd_);
        socket_fd_ = -1;
    }
    debug("HttpConn::close_conn()", END);
}

void HttpConn::clear_conn()
{
    debug("HttpConn::clear_conn()", BEGIN);
    init();
    debug("HttpConn::clear_conn()", END);
}

/* 首先先读取一部分，然后靠查找\r\n\r\n找到content开始的部分，拷贝到打开的mmap地址。
 * 靠查找"Content-Length"确定长度，直到把内容读完才可以返回true，搞定
 */
bool HttpConn::read()
{
    debug("HttpConn::read()", BEGIN);
    /* 变量均为静态类型，保证多次调用不改变 */
    static unsigned long bytes_received;
    static bool received_all_header;
    int received;

    if (is_first_read_)
    {
        bytes_received = 0;
        received_all_header = false;
        is_first_read_ = false;
    }
    while((received = recv(socket_fd_, (char *) read_buffer_.data() + bytes_received
        , read_buffer_.size() - bytes_received, 0)) > 0)
    {
        bytes_received += received;
        debug("received " + to_string(received) + " bytes, have got " + to_string(bytes_received) + " bytes");
        /* 缓冲区以2倍增大 */ 
        if (bytes_received == read_buffer_.size())
            read_buffer_.resize(2 * read_buffer_.size());
        if (!received_all_header)
        {
            string header_end = "\r\n\r\n";
            vector<char>::iterator header_end_iterator = search(read_buffer_.begin(), read_buffer_.end()
                , header_end.begin(), header_end.end());
            /* 如果找到，即已获取全部请求头 */
            if (header_end_iterator != read_buffer_.end())
            {
                debug("found \\r\\n\\r\\n");
                header_end_iterator += 2;  // +2是为了在最后保留一组\r\n
                /* 设置请求头大小 */
                request_header_size_ = header_end_iterator - read_buffer_.begin();
                request_header_.insert(request_header_.begin(), read_buffer_.begin(), header_end_iterator);

                regex pattern("Content-Length: (\\d+)\r\n");
                smatch result;
                if (regex_search(request_header_, result, pattern))
                {
                    request_content_size_ = atoi(string(result[1]).data());
                    debug("content size: " + to_string(request_content_size_));
                }
                /* 当报文中存在\r\n\r\n但不存在Content-Length请求头时，
                 * 这时有两种情况，当\r\n\r\n为结尾时，不存在content，否则报文错误
                 */
                else
                {
                    debug("no content lenght");
                    /* 如果\r\n\r\n是报文末尾，说明是普通get报文，read()结束，返回true */
                    if (header_end_iterator - read_buffer_.begin() + 2 == static_cast<long>(bytes_received))
                    {
                        debug("but Ok, basic GET request");
                        request_content_size_ = 0;
                        debug("HttpConn::read()", END);
                        return true;
                    }
                    /* 否则报文有问题，简单起见直接关闭连接 */
                    else
                    {
                        debug("bad request, just close connection for simplicity");
                        close_conn();
                        debug("HttpConn::read()", END);
                        return false;
                    }
                }
                received_all_header = true;
            }
        }
        /* 这里不能用else，因为received_all_header可能改变 */
        if (received_all_header)
        {
            debug("has got header");
            if (bytes_received == static_cast<unsigned long>(request_header_size_ + request_content_size_ + 2))
            {
                debug("got enough content");
                request_content_.insert(request_content_.begin(), read_buffer_.begin() + request_header_size_ + 2
                    , read_buffer_.begin() + request_header_size_ + request_content_size_ + 2);
                debug("HttpConn::read()", END);
                return true;
            }
        }
    }
    /* 进行recv()返回值的错误处理 */ 
    /* 只有errno为EAGAIN和EWOULDBLOCK的情况可以继续接收报文 */ 
    if (received == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
        /* 设置继续监听epollin事件以接收报文 */
        modfd(epoll_fd_, socket_fd_, EPOLLIN);
        debug("HttpConn::read()", END);
        return false;
    }
    /* 其余全部情况关闭连接 */
    else
    {
        close_conn();
        debug("HttpConn::read()", END);
        return false;
    }
}

void HttpConn::write()
{
    debug("HttpConn::write()", BEGIN);
    debug("write buffer: ");
    debug(string(write_buffer_.begin(), write_buffer_.end()));
    debug("have " + to_string(iv_count_) + " iovec struct(s), size " + to_string(iv_[0].iov_len) + " " + to_string(iv_[1].iov_len));

    while (true)
    {
        int old_iv1_len = 0;
        /* 限制单次发送大小以缓解浏览器ERR_CONTENT_LENGTH_MISMATCH报错 */
        if (iv_[1].iov_len > 16384)
        {
            old_iv1_len = iv_[1].iov_len;
            iv_[1].iov_len = 16384;
        }   
        int bytes_sended = writev(socket_fd_, iv_, iv_count_);
        debug("sent " + to_string(bytes_sended) + " bytes");
        if (bytes_sended <= -1)
        {
            debug("errno: " + to_string(errno));
            /* 如果错误为EAGAIN，重复设置EPOLLOUT事件等待触发 */
            if (errno == EAGAIN)
            {
                debug("EAGAIN, continue write");
                modfd(epoll_fd_, socket_fd_, EPOLLOUT);
            }
            /* 否则说明出错，关闭连接 */
            else
            {
                debug("close connection");
                unmap_file();
                close_conn();
            }

            debug("HttpConn::write()", END);
            return;
        }

        /* 暂停0.1s以暂时解决浏览器ERR_CONTENT_LENGTH_MISMATCH报错。这错误好像和缓冲有关系，但反正只在实际部署也就是网速慢得情况下出问题 */
        usleep(100000);
        if (old_iv1_len != 0)
            iv_[1].iov_len = old_iv1_len;
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
        debug("after send, iv_ size: " + to_string(iv_[0].iov_len) + " " + to_string(iv_[1].iov_len));
        /* 如果发送完全 */
        if (iv_[0].iov_len == 0 && iv_[1].iov_len == 0)
        {
            debug("send complete");
            unmap_file();
            if (is_linger_)
            {
                debug("is linger, reset EPOLLIN");
                modfd(epoll_fd_, socket_fd_, EPOLLIN);
                clear_conn();
            }
            else
                close_conn();
            
            debug("HttpConn::write()", END);
            return;
        }
    }
}

void HttpConn::read_and_process()
{
    debug("HttpConn::read_and_process()", BEGIN);
    /* 如果未完全读入请求报文，等待下次RPOLLIN事件，继续调用read()函数 */
    if (!read())
    {
        debug("HttpConn::read_and_process()", END);
        return;
    }
    debug("header size: " + to_string(request_header_size_) + " " + to_string(request_header_.size()));
    debug("content size: " + to_string(request_content_size_) + " " + to_string(request_content_.size()));
    debug(request_content_);
    
    switch(process_read())
    {
        case READ_COMPLETE:
            debug("request processing complete");
            iv_count_ = 2;
            debug("before function, response code: " + to_string(response_code_));
            // 注意判断，因为FunctionImplementation里没给DEFAULT定义函数，所以调用的话会出out_of_range异常
            if (function_implementation_.get_function() == FunctionImplementation::DEFAULT)
                response_code_ = process_file();
            else
                response_code_ = function_implementation_.call(address_, path_resource_, request_json_, response_json_);
            debug("after function, response code:  " + to_string(response_code_) + " (0 to 4 correspond 200, 400, 403, 404, 500)");
            if (response_code_ == OK_200)
                debug("response json: " + response_json_.dump());
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
    /* 设置EPOLLOUT事件，等待发送响应报文 */
    modfd(epoll_fd_, socket_fd_, EPOLLOUT);
    debug("HttpConn::read_and_process()", END);
}

enum HttpConn::READ_STATE HttpConn::process_read()
{
    debug("HttpConn::process_read()", BEGIN);
    READ_STATE read_state = READ_INCOMPLETE;

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
            debug("can't parse next line");
            break;  // 跳出while，避免对出错行调用parse_*函数
        }
        if (check_state_ != CHECK_CONTENT)
            debug("line:  " + line);

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
                function_implementation_.set_function(request_method_, request_url_, request_is_image_use_);
                if (function_implementation_.get_function() == FunctionImplementation::DEFAULT && request_url_ == "/")
                    request_url_ = "/welcome.html";
                read_state = parse_content();
                break;
            /* check_state_状态出错，设置http状态码500 */
            default:
                read_state = READ_ERROR;
                response_code_ = INTERNAL_SERVER_ERROR_500;
        }
    }

    debug("HttpConn::process_read()", END);
    return read_state;
}

bool HttpConn::parse_line(std::string &line)
{
    debug("HttpConn::parse_line(string &)", BEGIN);
    if (is_first_parse_line_)
    {
        request_line_iterator_ = request_header_.begin();
        is_first_parse_line_ = false;
    }
    string::iterator end;

    end = find(request_line_iterator_, request_header_.end(), '\r');
    /* 如果"r"后不存在请求头或"\r\n"不连续，报文错误 */
    if (request_line_iterator_ >= request_header_.end() || *(end + 1) != '\n')
    {
        debug("HttpConn::parse_line(string &)", END);
        return false;
    }
    else
    {
        line.assign(request_line_iterator_, end);
        request_line_iterator_ = end + 2;
        debug("HttpConn::parse_line(string &)", END);
        return true;
    }
}

enum HttpConn::READ_STATE HttpConn::parse_request_line(std::string &line)
{
    debug("HttpConn::parse_request_line(string &)", BEGIN);
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

    check_state_ = CHECK_HEADER;  // check_state_状态转移
    /* 当请求行合法时，继续处理，否则停止处理，设置400错误 */
    if (is_legal)
    {
        debug("HttpConn::parse_request_line(string &)", END);
        return READ_INCOMPLETE;  // READ_STATE状态转移
    }
    else
    {
        response_code_ = BAD_REQUEST_400;
        debug("HttpConn::parse_request_line(string &)", END);
        return READ_ERROR;  // READ_STATE状态转移
    }
}

enum HttpConn::READ_STATE HttpConn::parse_header(std::string &line)
{
    debug("HttpConn::parse_header(string &)", BEGIN);
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
                is_linger_ = true;
            else if (value == "close")
                is_linger_ = false;
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
            request_accept_encoding_ = value;
        else if (field == "Accept-Language")
            request_accept_language_ = value;
        else if (field == "Is-Image-Use")
            request_is_image_use_ = value;
        else
            debug("unknown header:  " + line);
    }
    else 
    {
        debug("illegal line:  " + line);
        is_legal = false;
    }

    /* request_line_iterator_超过request_header_尾后迭代器说明已经处理完全部请求头，可以开始处理content */
    if (request_line_iterator_ >= request_header_.end())
    {
        debug("chang check_state_ to CHECK_CONTENT");
        check_state_ = CHECK_CONTENT;  // CHECK_STATE状态转移
    }
    
    /* 当请求头不合法时照常处理，不合法时返回READ_ERROR设置 */
    if (is_legal)
    {
        debug("HttpConn::parse_header(string &)", END);
        return READ_INCOMPLETE;  // READ_STATE状态转移
    }
    else
    {
        response_code_ = BAD_REQUEST_400;
        debug("HttpConn::parse_header(string &)", END);
        return READ_ERROR;  // READ_STATE状态转移
    }
}

enum HttpConn::READ_STATE HttpConn::parse_content()
{
    debug("HttpConn::parse_content()", BEGIN);
    if (function_implementation_.get_function() != FunctionImplementation::DEFAULT && request_content_size_ != 0)
        try {
            request_json_ = json::parse(request_content_);
        } catch(...) {
            response_code_ = BAD_REQUEST_400;
            debug("HttpConn::parse_content()", END);
            return READ_ERROR;
        }
    
    debug("HttpConn::parse_content()", END);
    return READ_COMPLETE;
}

enum HTTP_CODE HttpConn::process_file()
{
    debug("HttpConn::process_file()", BEGIN);
    if (response_code_ != OK_200)
    {
        debug("HttpConn::process_file()", END);
        return response_code_;
    }
    
    string path_file;
    path_file =  path_resource_ + request_url_;
    debug("file name: " + path_file);

    /* 处理错误情况 */
    if (stat(path_file.data(), &response_file_stat_) < 0)
    {
        debug("HttpConn::process_file()", END);
        return NOT_FOUND_404;
    }
    if (!(response_file_stat_.st_mode & S_IROTH))
    {
        debug("HttpConn::process_file()", END);
        return FORBIDDEN_403;
    }
    if (S_ISDIR(response_file_stat_.st_mode))
    {
        debug("HttpConn::process_file()", END);
        return BAD_REQUEST_400;
    }

    /* 进行文件映射 */
    int fd = open(path_file.data(), O_RDONLY);
    response_file_ = (char *) mmap(0, response_file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    debug("HttpConn::process_file()", END);
    return OK_200;
}

void HttpConn::process_write()
{
    debug("HttpConn::process_write()", BEGIN);

    add_response_status_line();
    add_response_header();
    if (function_implementation_.get_function() != FunctionImplementation::DEFAULT)
        add_to_response(response_json_.dump());
    
    iv_[0].iov_base = write_buffer_.data(); 
    iv_[0].iov_len = write_buffer_.size();
    if (response_code_ == OK_200)
    {
        debug("size of the file that will be send: " + response_file_stat_.st_size);
        iv_[1].iov_base = response_file_; 
        iv_[1].iov_len = response_file_stat_.st_size;
    }

    debug("HttpConn::process_write()", END);
}

void HttpConn::add_response_status_line()
{
    debug("HttpConn::add_response_status_line()", BEGIN);
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
    debug("HttpConn::add_response_status_line()", END);
}

void HttpConn::add_response_header()
{
    debug("HttpConn::add_response_header()", BEGIN);
    add_response_content_length();
    add_response_linger();
    add_response_crlf();
    if (response_code_ != OK_200)
        add_response_error_content();
    debug("HttpConn::add_response_header()", END);
}

void HttpConn::add_response_content_length()
{
    debug("HttpConn::add_response_content_length()", BEGIN);
    string content_length = "Content-Length: ";

    if (response_code_ == OK_200)
    {
        if (function_implementation_.get_function() == FunctionImplementation::DEFAULT)
            content_length += to_string(response_file_stat_.st_size);
        else
            content_length += to_string(response_json_.dump().size());
    }   
    else
        content_length += to_string(RESPONSE_ERROR_CODE_TO_CONTENT.at(response_code_).size());
    add_to_response(content_length);
    add_response_crlf();

    debug("response " + content_length);
    debug("HttpConn::add_response_content_length()", END);
}

void HttpConn::add_response_linger()
{
    debug("HttpConn::add_response_linger()", BEGIN);
    string linger;

    linger = "Connection: ";
    if (is_linger_ && response_code_ == OK_200)
        linger += "keep-alive";
    else
        linger += "close";
    add_to_response(linger);
    add_response_crlf();
    debug("HttpConn::add_response_linger()", END);
}

void HttpConn::add_response_crlf()
{
    debug("HttpConn::add_response_crlf()", BEGIN);
    HttpConn::add_to_response("\r\n");
    debug("HttpConn::add_response_crlf()", END);
}

void HttpConn::add_response_error_content()
{
    debug("HttpConn::add_response_error_content()", BEGIN);
    add_to_response(RESPONSE_ERROR_CODE_TO_CONTENT.at(response_code_));
    debug("HttpConn::add_response_error_content()", END);
}

void HttpConn::add_to_response(std::string str)
{
    debug("HttpConn::add_to_response(string)", BEGIN);
    write_buffer_.insert(write_buffer_.end(), str.begin(), str.end());
    debug("HttpConn::add_to_response(string)", END);
}

void HttpConn::unmap_file()
{
    debug("HttpConn::unmap_file()", BEGIN);
    if(response_file_)
    {
        munmap(response_file_, response_file_stat_.st_size);
        response_file_ = 0;
    }
    debug("HttpConn::unmap_file()", END);
}

int setnonblocking(int fd)
{
    debug("setnonblocking(int)", BEGIN);
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);

    debug("setnonblocking(int)", END);
    return old_option;
}

void addfd(int epollfd, int fd)
{
    debug("addfd(int, int)", BEGIN);
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
    debug("addfd(int, int)", END);
}

void removefd(int epollfd, int fd)
{
    debug("removefd(int, int)", BEGIN);
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
    debug("removefd(int, int)", END);
}

void modfd(int epollfd, int fd, int ev)
{
    debug("modfd(int, int, int)", BEGIN);
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
    debug("modfd(int, int, int)", END);
}
