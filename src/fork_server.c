#include "fork_server.h"
#include "http.h"
#include "http_connection.h"
#include "http_response.h"

#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>

int fork_server_handle_connection(
    int client_fd,
    int root_fd,
    const server_config_t *config)
{
    if (config == NULL) {
        return -1;
    }

    http_connection_t connection;

    if (http_connection_init(
            &connection,
            config->http_buffer_size
        ) < 0) {
        perror("http_connection_init");
        return -1;
    }

    int result = 0;

    while (!connection.close_connection) {
    
	size_t remaining_capacity =
        http_connection_buffer_space(&connection);

    	char receive_buffer[4096];

	size_t receive_capacity =
             remaining_capacity < sizeof(receive_buffer)
              ? remaining_capacity
              : sizeof(receive_buffer);

    	if (receive_capacity == 0) {
        	http_send_error(
            	client_fd,
            	HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE,
            	0
       	     );

       	     result = -1;
             break;
    	}

    	ssize_t received = recv(
        	client_fd,
        	receive_buffer,
        	receive_capacity,
        	0
    		);

    	if (received < 0) {
        	if (errno == EINTR) {
            		continue;
        	}

        	if (errno == EAGAIN ||
            	    errno == EWOULDBLOCK) {
            	    fprintf(stderr, "Client receive timeout\n" );

            	   result = -1;
                   break;
                 }

                perror("recv");

                result = -1;
                break;
               }

    	if (received == 0) {
        	break;
    	}

    	if (http_connection_feed(
            &connection,
            receive_buffer,
            (size_t)received) < 0) {
            http_send_error( client_fd, HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE, 0 );

            result = -1;
            break;
          }

    	if (http_process_connection(&connection,client_fd,root_fd,config) < 0) {
            result = -1;
            break;
         }
    }
    http_connection_destroy(&connection);

    return result;
}
