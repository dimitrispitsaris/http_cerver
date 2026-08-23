#include "server.h"
#include "http.h"

#include <stdio.h>
#include <unistd.h>

int main(void)
{
    server_t server;

    if (server_init(&server, 8080, 50) < 0) {
        return 1;
    }

    if (server_start(&server) < 0) {
        close(server.fd);
        return 1;
    }

    printf(
        "HTTP server listening on 127.0.0.1:%d\n",
        server.port
    );

    while (1) {

        int client_fd = server_accept(&server);

        if (client_fd < 0) {
            continue;
        }

        printf(
            "Client connected: fd=%d\n",
            client_fd
        );

        if (http_handle_request(client_fd) < 0) {
            fprintf(
                stderr,
                "Error handling client fd=%d\n",
                client_fd
            );
        }

        close(client_fd);

        printf(
            "Client disconnected: fd=%d\n",
            client_fd
        );
    }

    close(server.fd);

    return 0;
}

