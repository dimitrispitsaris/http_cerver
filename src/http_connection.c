#include "http_connection.h"
#include "http_parse.h"

#include <stdlib.h>
#include <string.h>

int http_connection_init(
    http_connection_t *connection,
    size_t buffer_capacity
)
{
    if (connection == NULL ||
        buffer_capacity == 0) {
        return -1;
    }

    connection->buffer =
        malloc(buffer_capacity);

    if (connection->buffer == NULL) {
        return -1;
    }

    connection->buffer_used = 0;
    connection->buffer_capacity = buffer_capacity;
    connection->close_connection = 0;

    return 0;
}

void http_connection_destroy(
    http_connection_t *connection
)
{
    if (connection == NULL) {
        return;
    }

    free(connection->buffer);

    connection->buffer = NULL;
    connection->buffer_used = 0;
    connection->buffer_capacity = 0;
    connection->close_connection = 0;
}

int http_connection_feed(
    http_connection_t *connection,
    const char *data,
    size_t length
)
{
    if (connection == NULL ||
        data == NULL) {
        return -1;
    }

    if (length >
        connection->buffer_capacity -
        connection->buffer_used) {

        return -1;
    }

    memcpy(
        connection->buffer +
            connection->buffer_used,
        data,
        length
    );

    connection->buffer_used += length;

    return 0;
}

const char *http_connection_data(
    const http_connection_t *connection
)
{
    if (connection == NULL) {
        return NULL;
    }

    return connection->buffer;
}

size_t http_connection_data_length(
    const http_connection_t *connection
)
{
    if (connection == NULL) {
        return 0;
    }

    return connection->buffer_used;
}

int http_connection_request_ready(
    const http_connection_t *connection
)
{
    if (connection == NULL) {
        return 0;
    }

    return http_request_complete(
        connection->buffer,
        connection->buffer_used
    );
}

size_t http_connection_buffer_space(
    const http_connection_t *connection
)
{
    if (connection == NULL) {
        return 0;
    }

    return connection->buffer_capacity -
           connection->buffer_used;
}

size_t http_connection_request_length(
    const http_connection_t *connection
)
{
    if (connection == NULL) {
        return 0;
    }

    const char *header_end =
        http_find_header_end(
            connection->buffer,
            connection->buffer_used
        );

    if (header_end == NULL) {
        return 0;
    }

    return (size_t)(
        header_end - connection->buffer
    );
}

void http_connection_consume(
    http_connection_t *connection,
    size_t length
)
{
    if (connection == NULL ||
        length > connection->buffer_used) {
        return;
    }

    size_t remaining =
        connection->buffer_used - length;

    if (remaining > 0) {
        memmove(
            connection->buffer,
            connection->buffer + length,
            remaining
        );
    }

    connection->buffer_used = remaining;
}
