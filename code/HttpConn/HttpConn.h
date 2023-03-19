#ifndef __HTTP_CONN__
#define __HTTP_CONN__

#include <string>
#include <vector>
#include <unordered_map>
#include <sys/stat.h>
#include "HttpRelatedDefinitions.h"
#include "../FunctionImplementation/FunctionImplementation.h"
#include "../FunctionImplementation/json.hpp"

class HttpConn {
public:
    /* 构造函数 */
    HttpConn();
    /* 初始化函数，参数分别为socket文件描述符、socket地址和资源路径 */
    void init(int epoll_fd, int socket_fd, sockaddr_in address, std::string path_resource);
    /* 关闭连接 */
    void close_conn();
    /* 从socket文件描述符读取数据并处理（如果读完） */
    void read_and_process();
    /* 从socket文件描述符读取数据，如果读完返回true */
    bool read();
    /* 处理从socket文件描述符读取数据 */
    void process();
    /* 向socket文件描述符写出数据 */
    void write();

private:
    enum CHECK_STATE {CHECK_HEADER, CHECK_REQUEST_LINE, CHECK_CONTENT};                             // HTTP报文请求头处理状态
    enum READ_STATE {READ_INCOMPLETE, READ_COMPLETE, READ_ERROR};                                   // HTTP报文处理状态
    static const std::unordered_map<enum HTTP_CODE, std::string> RESPONSE_ERROR_CODE_TO_CONTENT;    // HTTP状态响应码对应响应报文实体主体内容

    /* 清空成员变量原有值 */
    void init();
    /* 在接受完整报文后，处理报文内容 */
    enum READ_STATE process_read();
    /* 提取报文请求行和请求头，格式正确返回true */
    bool parse_line(std::string &line);
    /* 解析报文请求行，返回报文处理状态 */
    enum READ_STATE parse_request_line(std::string &line);
    /* 解析报文请求头，返回报文处理状态 */
    enum READ_STATE parse_header(std::string &line);
    /* 解析报文实体主体，返回报文处理状态 */
    enum READ_STATE parse_content();
    /* 读取报文请求文件，返回响应报文状态码 */
    enum HTTP_CODE process_file();
    /* 处理响应报文 */
    void process_write();
    /* 以下7个函数分别向响应报文添加不同内容 */
    void add_response_status_line();
    void add_response_header();
    void add_response_content_length();
    void add_response_linger();
    void add_response_crlf();
    void add_response_error_content();
    void add_to_response(std::string str);
    /* 取消请求报文请求文件的内存映射 */
    void unmap_file();
    /* 清空连接状态，为再次使用做好准备 */ 
    void clear_conn();  

    /* epoll事件表文件描述符 */
    int epoll_fd_;                                      // epoll事件表文件描述符
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
    nlohmann::json request_json_;                       // 请求content解析后的json
    nlohmann::json response_json_;                      // 响应content序列化前的json
    /* 请求报文有关的变量 */
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