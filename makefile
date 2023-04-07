object = locker.o http_conn.o main.o

server : $(object)
	g++ -pthread -o server $(object)

locker.o : locker.h
http_conn.o: http_conn.h locker.h
main.o : locker.h http_conn.h threadpool.h

.PHONY: clean
clean:
	rm server *.o