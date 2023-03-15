#ifndef __HTTP_CONN__
#define __HTTP_CONN__

#include <string>
#include <vector>
#include <unordered_map>
#include <netinet/in.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include "HttpRelatedDefinitions.h"
#include "../FunctionImplementation/FunctionImplementation.h"
#include "../FunctionImplementation/json.hpp"

class HttpConn {
public:
    HttpConn();
    void init(int fd, sockaddr_in address, std::string path_resource);
    void close_conn();
    void read_and_process();
    bool read();
    void write();

    static int epoll_fd_;  // epoll事件文件描述符

private:
    enum CHECK_STATE {CHECK_HEADER, CHECK_REQUEST_LINE, CHECK_CONTENT};
    enum READ_STATE {READ_INCOMPLETE, READ_COMPLETE, READ_ERROR};
    /* 功能分别为默认的HTTP GET，获取共有多少张图片，上传图片和下载图片 */
    // enum FUNCTION {DEFAULT, COUNT_IMAGE, GET_IMAGE, POST_IMAGE};
    static const std::unordered_map<enum HTTP_CODE, std::string> RESPONSE_ERROR_CODE_TO_CONTENT;

    void init();
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

    /* 用于描述socket的变量 */
    int socket_fd_;                                     // socket文件描述符
    sockaddr_in address_;                               // 网络地址
    /* 用于进行解析的状态变量 */
    enum CHECK_STATE check_state_;                      // 请求头解析状态
    enum HTTP_CODE response_code_;                      // 响应报文状态码
    /* 请求报文有关变量 */
    enum HTTP_METHOD request_method_;                   // http请求方法
    std::string request_url_;                           // http请求URL
    enum HTTP_VERSION request_version_;                 // http请求版本
    bool is_linger_;                                    // 结束后是否断开连接
    std::string request_is_image_use_;                  // 用于判断是否是图片服务器前端发送的报文
    std::string request_host_;                          // 自host起五个字段并未进一步处理
    std::string request_user_agent_;
    std::string request_accept_;
    std::string request_accept_encoding_;
    std::string request_accept_language_;
    /* 用于存储报文的变量 */
    std::vector<char> read_buffer_;                     // 接收缓存
    std::vector<char> write_buffer_;                    // 发送缓存
    std::string request_header_;                        // 请求头，以字符串形式保存
    std::string request_content_;                       // 请求content, 以字符串形式保存
    nlohmann::json request_json_;               // 请求content解析后的json
    nlohmann::json response_json_;              // 响应content序列化前的json
    /* 报文变量有关的变量 */
    static const int DEFAULT_READ_BUFFER_SIZE = 2048;   // 读缓存默认大小
    int request_header_size_;                           // 请求头长度
    int request_content_size_;                          // 请求content长度
    std::string::iterator request_line_iterator_;       // 下一行起始位置迭代器
    /* 用于判断是否第一次调用函数的变量 */
    bool is_first_read_;                                // 是否首次调用read()，用于函数内部控制
    bool is_first_parse_line_;                          // 是否首次调用parse_line()，用于函数内部控制
    /* 响应报文有关变量 */
    FunctionImplementation function_implementation_;    // 不同接口对应的功能实现
    char * response_file_;                              // 响应报文中文件的映射内存地址
    struct stat response_file_stat_;                    // 响应报文中文件文件属性结构体
    struct iovec iv_[2];                                // iovec结构体
    int iv_count_;                                      // iovec结构体使用数
    std::string path_resource_;                         // 资源路径
};

#endif