#ifndef __FUNCTION_IMPLEMENTATION__
#define __FUNCTION_IMPLEMENTATION__

#include <unordered_map>
#include <functional>
#include <netinet/in.h>
#include "../HttpConn/HttpRelatedDefinitions.h"
#include "json.hpp"

class FunctionImplementation {
public:
    /* 支持功能，DEFAULT为默认请求文件，由服务器处理 */
    enum FUNCTIONS {DEFAULT, COUNT_IMAGE, GET_IMAGE, POST_IMAGE};

    /* 根据参数HTTP请求方法、URL和请求头字段设置功能 */
    void set_function(enum HTTP_METHOD method, std::string url, std::string is_image_use);
    /* 返回当前功能 */
    enum FUNCTIONS get_function();
    /* 调用具体功能实现函数，参数为IP地址、资源路径、请求和响应报文实体主体中的JSON数据 */
    enum HTTP_CODE call(struct sockaddr_in address, std::string path_resource, nlohmann::json &request_json, nlohmann::json &response_json);
    /* 功能置为DEFAULT */
    void clear();
private:
    enum FUNCTIONS function_;
    static const std::unordered_map<FUNCTIONS
        , std::function<enum HTTP_CODE(struct sockaddr_in, std::string, nlohmann::json &, nlohmann::json &)>> enum_to_function_;
};

#endif