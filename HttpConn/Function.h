#ifndef __FUNCTION__
#define __FUNCTION__

#include "HttpConn.h"

enum HttpConn::HTTP_CODE default_file(std::string path_resource, std::string request_url, struct stat &response_file_stat, char * &response_file);
enum HttpConn::HTTP_CODE count_image(std::string path_resource, nlohmann::json &request_content_json, nlohmann::json &response_content_json);
enum HttpConn::HTTP_CODE get_image(std::string path_resource, nlohmann::json &request_content_json, nlohmann::json &response_content_json);
enum HttpConn::HTTP_CODE post_image(std::string path_resource, nlohmann::json &request_content_json, nlohmann::json &response_content_json);

#endif