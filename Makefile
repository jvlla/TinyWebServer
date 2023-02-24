CFLAGS = -std=c++11 -O1 -Wall -g 

.PHONY: webServer, clean

webServer: main.cpp bin/Server.o bin/HttpConn.o ThreadPool/ThreadPool.h bin/Timer.o bin/FunctionImplementation.o bin/base64.o
	g++ $(CFLAGS) -o $@ main.cpp bin/Server.o bin/HttpConn.o bin/Timer.o bin/FunctionImplementation.o bin/base64.o -lpthread

bin/Server.o: Server/Server.cpp Server/Server.h ThreadPool/ThreadPool.h
	g++ $(CFLAGS) -c -o $@ Server/Server.cpp

bin/HttpConn.o: HttpConn/HttpConn.cpp HttpConn/HttpConn.h HttpConn/HttpRelatedDefinitions.h
	g++ $(CFLAGS) -c -o $@ HttpConn/HttpConn.cpp

bin/FunctionImplementation.o: FunctionImplementation/FunctionImplementation.cpp FunctionImplementation/FunctionImplementation.h \
	HttpConn/HttpRelatedDefinitions.h FunctionImplementation/base64.h 
	g++ $(CFLAGS) -c -o $@ FunctionImplementation/FunctionImplementation.cpp

bin/base64.o: FunctionImplementation/base64.cpp FunctionImplementation/base64.h
	g++ $(CFLAGS) -c -o $@ FunctionImplementation/base64.cpp

bin/Timer.o: Timer/Timer.cpp Timer/Timer.h
	g++ $(CFLAGS) -c -o $@ Timer/Timer.cpp

clean:
	rm -rf webServer
	rm -rf bin/*
