#ifndef __FUNCTION_IMPLEMENTATION__
#define __FUNCTION_IMPLEMENTATION__

#include <unordered_map>
#include <functional>
#include <netinet/in.h>
#include "../HttpConn/HttpRelatedDefinitions.h"
#include "json.hpp"

class FunctionImplementation {
public:
    enum FUNCTIONS {DEFAULT, COUNT_IMAGE, GET_IMAGE, POST_IMAGE};

    void set_function(enum HTTP_METHOD method, std::string url, std::string is_image_use);
    enum FUNCTIONS get_function();
    enum HTTP_CODE call(struct sockaddr_in address, std::string path_resource, nlohmann::json &request_json, nlohmann::json &response_json);
    void clear();
private:
    enum FUNCTIONS function_;
    static const std::unordered_map<FUNCTIONS
        , std::function<enum HTTP_CODE(struct sockaddr_in, std::string, nlohmann::json &, nlohmann::json &)>> enum_to_function;
};

#endif