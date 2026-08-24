#ifndef SERVER_H
#define SERVER_H

typedef struct{
	int fd;
	int port;
	int backlog;
}server_t;
//We use pointer because we want the func to modify the structure
int server_init(server_t *server,int port,int backlog);
int server_start(server_t *server);
int server_accept(
    server_t *server,
    int client_timeout
);


#endif
