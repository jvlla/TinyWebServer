#include <iostream>
#include <stdio.h>

#include <fstream>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/mman.h>
#include <dirent.h>
#include "FunctionImplementation.h"
#include "json.hpp"
#include "base64.h"

using namespace std;
using json = nlohmann::json;

enum indent {BEGIN, END, MID};
extern void debug(string str, enum indent indent_change = MID);
enum HTTP_CODE count_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json);
enum HTTP_CODE get_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json);
enum HTTP_CODE post_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json);
int get_folder_file_count(string folder_path);

const unordered_map<FunctionImplementation::FUNCTIONS
    , function<enum HTTP_CODE(struct sockaddr_in, string, json &, json &)>> FunctionImplementation::enum_to_function = {
    {COUNT_IMAGE, count_image},
    {GET_IMAGE, get_image},
    {POST_IMAGE, post_image}
};

void FunctionImplementation::set_function(enum HTTP_METHOD method, std::string url, string is_image_use)
{
    if (method == GET && url == "/COUNT_IMAGE" && is_image_use == "true")
        function_ = COUNT_IMAGE;
    else if (method == POST && url == "/GET_IMAGE" && is_image_use == "true")
        function_ = GET_IMAGE;
    else if (method == POST && url == "/POST_IMAGE" && is_image_use == "true")
        function_ = POST_IMAGE;
    else
        function_ = DEFAULT;
}

enum FunctionImplementation::FUNCTIONS FunctionImplementation::get_function()
{
    return function_;
}

enum HTTP_CODE FunctionImplementation::call(struct sockaddr_in address, string path_resource, json &request_json, json &response_json)
{
    return enum_to_function.at(function_)(address, path_resource, request_json, response_json);
}

void FunctionImplementation::clear()
{
    function_ = DEFAULT;
}

enum HTTP_CODE count_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json)
{
    debug("count_image(string, json &, json &)", BEGIN);
    int image_count;

    image_count = get_folder_file_count(path_resource + "/images");
    if (image_count < 0)
    {
        debug("count_image(string, json &, json &)", END);
        return INTERNAL_SERVER_ERROR_500;
    }
    response_json["imageCount"] = image_count;

    debug("count_image(string, json &, json &)", END);
    return OK_200;
}

enum HTTP_CODE get_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json)
{
    debug("get_image(string, json &, json &)", BEGIN);
    if (!request_json.contains("imageNumber"))
        return BAD_REQUEST_400;
    
    response_json["imageName"] = "/images/" + to_string(request_json["imageNumber"]) + ".jpg";
    // 添上数据库的话添上返回原名和上传ip

    debug("get_image(string, json &, json &)", END);
    return OK_200;
}

enum HTTP_CODE post_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json)
{
    debug("post_image(string, json &, json &)", BEGIN);
    if (!(request_json.contains("imageName") && request_json.contains("image")))
    {
        debug("post_image(string, json &, json &)", END);
        return BAD_REQUEST_400;
    }
    string image_received, image_decoded;
    ofstream of;
    int images_have;

    /* 图片base64解码 */
    image_received = request_json["image"];
    if (image_received.compare(0, 23, "data:image/jpeg;base64,"))
    {
        debug("post_image(string, json &, json &)", END);
        return BAD_REQUEST_400;
    }
    image_received.erase(0, 23);
    image_decoded = base64_decode(image_received);

    /* 图片写出 */
    images_have = get_folder_file_count(path_resource + "/images");
    of.open(path_resource + "/images/" + to_string(images_have + 1) + ".jpg", ios::out | ios::trunc);
    if (!of)
    {
        debug("post_image(string, json &, json &)", END);
        return INTERNAL_SERVER_ERROR_500;
    }
    of << image_decoded;
    of.close();
    debug("write file successful");

    debug("post_image(string, json &, json &)", END);
    return OK_200;
}

int get_folder_file_count(string folder_path)
{
    debug("get_folder_file_count()", BEGIN);
    DIR * dir;
    struct dirent * dirent;
    int file_count = 0;

    /* 获取resources/images文件夹下文件数 */
    dir = opendir(folder_path.c_str());
    if (dir == NULL)
    {
        debug("get_folder_file_count()", END);
        return -1;
    }
    while ((dirent = readdir(dir)) != NULL)
         if(strcmp(dirent->d_name, ".") != 0 && strcmp(dirent->d_name, "..") != 0)
            ++file_count;

    debug("get_folder_file_count()", END);
    return file_count;
}
