#include "http_parse.h"
#include "http.h"
#include "config.h"

#include <string.h>
#include <strings.h>





static int http_hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}


static int http_decode_path(
    const char *encoded,
    char *decoded,
    size_t decoded_size)
{
    size_t src = 0;
    size_t dst = 0;

    if (decoded_size == 0) {
        return -1;
    }

    while (encoded[src] != '\0') {
        unsigned char value;

        if (dst + 1 >= decoded_size) {
            return -1;
        }

        if (encoded[src] == '%') {
            if (encoded[src + 1] == '\0' ||
                encoded[src + 2] == '\0') {
                return -1;
            }

            int high = http_hex_value(encoded[src + 1]);
            int low = http_hex_value(encoded[src + 2]);

            if (high < 0 || low < 0) {
                return -1;
            }

            value = (unsigned char)((high << 4) | low);
            src += 3;
        } else {
            value = (unsigned char)encoded[src];
            src++;
        }

        /*
         * NUL and ASCII control characters are not allowed
         * in the decoded filesystem path.
         */
        if (value == '\0' || value < 32 || value == 127) {
            return -1;
        }

        decoded[dst++] = (char)value;
    }

    decoded[dst] = '\0';
    return 0;
}


static int http_parse_target(
    const char *target,
    http_request_t *request)
{
    char path[4096];
    const char *query_start = strchr(target, '?');

    if (query_start != NULL) {
        size_t path_length = (size_t)(query_start - target);

        if (path_length == 0 || path_length >= sizeof(path)) {
            return -1;
        }

        memcpy(path, target, path_length);
        path[path_length] = '\0';

        query_start++;

        size_t query_length = strlen(query_start);

        if (query_length >= sizeof(request->query)) {
            return -1;
        }

        memcpy(request->query, query_start, query_length + 1);
    } else {
        size_t path_length = strlen(target);

        if (path_length == 0 || path_length >= sizeof(path)) {
            return -1;
        }

        memcpy(path, target, path_length + 1);
        request->query[0] = '\0';
    }

    /*
     * HTTP origin-form request targets must begin with '/'.
     */
    if (path[0] != '/') {
        return -1;
    }

    /*
     * Decode the path before passing it to the filesystem layer.
     */
    return http_decode_path(
        path,
        request->path,
        sizeof(request->path)
    );
}


const char *http_find_header_end(
    const char *buffer,
    size_t length)
{
    if (length < 4) {
        return NULL;
    }

    for (size_t i = 0; i <= length - 4; i++) {
        if (buffer[i] == '\r' &&
            buffer[i + 1] == '\n' &&
            buffer[i + 2] == '\r' &&
            buffer[i + 3] == '\n') {
            return buffer + i + 4;
        }
    }

    return NULL;
}


int http_request_complete(
    const char *buffer,
    size_t length)
{
    return http_find_header_end(buffer, length) != NULL;
}



const char *http_get_header(
    const http_request_t *request,
    const char *name)
{
    for (size_t i = 0; i < request->header_count; i++) {
        if (strcasecmp(request->headers[i].name, name) == 0) {
            return request->headers[i].value;
        }
    }

    return NULL;
}

static int http_is_tchar(unsigned char c)
{
    if ((c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z')) {
        return 1;
    }

    switch (c) {
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case '\'':
        case '*':
        case '+':
        case '-':
        case '.':
        case '^':
        case '_':
        case '`':
        case '|':
        case '~':
            return 1;

        default:
            return 0;
    }
}


static int http_valid_header_name(
    const char *name,
    size_t length)
{
    if (length == 0) {
        return 0;
    }

    for (size_t i = 0; i < length; i++) {
        if (!http_is_tchar((unsigned char)name[i])) {
            return 0;
        }
    }

    return 1;
}


static int http_valid_header_value(
    const char *value,
    size_t length)
{
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)value[i];

        /*
         * Reject all control characters except HTAB.
         */
        if (c < 32 && c != '\t') {
            return 0;
        }

        if (c == 127) {
            return 0;
        }
    }

    return 1;
}

static int http_parse_headers(
    const char *buffer,
    http_request_t *request,
    const server_config_t *config)
{
    const char *line = strstr(buffer, "\r\n");

    if (line == NULL) {
        return HTTP_PARSE_BAD_REQUEST;
    }

    line += 2;

    while (1) {
        if (line[0] == '\r' && line[1] == '\n') {
            return HTTP_PARSE_OK;
        }

        if (request->header_count >= config->http_max_headers) {
            return HTTP_PARSE_HEADERS_TOO_LARGE;
        }

        const char *line_end = strstr(line, "\r\n");

        if (line_end == NULL) {
            return HTTP_PARSE_BAD_REQUEST;
        }

        const char *colon = memchr(
            line,
            ':',
            (size_t)(line_end - line)
        );

        if (colon == NULL) {
            return HTTP_PARSE_BAD_REQUEST;
        }

        size_t name_length = (size_t)(colon - line);

        /*
         * Whitespace before ':' is forbidden in an HTTP field-name.
         */
        if (name_length == 0 ||
            line[name_length - 1] == ' ' ||
            line[name_length - 1] == '\t') {
            return HTTP_PARSE_BAD_REQUEST;
        }

        if (!http_valid_header_name(line, name_length)) {
            return HTTP_PARSE_BAD_REQUEST;
        }

        if (name_length >= config->http_header_name_max) {
            return HTTP_PARSE_HEADERS_TOO_LARGE;
        }

        http_header_t *header =
            &request->headers[request->header_count];

        memcpy(header->name, line, name_length);
        header->name[name_length] = '\0';

        const char *value_start = colon + 1;

        while (value_start < line_end &&
               (*value_start == ' ' || *value_start == '\t')) {
            value_start++;
        }

        size_t value_length = (size_t)(line_end - value_start);

        /*
         * Remove trailing optional whitespace.
         */
        while (value_length > 0 &&
               (value_start[value_length - 1] == ' ' ||
                value_start[value_length - 1] == '\t')) {
            value_length--;
        }

        if (!http_valid_header_value(value_start, value_length)) {
            return HTTP_PARSE_BAD_REQUEST;
        }

        if (value_length >= config->http_header_value_max) {
            return HTTP_PARSE_HEADERS_TOO_LARGE;
        }

        memcpy(header->value, value_start, value_length);
        header->value[value_length] = '\0';

        request->header_count++;
        line = line_end + 2;
    }
}


static int http_parse_request_line(
    const char *buffer,
    http_request_t *request,
    char *target,
    size_t target_size)
{
    const char *line_end = strstr(buffer, "\r\n");

    if (line_end == NULL) {
        return -1;
    }

    const char *first_space = memchr(
        buffer,
        ' ',
        (size_t)(line_end - buffer)
    );

    if (first_space == NULL) {
        return -1;
    }

    const char *second_space = memchr(
        first_space + 1,
        ' ',
        (size_t)(line_end - first_space - 1)
    );

    if (second_space == NULL) {
        return -1;
    }

    /*
     * Exactly two SP characters are required.
     */
    if (memchr(
            second_space + 1,
            ' ',
            (size_t)(line_end - second_space - 1)
        ) != NULL) {
        return -1;
    }

    size_t method_length = (size_t)(first_space - buffer);

    if (method_length == 0 ||
        method_length >= sizeof(request->method)) {
        return -1;
    }

    if (!http_valid_header_name(buffer, method_length)) {
        /*
         * HTTP method syntax uses the same tchar character set.
         */
        return -1;
    }

    memcpy(request->method, buffer, method_length);
    request->method[method_length] = '\0';

    size_t target_length =
        (size_t)(second_space - first_space - 1);

    if (target_length == 0 || target_length >= target_size) {
        return -1;
    }

    memcpy(target, first_space + 1, target_length);
    target[target_length] = '\0';

    size_t version_length =
        (size_t)(line_end - second_space - 1);

    if (version_length == 0 ||
        version_length >= sizeof(request->version)) {
        return -1;
    }

    memcpy(request->version, second_space + 1, version_length);
    request->version[version_length] = '\0';

    return 0;
}


int http_parse_request(
    const char *buffer,
    http_request_t *request,
    const server_config_t *config)
{
    if (buffer == NULL || request == NULL) {
        return HTTP_PARSE_BAD_REQUEST;
    }

    request->header_count = 0;
    request->query[0] = '\0';

    char target[4096];

    if (http_parse_request_line(
            buffer,
            request,
            target,
            sizeof(target)
        ) < 0) {
        return HTTP_PARSE_BAD_REQUEST;
    }

    /*
     * Currently support HTTP/1.0 and HTTP/1.1.
     */
    if (strcmp(request->version, "HTTP/1.1") != 0 &&
        strcmp(request->version, "HTTP/1.0") != 0) {
        return HTTP_PARSE_BAD_REQUEST;
    }

    if (http_parse_target(target, request) < 0) {
        return HTTP_PARSE_BAD_REQUEST;
    }

    return http_parse_headers(buffer, request, config);
}



