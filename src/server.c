#include "server.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/time.h>

int server_init(server_t *server,int port,int backlog)
{
	server->port=port;
	server->backlog=backlog;

	server->fd=socket(AF_INET,SOCK_STREAM,0);

	  if (server->fd < 0) {
        perror("socket");
        return -1;
    }

    return 0;
}


int server_start(server_t *server)
{
	struct sockaddr_in address;
	memset(&address,0,sizeof(address));

	address.sin_family=AF_INET;
	address.sin_port=htons(server->port);

	if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        fprintf(stderr, "Invalid server address\n");
        return -1;
    }

    if (bind(server->fd,(struct sockaddr *)&address,sizeof(address))<0)
    {
    	perror("bind");
    	return -1;
    }

    if (listen(server->fd,server->backlog)<0)
    {
    perror("listen");
    return -1;
    }
    return 0;
}

int server_accept(server_t *server)
{
    struct sockaddr_in client_address;
    socklen_t client_address_len = sizeof(client_address);

    int client_fd = accept(
        server->fd,
        (struct sockaddr *)&client_address,
        &client_address_len
    );

    if (client_fd < 0) {
        perror("accept");
        return -1;
    }

    struct timeval timeout;

    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    if (setsockopt(
            client_fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout)
        ) < 0) {

        perror("setsockopt(SO_RCVTIMEO)");

        close(client_fd);
        return -1;
    }

    return client_fd;
}


