#ifndef HTTP_H
#define HTTP_H

#include <sys/types.h>
#include <stddef.h>

#include "config.h"

#define HTTP_MAX_HEADERS DEFAULT_HTTP_MAX_HEADERS
#define HTTP_HEADER_NAME_MAX DEFAULT_HTTP_HEADER_NAME_MAX
#define HTTP_HEADER_VALUE_MAX DEFAULT_HTTP_HEADER_VALUE_MAX
#define HTTP_QUERY_MAX 4096

typedef enum {
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_FORBIDDEN = 403,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
    HTTP_STATUS_INTERNAL_SERVER_ERROR = 500
} http_status_t;

typedef enum {
    HTTP_PARSE_OK = 0,
    HTTP_PARSE_BAD_REQUEST = -1,
    HTTP_PARSE_HEADERS_TOO_LARGE = -2
} http_parse_result_t;

typedef struct {
    char name[HTTP_HEADER_NAME_MAX];
    char value[HTTP_HEADER_VALUE_MAX];
} http_header_t;

typedef struct {
    char method[16];
    char path[4096];
    char query[HTTP_QUERY_MAX];
    char version[16];

    http_header_t headers[HTTP_MAX_HEADERS];
    size_t header_count;

} http_request_t;

int http_handle_request(
    int client_fd,
    int root_fd,
    const server_config_t *config
);

#endif
