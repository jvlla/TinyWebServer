#ifndef __BASE_64__
#define __BASE_64__
#include <string>

/* BASE64编码函数 */
std::string base64_encode(unsigned char const* , unsigned int len);
/* BASE64解码函数 */
std::string base64_decode(std::string const& s);

#endif