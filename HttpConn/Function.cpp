#include <iostream>
#include <cstdio>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/mman.h>
#include "Function.h"
#include "HttpConn.h"
#include "json.hpp"
using namespace std;
using json = nlohmann::json;

enum HttpConn::HTTP_CODE default_file(string path_resource, string request_url, struct stat &response_file_stat, char * &response_file)
{
    string path_file;
    char path[1000];
    int fd;

    /* 处理文件名 */
    if (!getcwd(path, sizeof(path)))
        return HttpConn::INTERNAL_SERVER_ERROR_500;
    path_file = path;
    path_file += path_resource + request_url;
    printf("%s\n", path_file.data());

    /* 处理错误情况 */
    if (stat(path_file.data(), &response_file_stat) < 0)
        return HttpConn::NOT_FOUND_404;
    if (!(response_file_stat.st_mode & S_IROTH))
        return HttpConn::FORBIDDEN_403;
    if (S_ISDIR(response_file_stat.st_mode))
        return HttpConn::BAD_REQUEST_400;
    
    /* 进行文件映射 */
    fd = open(path_file.data(), O_RDONLY);
    response_file = (char *) mmap(0, response_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return HttpConn::OK_200;
}

//6是写死的，之后再改，赶进度
enum HttpConn::HTTP_CODE count_image(std::string path_resource, json &request_content_json, json &response_content_json)
{
    response_content_json["pictureCount"] = 3;
    cout << response_content_json["pictureCount"] << endl;
    cout << response_content_json.dump() << endl;
    return HttpConn::OK_200;
}

enum HttpConn::HTTP_CODE get_image(std::string path_resource, json &request_content_json, json &response_content_json)
{
    if (!request_content_json.contains("imageNumber"))
        return HttpConn::BAD_REQUEST_400;
    
    response_content_json["imageName"] = "/images/" + to_string(request_content_json["imageNumber"]) + ".jpg";
    return HttpConn::OK_200;
}

enum HttpConn::HTTP_CODE post_image(std::string path_resource, json &request_content_json, json &response_content_json)
{
    if (!(request_content_json.contains("iName") && request_content_json.contains("image")))
        return HttpConn::BAD_REQUEST_400;
    cout << "yes" << endl;

    return HttpConn::OK_200;
}
