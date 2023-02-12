#ifndef __HTTP_CONN__
#define __HTTP_CONN__

#include <vector>
#include <netinet/in.h>
#include <sys/uio.h>

class HttpConn {
public:
    HttpConn() {}
    // 提供connfd
    void init();
    void close();
    void read_and_process();
    void write();

private:
    enum LINE_STATUS { LINE_OK, LINE_BAD, LINE_OPEN };
    enum CHECK_STATE { CHECK_HEADER, CHECK_REQUEST_LINE, CHECK_CONTENT };
    enum READ_STATE { READ_INCOMPLETE, READ_COMPLETE, READ_ERROR };
    enum METHOD { GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };
    enum HTTP_CODE { OK_200, BAD_REQUEST_400, FORBIDDEN_403, NOT_FOUND_404
        , INTERNAL_SERVER_ERROR_500 };
    // enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR };
    /* no_request是还没读完，get_request是读完了之后该准备看文件继续，bad_request对应400 
     * no_resourse对应404，forbidden_request对应403，file_request就是正常了，是500
     * internal_error对应500
     */ 

    enum READ_STATE process_read();
    enum LINE_STATUS parse_line();
    enum READ_STATE parse_header();
    enum READ_STATE parse_request_line();
    enum READ_STATE parse_conent();
    bool process_write();
    void clear();  // 恢复line、check等等index

    int connfd_;            // socket文件描述符
    sockaddr_in address_;
    bool is_keep_alive_;
    std::vector<char> read_buffer;
    std::vector<char> write_buffer;
    int line_index_, check_index;   // 当前行起始索引、未检查起始索引
    struct iovec iv_[4];
    int iv_count_;                  // iovec结点数
    // 还得有iovec那个东西
};

#endif