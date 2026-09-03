#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include "http.h"

const char *http_status_reason(
    http_status_t status
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

#endif
