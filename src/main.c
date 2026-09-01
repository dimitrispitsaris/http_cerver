#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include "http.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <config.h>

static void reap_children(int signal)
{
    (void)signal;

    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
}


int main(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = reap_children;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    server_t server;
    server_config_t config;

    config_init(&config);


    if (server_init(&server,&config) < 0) {
        return EXIT_FAILURE;
    }

    if (server_start(&server) < 0) {
	server_destroy(&server);
        return EXIT_FAILURE;
    }

    while (1) {
	int client_fd = server_accept(
                 &server,
                 config.client_timeout);

        if (client_fd < 0) {
            continue;
        }

        pid_t pid = fork();

        if (pid < 0) {

            perror("fork");
            close(client_fd);
            continue;
        }

        if (pid == 0) {

            /* CHILD */

            close(server.fd);

            if (http_handle_request(client_fd,server.root_fd,&config) < 0) {
                close(client_fd);
                _exit(EXIT_FAILURE);
            }

            close(client_fd);

            _exit(EXIT_SUCCESS);
        }

        /* PARENT */

        close(client_fd);
    }

    close(server.fd);

    return EXIT_SUCCESS;
}
