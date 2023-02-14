#ifndef __HTTP_CONN__
#define __HTTP_CONN__

#include <string>
#include <vector>
#include <netinet/in.h>
#include <sys/uio.h>

class HttpConn {
public:
    HttpConn();
    void init(int fd, sockaddr_in address);
    void close_conn();
    void read_and_process();
    bool read();
    void write();

    static int epoll_fd_;                        // epoll事件文件描述符

private:
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    enum CHECK_STATE {CHECK_HEADER, CHECK_REQUEST_LINE, CHECK_CONTENT};
    enum READ_STATE {READ_INCOMPLETE, READ_COMPLETE, READ_ERROR};
    enum METHOD {GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH};
    enum HTTP_CODE {OK_200, BAD_REQUEST_400, FORBIDDEN_403, NOT_FOUND_404
        , INTERNAL_SERVER_ERROR_500};
    // enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR };
    /* no_request是还没读完，get_request是读完了之后该准备看文件继续，bad_request对应400 
     * no_resourse对应404，forbidden_request对应403，file_request就是正常了，是500
     * internal_error对应500
     */ 

    enum READ_STATE process_read();
    bool parse_line(std::string &line);
    enum READ_STATE parse_header(std::string &line);
    enum READ_STATE parse_request_line(std::string &line);
    enum READ_STATE parse_content();
    void process_file();
    void process_write();
    /* 恢复line、check等等index */ 
    void clear_conn();  

    int socket_fd_;                             // socket文件描述符
    sockaddr_in address_;                       // 网络地址
    bool is_keep_alive_;                        // 结束后是否断开连接
    std::vector<char> read_buffer_;             // 接收缓存
    std::vector<char> write_buffer_;            // 发送缓存
    int header_size_;                            // 请求头长度
    /* 迭代器需要在read_buffer_插入元素后初始化，避免失效 */
    std::vector<char>::iterator line_iterator_; // 下一行起始位置迭代器
    enum CHECK_STATE check_state_;              // 请求头解析状态
    enum HTTP_CODE http_code_;                  // http状态码
    struct iovec iv_[2];
    int iv_count_;                              // iovec结构体使用数
};

#endif