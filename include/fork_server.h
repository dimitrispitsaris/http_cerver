#ifndef FORK_SERVER_H
#define FORK_SERVER_H

#include "config.h"

int fork_server_handle_connection(
    int client_fd,
    int root_fd,
    const server_config_t *config
);

#endif
