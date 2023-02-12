CFLAGS = -std=c++11 -O2 -Wall -g 

TARGET = webServer
# OBJS = main.cpp Server/*.cpp HttpConn/*.cpp Timer/*.cpp
# HS = Server/*.h HttpConn/*.h ThreadPool/*.h Timer/*.h
DIR = Server HttpConn ThreadPool Timer
SOURCE_CPP = Server/Server.cpp HttpConn/HttpConn.cpp Timer/Timer.cpp
SOURCE_H = Server/Server.h HttpConn/HttpConn.h ThreadPool/ThreadPool.h\
	Timer/Timer.h

.PHONY: all, clean

all: $(OBJS)
	g++ $(CFLAGS) -o $(TARGET) main.cpp $(SOURCE_CPP) $(SOURCE_H) -pthread

clean:
	rm -rf $(TARGET)
