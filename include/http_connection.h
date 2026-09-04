#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include <stddef.h>


typedef struct {
    char *buffer;
    size_t buffer_used;
    size_t buffer_capacity;

    int close_connection;

} http_connection_t;

int http_connection_init(
    http_connection_t *connection,
    size_t buffer_capacity
);

void http_connection_destroy(
    http_connection_t *connection
);

void http_connection_reset(
    http_connection_t *connection
);

#endif
