#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <stddef.h>
#include <sys/types.h>

#include "http.h"

#define HTTP_RESPONSE_HEADER_MAX 1024

typedef struct http_response_t{
    char headers[HTTP_RESPONSE_HEADER_MAX];
    size_t headers_length;
    size_t headers_sent;
    
    char *body;
    size_t body_length;
    size_t body_sent;

    int file_fd;
    off_t file_size;
    off_t file_offset;

    int send_body;
    int complete;
} http_response_t;

void http_response_init(
    http_response_t *response
);

void http_response_destroy(
    http_response_t *response
);

int http_build_response_headers(
    char *buffer,
    size_t capacity,
    http_status_t status,
    const char *content_type,
    off_t content_length,
    int keep_alive
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

int http_send_bad_request_message(
    int client_fd,
    const char *message
);

int http_response_set_body(
    http_response_t *response,
    const char *body,
    size_t body_length
);

int http_response_prepare_error(
    http_response_t *response,
    http_status_t status,
    const char *message
);
int http_response_send_blocking(
    int client_fd,
    http_response_t *response
);

#endif
