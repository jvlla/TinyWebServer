#ifndef __BASE_64__
#define __BASE_64__
#include <string>

std::string base64_encode(unsigned char const* , unsigned int len);
std::string base64_decode(std::string const& s);

#endif