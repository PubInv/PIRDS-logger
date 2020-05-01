all: server webserver

server: server.c
	gcc -o server server.c

webserver: webserver.c
	gcc -o webserver webserver.c
