#ifndef HTTP_PARSE_H
#define HTTP_PARSE_H

#include <stddef.h>

#include "http.h"
#include "config.h"

int http_parse_request(
    const char *buffer,
    http_request_t *request,
    const server_config_t *config
);

const char *http_get_header(
    const http_request_t *request,
    const char *name
);

int http_request_complete(
    const char *buffer,
    size_t length
);

const char *http_find_header_end(
    const char *buffer,
    size_t length
);

#endif
