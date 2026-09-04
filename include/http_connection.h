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

int http_connection_feed(
    http_connection_t *connection,
    const char *data,
    size_t length
);

const char *http_connection_data(
    const http_connection_t *connection
);

size_t http_connection_data_length(
    const http_connection_t *connection
);

size_t http_connection_buffer_space(
    const http_connection_t *connection
);

int http_connection_request_ready(
    const http_connection_t *connection
);

size_t http_connection_request_length(
    const http_connection_t *connection
);

void http_connection_consume(
    http_connection_t *connection,
    size_t length
);

#endif
