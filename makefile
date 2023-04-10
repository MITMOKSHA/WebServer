object = locker.o http_conn.o main.o timer.o

server : $(object)
	g++ -g -pthread -o server $(object)

locker.o : locker.h
	g++ -c -g -o locker.o locker.cpp
http_conn.o: http_conn.h locker.h timer.h
	g++ -c -g -o http_conn.o http_conn.cpp
main.o : locker.h http_conn.h threadpool.h
	g++ -c -g -o main.o main.cpp
timer.o: timer.h
	g++ -c -g -o timer.o timer.cpp

.PHONY: clean
clean:
	rm server *.o