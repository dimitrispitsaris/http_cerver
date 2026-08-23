#ifndef HTTP_H
#define HTTP_H
#include <stddef.h>
#include <sys/types.h>
#define HTTP_MAX_HEADERS 32
#define HTTP_HEADER_NAME_MAX 64
#define HTTP_HEADER_VALUE_MAX 1024


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
    char path[4096];
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
    off_t content_length
);

int http_send_error(
    int client_fd,
    http_status_t status
);

int http_handle_request(int client_fd);

#endif
