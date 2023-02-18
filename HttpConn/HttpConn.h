#ifndef __HTTP_CONN__
#define __HTTP_CONN__

#include <string>
#include <vector>
#include <unordered_map>
#include <netinet/in.h>
#include <sys/uio.h>
#include <sys/stat.h>

class HttpConn {
public:
    HttpConn();
    void init(int fd, sockaddr_in address, std::string path_resource);
    // ---------------------检查一下是不是调用close也调用了umap-------------------------
    void close_conn();
    void read_and_process();
    bool read();
    void write();

    static int epoll_fd_;  // epoll事件文件描述符

private:
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    enum CHECK_STATE {CHECK_HEADER, CHECK_REQUEST_LINE, CHECK_CONTENT};
    enum READ_STATE {READ_INCOMPLETE, READ_COMPLETE, READ_ERROR};
    enum HTTP_METHOD {GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH};
    enum HTTP_VERSION {HTTP_09, HTTP_10, HTTP_11, HTTP_20};
    enum HTTP_CODE {OK_200, BAD_REQUEST_400, FORBIDDEN_403, NOT_FOUND_404
        , INTERNAL_SERVER_ERROR_500};
    static const std::unordered_map<enum HTTP_CODE, std::string> RESPONSE_ERROR_CODE_TO_CONTENT;

    /* 发送成功且继续使用连接后调用 */
    enum READ_STATE process_read();
    bool parse_line(std::string &line);
    enum READ_STATE parse_header(std::string &line);
    enum READ_STATE parse_request_line(std::string &line);
    enum READ_STATE parse_content();
    enum HTTP_CODE process_file();
    void process_write();
    void add_response_status_line();
    void add_response_header();
    void add_response_content_length();
    void add_response_linger();
    void add_response_crlf();
    void add_response_error_content();
    void add_to_response(std::string str);
    /* 取消响应报文文件的内存映射 */
    void unmap_file();
    /* 恢复line、check等等index */ 
    void clear_conn();  

    enum HTTP_METHOD request_method_;           // http请求方法
    std::string request_url_;                   // http请求URL
    enum HTTP_VERSION request_version_;         // http请求版本
    bool is_linger;                             // 结束后是否断开连接
    std::string request_host_;                  // 自host起五个字段并未进一步处理
    std::string request_user_agent_;                    
    std::string request_accept_;
    std::string request_accept_encoding;
    std::string request_accept_language;
    
    int socket_fd_;                             // socket文件描述符
    sockaddr_in address_;                       // 网络地址
    std::vector<char> read_buffer_;             // 接收缓存
    std::vector<char> write_buffer_;            // 发送缓存
    int header_size_;                           // 请求头长度
    /* 迭代器需要在read_buffer_插入元素后初始化，避免失效 */
    std::vector<char>::iterator line_iterator_; // 下一行起始位置迭代器
    enum CHECK_STATE check_state_;              // 请求头解析状态
    enum HTTP_CODE response_code_;          // 响应报文状态码
    std::string path_resource_;                 // 资源路径
    char * response_file_;                      // 响应报文中文件的映射内存地址
    struct stat response_file_stat_;            // 响应报文中文件文件属性结构体
    // int response_file_size_;                 // 响应报文中文件大小，即报文长度
    struct iovec iv_[2];                        // iovec结构体
    int iv_count_;                              // iovec结构体使用数
};

#endif