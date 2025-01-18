CC=gcc
all: server deliver

server: server.c
	gcc server.c -o server
deliver: deliver.c
	gcc deliver.c -lm -o deliver
clean:
	rm -f *.o server deliver