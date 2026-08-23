#include "server.h"
#include "http.h"
#include <stdio.h>
#include "file.h"
#include <unistd.h>

int main(void)
{
    server_t server;

    if (server_init(&server, 8080, 50) < 0) {
        return 1;
    }

    if (server_start(&server) < 0) {
        return 1;
    }

    printf("Server listening on port %d\n", server.port);
    while (1) {
        int client_fd = server_accept(&server);

        if (client_fd < 0) {
            continue;
          }

        printf("Client connected: fd=%d\n", client_fd);

        http_handle_request(client_fd);

        close(client_fd);
    }
    return 0;


}
