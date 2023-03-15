all:
	mkdir -p bin log
	cd code && make

clean:
	rm -rf webServer
	rm -rf bin log
