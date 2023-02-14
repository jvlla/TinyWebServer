CFLAGS = -std=c++11 -O2 -Wall -g 

.PHONY: webServer, clean

webServer: main.cpp bin/Server.o bin/HttpConn.o bin/Timer.o ThreadPool/ThreadPool.h
	g++ $(CFLAGS) -o $@ main.cpp bin/Server.o bin/HttpConn.o bin/Timer.o \
		ThreadPool/ThreadPool.h -lpthread

bin/Server.o: Server/Server.cpp Server/Server.h
	g++ $(CFLAGS) -c -o $@ Server/Server.cpp

bin/HttpConn.o: HttpConn/HttpConn.cpp HttpConn/HttpConn.h
	g++ $(CFLAGS) -c -o $@ HttpConn/HttpConn.cpp

bin/Timer.o: Timer/Timer.cpp Timer/Timer.h
	g++ $(CFLAGS) -c -o $@ Timer/Timer.cpp

clean:
	rm -rf webServer
	rm -rf bin/*
