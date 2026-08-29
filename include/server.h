#ifndef SERVER_H
#define SERVER_
#include "config.h"

typedef struct{
	int fd;  //Listening TCP socket,created during server init
	int root_fd; // Fd to servers root dir. Used as fs anchor for HTTP req.
	int port;
	int backlog;
}server_t;

int server_init(server_t *server,const server_config_t *config); //Init server runtime state. Creates list socket opens root_dir
int server_start(server_t *server); //Bind and listen on the config add and port

int server_accept(     //Accept client conn
    server_t *server,
    int client_timeout
);

void server_destroy(server_t *server); ///Release server-owned resources.


#endif
