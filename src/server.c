#define _GNU_SOURCE
#include "server.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>


int server_init(server_t *server,const server_config_t *config)
{
	server->port=config->port;
	server->backlog=config->backlog;
	// Initialize FDs to invalid values. * * This is important for server_destroy().
	 server->fd = -1;
	 server->root_fd = -1;

	server->fd=socket(AF_INET,SOCK_STREAM,0);

	  if (server->fd < 0) {
       		  perror("socket");
          return -1;
    }

	  server->root_fd=open(config->document_root,O_RDONLY | O_DIRECTORY);

	  if (server->root_fd<0)
	  {
		  perror("open document root");
		  close(server->fd);
		  server->fd=-1;
		  return -1;
	  }


    return 0;
}


int server_start(server_t *server)
{
	struct sockaddr_in address;
	memset(&address,0,sizeof(address));

	address.sin_family=AF_INET; //IPv4
	address.sin_port=htons(server->port); //Convert host byte order to network byte order

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

int server_accept(server_t *server,int client_timeout)
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

    //Config receive timeout. Prevents persistent HTTP conn from waiting forever for next req.
    struct timeval timeout;

    timeout.tv_sec = client_timeout;
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

void server_destroy(server_t *server)
    {
	    if (server->fd>=0)
	    {
		    close(server->fd);
		    server->fd=-1;
	    }

	    if (server->root_fd>=0)
	    {
		    close(server->root_fd);
		    server->root_fd=-1;
	    }

	}	

