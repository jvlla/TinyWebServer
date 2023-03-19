#include <algorithm>
#include <regex>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include "HttpConn.h"
#include "../FunctionImplementation/FunctionImplementation.h"
#include "../FunctionImplementation/json.hpp"
#include "../Log/Log.h"

using namespace std;
using json = nlohmann::json;

void addfd(int epollfd, int fd);
void removefd(int epollfd, int fd);
void modfd(int epollfd, int fd, int ev);

/* static成员变量在类定义外进行初始化 */
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
    log(Log::DEBUG, "[begin] HttpConn::HttpConn()");
    socket_fd_ = -1;
    address_ = {0};
    log(Log::DEBUG, "[end  ] HttpConn::HttpConn()");
}

void HttpConn::init(int epoll_fd, int socket_fd, sockaddr_in address, std::string path_resource)
{
    log(Log::DEBUG, "[begin] HttpConn::init(int, sockaddr_in, string)");
    epoll_fd_ = epoll_fd;
    socket_fd_ = socket_fd;
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
    log(Log::DEBUG, "[end  ] HttpConn::init(int, sockaddr_in, string)");
}

/* 除在上一init()函数中传递变量，其余变量全部赋初值 */
void HttpConn::init()
{
    log(Log::DEBUG, "[begin] HttpConn::init()");
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
    log(Log::DEBUG, "[end  ] HttpConn::init()");
}

void HttpConn::close_conn()
{
    log(Log::DEBUG, "[begin] HttpConn::close_conn()");
    if (socket_fd_ != -1)
    {
        removefd(epoll_fd_, socket_fd_);
        socket_fd_ = -1;
    }
    log(Log::DEBUG, "[end  ] HttpConn::close_conn()");
}

void HttpConn::clear_conn()
{
    log(Log::DEBUG, "[begin] HttpConn::clear_conn()");
    init();
    log(Log::DEBUG, "[end  ] HttpConn::clear_conn()");
}

/* 首先先读取一部分，然后靠查找\r\n\r\n找到content开始的部分，拷贝到打开的mmap地址。
 * 靠查找"Content-Length"确定长度，直到把内容读完才可以返回true，搞定
 */
bool HttpConn::read()
{
    log(Log::DEBUG, "[begin] HttpConn::read()");
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
    while ((received = recv(socket_fd_, (char *) read_buffer_.data() + bytes_received
        , read_buffer_.size() - bytes_received, 0)) > 0)
    {
        bytes_received += received;
        log(Log::INFO, "received " + to_string(received) + " bytes, have got " + to_string(bytes_received) + " bytes");
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
                log(Log::INFO, "found \\r\\n\\r\\n");
                header_end_iterator += 2;  // +2是为了在最后保留一组\r\n
                /* 设置请求头大小 */
                request_header_size_ = header_end_iterator - read_buffer_.begin();
                request_header_.insert(request_header_.begin(), read_buffer_.begin(), header_end_iterator);

                regex pattern("Content-Length: (\\d+)\r\n");
                smatch result;
                if (regex_search(request_header_, result, pattern))
                {
                    request_content_size_ = atoi(string(result[1]).data());
                    log(Log::INFO, "content size: " + to_string(request_content_size_));
                }
                /* 当报文中存在\r\n\r\n但不存在Content-Length请求头时，
                 * 这时有两种情况，当\r\n\r\n为结尾时，不存在content，否则报文错误
                 */
                else
                {
                    log(Log::INFO, "no content lenght");
                    /* 如果\r\n\r\n是报文末尾，说明是普通get报文，read()结束，返回true */
                    if (header_end_iterator - read_buffer_.begin() + 2 == static_cast<long>(bytes_received))
                    {
                        log(Log::INFO, "but Ok, basic GET request");
                        request_content_size_ = 0;
                        log(Log::DEBUG, "[end  ] HttpConn::read()");
                        return true;
                    }
                    /* 否则报文有问题，简单起见直接关闭连接 */
                    else
                    {
                        log(Log::INFO, "bad request, just close connection for simplicity");
                        close_conn();
                        log(Log::DEBUG, "[end  ] HttpConn::read()");
                        return false;
                    }
                }
                received_all_header = true;
            }
        }
        /* 这里不能用else，因为received_all_header可能改变 */
        if (received_all_header)
        {
            log(Log::INFO, "has got header");
            if (bytes_received == static_cast<unsigned long>(request_header_size_ + request_content_size_ + 2))
            {
                log(Log::INFO, "got enough content");
                request_content_.insert(request_content_.begin(), read_buffer_.begin() + request_header_size_ + 2
                    , read_buffer_.begin() + request_header_size_ + request_content_size_ + 2);
                log(Log::DEBUG, "[end  ] HttpConn::read()");
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
        log(Log::DEBUG, "[end  ] HttpConn::read()");
        return false;
    }
    /* 其余全部情况关闭连接 */
    else
    {
        close_conn();
        log(Log::DEBUG, "[end  ] HttpConn::read()");
        return false;
    }
}

void HttpConn::write()
{
    log(Log::DEBUG, "[begin] HttpConn::write()");
    log(Log::DEBUG, "write buffer: \n" + string(write_buffer_.begin(), write_buffer_.end()));
    log(Log::INFO, "have " + to_string(iv_count_) + " iovec struct(s), size " + to_string(iv_[0].iov_len) + " " + to_string(iv_[1].iov_len));

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
        log(Log::INFO, "sent " + to_string(bytes_sended) + " bytes");
        if (bytes_sended <= -1)
        {
            log(Log::WARN, "errno: " + to_string(errno));
            /* 如果错误为EAGAIN，重复设置EPOLLOUT事件等待触发 */
            if (errno == EAGAIN)
            {
                log(Log::INFO, "EAGAIN, continue write");
                modfd(epoll_fd_, socket_fd_, EPOLLOUT);
            }
            /* 否则说明出错，关闭连接 */
            else
            {
                log(Log::ERROR, "close connection");
                unmap_file();
                close_conn();
            }

            log(Log::DEBUG, "[end  ] HttpConn::write()");
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
        log(Log::INFO, "after send, iv_ size: " + to_string(iv_[0].iov_len) + " " + to_string(iv_[1].iov_len));
        /* 如果发送完全 */
        if (iv_[0].iov_len == 0 && iv_[1].iov_len == 0)
        {
            log(Log::INFO, "send complete");
            unmap_file();
            if (is_linger_)
            {
                log(Log::INFO, "is linger, reset EPOLLIN");
                modfd(epoll_fd_, socket_fd_, EPOLLIN);
                clear_conn();
            }
            else
                close_conn();
            
            log(Log::DEBUG, "[end  ] HttpConn::write()");
            return;
        }
    }
}

void HttpConn::read_and_process()
{
    log(Log::DEBUG, "[begin] HttpConn::read_and_process()");
    if (read())
        process();
    log(Log::DEBUG, "[end  ] HttpConn::read_and_process()");
}

void HttpConn::process()
{
    log(Log::DEBUG, "[begin] HttpConn::process()");
    /* 如果未完全读入请求报文，等待下次RPOLLIN事件，继续调用read()函数 */
    
    log(Log::INFO, "header size: " + to_string(request_header_size_) + " " + to_string(request_header_.size()));
    log(Log::INFO, "content size: " + to_string(request_content_size_) + " " + to_string(request_content_.size()));
    log(Log::DEBUG, "content: \n" + request_content_);
    
    switch (process_read())
    {
        case READ_COMPLETE:
            log(Log::INFO, "request processing complete");
            iv_count_ = 2;
            log(Log::INFO, "before function, response code: " + to_string(response_code_));
            // 注意判断，因为FunctionImplementation里没给DEFAULT定义函数，所以调用的话会出out_of_range异常
            if (function_implementation_.get_function() == FunctionImplementation::DEFAULT)
                response_code_ = process_file();
            else
                response_code_ = function_implementation_.call(address_, path_resource_, request_json_, response_json_);
            log(Log::INFO, "after function, response code:  " + to_string(response_code_) + " (0 to 4 correspond 200, 400, 403, 404, 500)");
            if (response_code_ == OK_200)
                log(Log::DEBUG, "response json: " + response_json_.dump());
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
    log(Log::DEBUG, "HttpConn::process()");
}

enum HttpConn::READ_STATE HttpConn::process_read()
{
    log(Log::DEBUG, "HttpConn::process_read()");
    READ_STATE read_state = READ_INCOMPLETE;

    while (read_state == READ_INCOMPLETE)
    {
        std::string line;

        /* 当非CHECK_CONTENT时，解析下一行，调用parse_line获得请求头中一行.
         * 如果该行出错，退出循环，设置http状态码400 
         */
        if(check_state_ != CHECK_CONTENT && !parse_line(line))
        {
            read_state = READ_ERROR;
            response_code_ = BAD_REQUEST_400;
            log(Log::INFO, "can't parse next line");
            break;  // 跳出while，避免对出错行调用parse_*函数
        }
        if (check_state_ != CHECK_CONTENT)
            log(Log::DEBUG, "line:  " + line);

        /* check_state_状态转移在各个parse_*函数中完成 */ 
        switch (check_state_)
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

    log(Log::DEBUG, "[end  ] HttpConn::process_read()");
    return read_state;
}

bool HttpConn::parse_line(std::string &line)
{
    log(Log::DEBUG, "[begin] HttpConn::parse_line(string &)");
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
        log(Log::DEBUG, "[end  ] HttpConn::parse_line(string &)");
        return false;
    }
    else
    {
        line.assign(request_line_iterator_, end);
        request_line_iterator_ = end + 2;
        log(Log::DEBUG, "[end  ] HttpConn::parse_line(string &)");
        return true;
    }
}

enum HttpConn::READ_STATE HttpConn::parse_request_line(std::string &line)
{
    log(Log::DEBUG, "[begin] HttpConn::parse_request_line(string &)");
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
        log(Log::DEBUG, "[end  ] HttpConn::parse_request_line(string &)");
        return READ_INCOMPLETE;  // READ_STATE状态转移
    }
    else
    {
        response_code_ = BAD_REQUEST_400;
        log(Log::DEBUG, "[end  ] HttpConn::parse_request_line(string &)");
        return READ_ERROR;  // READ_STATE状态转移
    }
}

enum HttpConn::READ_STATE HttpConn::parse_header(std::string &line)
{
    log(Log::DEBUG, "[begin] HttpConn::parse_header(string &)");
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
            log(Log::INFO, "unknown header:  " + line);
    }
    else 
    {
        log(Log::INFO, "illegal line:  " + line);
        is_legal = false;
    }

    /* request_line_iterator_超过request_header_尾后迭代器说明已经处理完全部请求头，可以开始处理content */
    if (request_line_iterator_ >= request_header_.end())
    {
        log(Log::INFO, "change check_state_ to CHECK_CONTENT");
        check_state_ = CHECK_CONTENT;  // CHECK_STATE状态转移
    }
    
    /* 当请求头不合法时照常处理，不合法时返回READ_ERROR设置 */
    if (is_legal)
    {
        log(Log::DEBUG, "[end  ] HttpConn::parse_header(string &)");
        return READ_INCOMPLETE;  // READ_STATE状态转移
    }
    else
    {
        response_code_ = BAD_REQUEST_400;
        log(Log::DEBUG, "[end  ] HttpConn::parse_header(string &)");
        return READ_ERROR;  // READ_STATE状态转移
    }
}

enum HttpConn::READ_STATE HttpConn::parse_content()
{
    log(Log::DEBUG, "[begin] HttpConn::parse_content()");
    if (function_implementation_.get_function() != FunctionImplementation::DEFAULT && request_content_size_ != 0)
        try {
            request_json_ = json::parse(request_content_);
        } catch(...) {
            response_code_ = BAD_REQUEST_400;
            log(Log::DEBUG, "[end  ] HttpConn::parse_content()");
            return READ_ERROR;
        }
    
    log(Log::DEBUG, "[end  ] HttpConn::parse_content()");
    return READ_COMPLETE;
}

enum HTTP_CODE HttpConn::process_file()
{
    log(Log::DEBUG, "[begin] HttpConn::process_file()");
    if (response_code_ != OK_200)
    {
        log(Log::DEBUG, "[end  ] HttpConn::process_file()");
        return response_code_;
    }
    
    string path_file;
    path_file =  path_resource_ + request_url_;
    log(Log::INFO, "name of the file to open: " + path_file);

    /* 处理错误情况 */
    if (stat(path_file.data(), &response_file_stat_) < 0)
    {
        log(Log::DEBUG, "[end  ] HttpConn::process_file()");
        return NOT_FOUND_404;
    }
    if (!(response_file_stat_.st_mode & S_IROTH))
    {
        log(Log::DEBUG, "[end  ] HttpConn::process_file()");
        return FORBIDDEN_403;
    }
    if (S_ISDIR(response_file_stat_.st_mode))
    {
        log(Log::DEBUG, "[end  ] HttpConn::process_file()");
        return BAD_REQUEST_400;
    }

    /* 进行文件映射 */
    int fd = open(path_file.data(), O_RDONLY);
    response_file_ = (char *) mmap(0, response_file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    log(Log::DEBUG, "[end  ] HttpConn::process_file()");
    return OK_200;
}

void HttpConn::process_write()
{
    log(Log::DEBUG, "[begin] HttpConn::process_write()");

    add_response_status_line();
    add_response_header();
    if (function_implementation_.get_function() != FunctionImplementation::DEFAULT)
        add_to_response(response_json_.dump());
    
    iv_[0].iov_base = write_buffer_.data(); 
    iv_[0].iov_len = write_buffer_.size();
    if (response_code_ == OK_200)
    {
        log(Log::INFO, "size of the file that will be send: " + to_string(response_file_stat_.st_size));
        iv_[1].iov_base = response_file_; 
        iv_[1].iov_len = response_file_stat_.st_size;
    }

    log(Log::DEBUG, "[end  ] HttpConn::process_write()");
}

void HttpConn::add_response_status_line()
{
    log(Log::DEBUG, "[begin] HttpConn::add_response_status_line()");
    string status;

    status += "HTTP/1.1 ";  // 默认使用htpp 1.1
    switch (response_code_)
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
    log(Log::DEBUG, "[end  ] HttpConn::add_response_status_line()");
}

void HttpConn::add_response_header()
{
    log(Log::DEBUG, "[begin] HttpConn::add_response_header()");
    add_response_content_length();
    add_response_linger();
    add_response_crlf();
    if (response_code_ != OK_200)
        add_response_error_content();
    log(Log::DEBUG, "[end  ] HttpConn::add_response_header()");
}

void HttpConn::add_response_content_length()
{
    log(Log::DEBUG, "[begin] HttpConn::add_response_content_length()");
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

    log(Log::INFO, "response content length: " + content_length);
    log(Log::DEBUG, "[end  ] HttpConn::add_response_content_length()");
}

void HttpConn::add_response_linger()
{
    log(Log::DEBUG, "[begin] HttpConn::add_response_linger()");
    string linger;

    linger = "Connection: ";
    if (is_linger_ && response_code_ == OK_200)
        linger += "keep-alive";
    else
        linger += "close";
    add_to_response(linger);
    add_response_crlf();
    log(Log::DEBUG, "[end  ] HttpConn::add_response_linger()");
}

void HttpConn::add_response_crlf()
{
    log(Log::DEBUG, "[begin] HttpConn::add_response_crlf()");
    HttpConn::add_to_response("\r\n");
    log(Log::DEBUG, "[end  ] HttpConn::add_response_crlf()");
}

void HttpConn::add_response_error_content()
{
    log(Log::DEBUG, "[begin] HttpConn::add_response_error_content()");
    add_to_response(RESPONSE_ERROR_CODE_TO_CONTENT.at(response_code_));
    log(Log::DEBUG, "[end  ] HttpConn::add_response_error_content()");
}

void HttpConn::add_to_response(std::string str)
{
    log(Log::DEBUG, "[begin] HttpConn::add_to_response(string)");
    write_buffer_.insert(write_buffer_.end(), str.begin(), str.end());
    log(Log::DEBUG, "[end  ] HttpConn::add_to_response(string)");
}

void HttpConn::unmap_file()
{
    log(Log::DEBUG, "[begin] HttpConn::unmap_file()");
    if(response_file_)
    {
        munmap(response_file_, response_file_stat_.st_size);
        response_file_ = 0;
    }
    log(Log::DEBUG, "[end  ] HttpConn::unmap_file()");
}

int setnonblocking(int fd)
{
    log(Log::DEBUG, "[begin] setnonblocking(int)");
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);

    log(Log::DEBUG, "[end  ] setnonblocking(int)");
    return old_option;
}

void addfd(int epollfd, int fd)
{
    log(Log::DEBUG, "[begin] addfd(int, int)");
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
    log(Log::DEBUG, "[end  ] addfd(int, int)");
}

void removefd(int epollfd, int fd)
{
    log(Log::DEBUG, "[begin] removefd(int, int)");
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
    log(Log::DEBUG, "[end  ] removefd(int, int)");
}

void modfd(int epollfd, int fd, int ev)
{
    log(Log::DEBUG, "[begin] modfd(int, int, int)");
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
    log(Log::DEBUG, "[end  ] modfd(int, int, int)");
}
