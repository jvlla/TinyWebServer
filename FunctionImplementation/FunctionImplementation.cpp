#include <iostream>
#include <stdio.h>

#include <fstream>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/mman.h>
#include <dirent.h>
#include <mysql/mysql.h>
#include "FunctionImplementation.h"
#include "json.hpp"
#include "base64.h"
#include "../Pool/SQLConnPool.h"

using namespace std;
using json = nlohmann::json;

enum indent {BEGIN, END, MID};
extern void debug(string str, enum indent indent_change = MID);
enum HTTP_CODE count_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json);
enum HTTP_CODE get_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json);
enum HTTP_CODE post_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json);
bool do_sql(string sql, MYSQL_RES * &result);
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
    string sql;
    MYSQL_RES * sql_result;
    MYSQL_ROW sql_row;

    sql = "select count(*) from image";
    if (!do_sql(sql, sql_result) || mysql_num_rows(sql_result) != 1)
    {
        debug("error when do sql: " + sql);
        debug("count_image(string, json &, json &)", END);
        return INTERNAL_SERVER_ERROR_500;
    }
    sql_row = mysql_fetch_row(sql_result);

    image_count = atoi(sql_row[0]);
    response_json["imageCount"] = image_count;

    debug("count_image(string, json &, json &)", END);
    return OK_200;
}

enum HTTP_CODE get_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json)
{
    debug("get_image(string, json &, json &)", BEGIN);
    if (!request_json.contains("imageNumber"))
        return BAD_REQUEST_400;
    int image_number;
    string sql;
    MYSQL_RES * sql_result;
    MYSQL_ROW sql_row;
    struct in_addr ip_net;
    string ip_str;
    string image_name_original;
    
    image_number = request_json["imageNumber"];
    sql = "select image_ip, image_name from image where image_id = " + to_string(image_number);
    cout << sql << endl;
    /* 利用短路求值先调用do_sql()再判断是否只有一行结果 */
    if (!do_sql(sql, sql_result) || mysql_num_rows(sql_result) != 1)
    {
        debug("error when do sql: " + sql);
        debug("get_image(string, json &, json &)", END);
        return INTERNAL_SERVER_ERROR_500;
    }
    sql_row = mysql_fetch_row(sql_result);

    /* 对数据进行格式转换并添加至json */
    ip_net.s_addr = atoi(sql_row[0]);
    ip_str = inet_ntoa(ip_net);
    image_name_original = sql_row[1];
    response_json["imageName"] = "/images/" + to_string(image_number) + ".jpg";
    response_json["uploadIP"] = ip_str;
    response_json["imageNameOriginal"] = image_name_original;

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
    string image_name = request_json["imageName"];
    string image_received, image_decoded;
    ofstream of;
    int image_id;

    /* 先将数据写入数据库以保证多线程下图片编号可靠 */
    MYSQL_RES * sql_result;
    MYSQL_ROW sql_row;
    string sql = "insert into image(image_ip, image_name) values(" + to_string(address.sin_addr.s_addr) + ", \"" + image_name + "\")";
    cout << sql << endl;
    if (!do_sql(sql, sql_result))
    {
        debug("error when do sql: " + sql);
        debug("get_image(string, json &, json &)", END);
        return INTERNAL_SERVER_ERROR_500;
    }
    //-------------------------------可以插一样的---------------------------------
    sql = "select image_id from image where image_ip = " + to_string(address.sin_addr.s_addr)
        + " and image_name = \"" +  image_name + "\"";
    if (!do_sql(sql, sql_result) || mysql_num_rows(sql_result) != 1)
    {
        debug("error when do sql: " + sql);
        debug("get_image(string, json &, json &)", END);
        return INTERNAL_SERVER_ERROR_500;
    }
    sql_row = mysql_fetch_row(sql_result);
    image_id = atoi(sql_row[0]);
    debug("new image id: " + to_string(image_id));

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
    of.open(path_resource + "/images/" + to_string(image_id) + ".jpg", ios::out | ios::trunc);
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

bool do_sql(string sql, MYSQL_RES * &result)
{
    debug("do_sql(string, MYSQL_RES * &)", BEGIN);
    MYSQL * conn;
    int ret;
    string ip_str;
    string image_name_original;
    
    conn = SQLConnPool::get_instance().get_conn();  // 注意释放
    ret = mysql_real_query(conn, sql.c_str(), sql.size());
    if (ret != 0)
    {
        debug("do_sql(string, MYSQL_RES * &)", END);
        return false;
    }
    result = mysql_store_result(conn);
    SQLConnPool::get_instance().free_conn(conn);
    
    debug("do_sql(string, MYSQL_RES * &)", END);
    return true;
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
