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
#include "../Log/Log.h"
#include <stdio.h>

using namespace std;
using json = nlohmann::json;

enum HTTP_CODE count_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json);
enum HTTP_CODE get_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json);
enum HTTP_CODE post_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json);
bool do_sql(string sql, MYSQL_RES * &result);

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
    log(Log::DEBUG, "[begin] count_image(string, json &, json &)");
    int image_count;
    string sql;
    MYSQL_RES * sql_result;
    MYSQL_ROW sql_row;

    sql = "select count(*) from image";
    if (!do_sql(sql, sql_result) || mysql_num_rows(sql_result) != 1)
    {
        log(Log::ERROR, "error when do sql: " + sql);
        log(Log::DEBUG, "[end  ] count_image(string, json &, json &)");
        return INTERNAL_SERVER_ERROR_500;
    }
    sql_row = mysql_fetch_row(sql_result);

    image_count = atoi(sql_row[0]);
    response_json["imageCount"] = image_count;

    log(Log::DEBUG, "[end  ] count_image(string, json &, json &)");
    return OK_200;
}

enum HTTP_CODE get_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json)
{
    log(Log::DEBUG, "[begin] get_image(string, json &, json &)");
    if (!request_json.contains("imageNumber"))
    {
        log(Log::WARN, "json key not found");
        log(Log::DEBUG, "[end  ] get_image(string, json &, json &)");
        return BAD_REQUEST_400;
    }
    int image_number;
    string sql;
    MYSQL_RES * sql_result;
    MYSQL_ROW sql_row;
    struct in_addr ip_net;
    string ip_str;
    string image_name_original;
    
    image_number = request_json["imageNumber"];
    sql = "select image_ip, image_name from image where image_id = " + to_string(image_number);
    /* 利用短路求值先调用do_sql()再判断是否只有一行结果 */
    if (!do_sql(sql, sql_result) || mysql_num_rows(sql_result) != 1)
    {
        log(Log::ERROR, "error when do sql: " + sql);
        log(Log::DEBUG, "[end  ] get_image(string, json &, json &)");
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

    log(Log::DEBUG, "[end  ] get_image(string, json &, json &)");
    return OK_200;
}

enum HTTP_CODE post_image(struct sockaddr_in address, string path_resource, json &request_json, json &response_json)
{
    log(Log::DEBUG, "[begin] post_image(string, json &, json &)");
    if (!(request_json.contains("imageName") && request_json.contains("image")))
    {
        log(Log::WARN, "json key not found");
        log(Log::DEBUG, "[end  ] post_image(string, json &, json &)");
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
    if (!do_sql(sql, sql_result))
    {
        log(Log::ERROR, "error when do sql: " + sql);
        log(Log::DEBUG, "[end  ] get_image(string, json &, json &)");
        return INTERNAL_SERVER_ERROR_500;
    }
    // 可以插入同名图片，直接选择最后一张即可
    sql = "select image_id from image where image_ip = " + to_string(address.sin_addr.s_addr)
        + " and image_name = \"" +  image_name + "\"";
    if (!do_sql(sql, sql_result) || mysql_num_rows(sql_result) < 1) // 注意可能不止一条记录
    {
        log(Log::ERROR, "error when do sql: " + sql);
        log(Log::DEBUG, "[end  ] get_image(string, json &, json &)");
        return INTERNAL_SERVER_ERROR_500;
    }
    // 取最后一行记录
    uint64_t count = mysql_num_rows(sql_result);
    do {
        sql_row = mysql_fetch_row(sql_result);
    } while (--count > 0);
    image_id = atoi(sql_row[0]);  // id为最后一张图片id
    log(Log::INFO, "new image id: " + to_string(image_id));

    /* 图片base64解码 */
    image_received = request_json["image"];
    if (image_received.compare(0, 23, "data:image/jpeg;base64,"))
    {
        log(Log::WARN, "image base64 coding is invalid");
        log(Log::DEBUG, "[end  ] post_image(string, json &, json &)");
        return BAD_REQUEST_400;
    }
    image_received.erase(0, 23);
    image_decoded = base64_decode(image_received);

    /* 图片写出 */
    of.open(path_resource + "/images/" + to_string(image_id) + ".jpg", ios::out | ios::trunc);
    if (!of.is_open())
    {
        log(Log::ERROR, "open new image file failed");
        log(Log::DEBUG, "[end  ] post_image(string, json &, json &)");
        return INTERNAL_SERVER_ERROR_500;
    }
    of << image_decoded;
    of.close();
    log(Log::INFO, "write file successful");

    log(Log::DEBUG, "[end  ] post_image(string, json &, json &)");
    return OK_200;
}

bool do_sql(string sql, MYSQL_RES * &result)
{
    log(Log::DEBUG, "[begin] do_sql(string, MYSQL_RES * &)");
    MYSQL * conn;
    int ret;
    string ip_str;
    string image_name_original;
    
    conn = SQLConnPool::get_instance().get_conn();  // 注意释放
    ret = mysql_real_query(conn, sql.c_str(), sql.size());
    if (ret != 0)
    {
        log(Log::DEBUG, "[end  ] do_sql(string, MYSQL_RES * &)");
        return false;
    }
    result = mysql_store_result(conn);
    SQLConnPool::get_instance().free_conn(conn);
    
    log(Log::DEBUG, "[end  ] do_sql(string, MYSQL_RES * &)");
    return true;
}
