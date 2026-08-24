#ifndef HTTP_H
#define HTTP_H

#include <sys/types.h>
#include <stddef.h>

#include "config.h"

#define HTTP_MAX_HEADERS 32
#define HTTP_HEADER_NAME_MAX 64
#define HTTP_HEADER_VALUE_MAX 4096
#define HTTP_QUERY_MAX 4096

typedef enum {
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_FORBIDDEN = 403,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_INTERNAL_SERVER_ERROR = 500
} http_status_t;


typedef struct {
    char name[HTTP_HEADER_NAME_MAX];
    char value[HTTP_HEADER_VALUE_MAX];
} http_header_t;


typedef struct {
    char method[16];

    /*
     * Decoded URL path.
     *
     * Example:
     *
     *     /hello%20world.html
     *
     * becomes:
     *
     *     /hello world.html
     */
    char path[4096];

    /*
     * Query string, without '?'.
     *
     * Example:
     *
     *     /index.html?name=dimitris
     *
     * gives:
     *
     *     path  = "/index.html"
     *     query = "name=dimitris"
     */
    char query[HTTP_QUERY_MAX];

    char version[16];

    http_header_t headers[HTTP_MAX_HEADERS];
    size_t header_count;

} http_request_t;


const char *http_status_reason(http_status_t status);


int http_parse_request(
    const char *buffer,
    http_request_t *request
);


int http_send_response(
    int client_fd,
    http_status_t status,
    const char *content_type,
    off_t content_length,
    int keep_alive
);


int http_send_error(
    int client_fd,
    http_status_t status,
    int keep_alive
);


int http_handle_request(
    int client_fd,
    const server_config_t *config
);

#endif

