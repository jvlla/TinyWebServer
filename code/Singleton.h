#ifndef __SINGLETON__
#define __SINGLETON__
#include <iostream>

template<typename T>
class Singleton
{
public:
    Singleton(const Singleton&) = delete; 
    Singleton& operator=(const Singleton&) = delete; 

    static T& get_instance() {
        static T t;
        return t;
    }

protected:
    Singleton()
    {
        std::cout << "singleton created" << std::endl;
    }

    virtual ~Singleton()
    {
        std::cout << "singleton destroied" << std::endl;
    }
};

#endif