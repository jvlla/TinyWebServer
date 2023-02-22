CFLAGS = -std=c++11 -O1 -Wall -g 

.PHONY: webServer, clean

webServer: main.cpp bin/Server.o bin/HttpConn.o bin/Function.o ThreadPool/ThreadPool.h bin/Timer.o
	g++ $(CFLAGS) -o $@ main.cpp bin/Server.o bin/HttpConn.o bin/Function.o ThreadPool/ThreadPool.h bin/Timer.o -lpthread

bin/Server.o: Server/Server.cpp Server/Server.h
	g++ $(CFLAGS) -c -o $@ Server/Server.cpp

bin/HttpConn.o: HttpConn/HttpConn.cpp HttpConn/HttpConn.h
	g++ $(CFLAGS) -c -o $@ HttpConn/HttpConn.cpp

bin/Function.o: HttpConn/Function.cpp HttpConn/Function.h
	g++ $(CFLAGS) -c -o $@ HttpConn/Function.cpp

bin/Timer.o: Timer/Timer.cpp Timer/Timer.h
	g++ $(CFLAGS) -c -o $@ Timer/Timer.cpp

clean:
	rm -rf webServer
	rm -rf bin/*
