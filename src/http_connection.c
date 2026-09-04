#include "http_connection.h"

#include <stdlib.h>

int http_connection_init(
    http_connection_t *connection,
    size_t buffer_capacity
)
{
    if (connection == NULL ||
        buffer_capacity == 0) {
        return -1;
    }

    connection->buffer = malloc(buffer_capacity);

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

void http_connection_reset(
    http_connection_t *connection
)
{
    if (connection == NULL) {
        return;
    }

    connection->buffer_used = 0;
    connection->close_connection = 0;
}
