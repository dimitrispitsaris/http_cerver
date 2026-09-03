#include "file.h"
#include "mime.h"
#include "io.h"
#include "http.h"
#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>


static int http_config_valid(const server_config_t *config)
{
    if (config == NULL) {
        return 0;
    }

    if (config->http_buffer_size == 0) {
        return 0;
    }

    if (config->http_max_headers == 0 ||
        config->http_max_headers > HTTP_MAX_HEADERS) {
        return 0;
    }

    if (config->http_header_name_max == 0 ||
        config->http_header_name_max > HTTP_HEADER_NAME_MAX) {
        return 0;
    }

    if (config->http_header_value_max == 0 ||
        config->http_header_value_max > HTTP_HEADER_VALUE_MAX) {
        return 0;
    }

    return 1;
}


static int http_method_allowed(const char *method)
{
    return strcmp(method, "GET") == 0 ||
           strcmp(method, "HEAD") == 0;
}


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


static const char *http_find_header_end(
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


static int http_request_complete(
    const char *buffer,
    size_t length)
{
    return http_find_header_end(buffer, length) != NULL;
}


static int http_validate_host(const http_request_t *request)
{
    size_t host_count = 0;

    for (size_t i = 0; i < request->header_count; i++) {
        if (strcasecmp(request->headers[i].name, "Host") == 0) {
            host_count++;
        }
    }

    /*
     * HTTP/1.1 requires exactly one Host header.
     */
    return host_count == 1 ? 0 : -1;
}


static const char *http_get_header(
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


static int http_validate_request_framing(
    const http_request_t *request,
    const char **reason)
{
    const char *content_length = NULL;
    size_t content_length_count = 0;
    int transfer_encoding_present = 0;

    for (size_t i = 0; i < request->header_count; i++) {
        const http_header_t *header = &request->headers[i];

        if (strcasecmp(header->name, "Content-Length") == 0) {
            content_length_count++;

            if (content_length_count > 1) {
                *reason = "Duplicate Content-Length headers are not supported";
                return -1;
            }

            content_length = header->value;
        }

        if (strcasecmp(header->name, "Transfer-Encoding") == 0) {
            transfer_encoding_present = 1;
        }
    }

    /*
     * This server does not implement request bodies or
     * Transfer-Encoding.
     */
    if (transfer_encoding_present) {
        *reason = "Transfer-Encoding is not supported";
        return -1;
    }

    if (content_length == NULL) {
        return 0;
    }

    if (*content_length == '\0') {
        *reason = "Invalid Content-Length";
        return -1;
    }

    const char *start = content_length;
    const char *end = content_length + strlen(content_length);

    /*
     * Header parsing removes leading OWS but preserves trailing OWS.
     */
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }

    if (start == end) {
        *reason = "Invalid Content-Length";
        return -1;
    }

    unsigned long long length = 0;

    for (const char *p = start; p < end; p++) {
        if (*p < '0' || *p > '9') {
            *reason = "Invalid Content-Length";
            return -1;
        }

        unsigned int digit = (unsigned int)(*p - '0');

        if (length > (ULLONG_MAX - digit) / 10) {
            *reason = "Invalid Content-Length";
            return -1;
        }

        length = length * 10 + digit;
    }

    /*
     * Content-Length: 0 is an empty request body.
     */
    if (length == 0) {
        return 0;
    }

    /*
     * Non-zero request bodies are not currently supported.
     */
    *reason = "Request bodies are not supported";
    return -1;
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


const char *http_status_reason(http_status_t status)
{
    switch (status) {
        case HTTP_STATUS_OK:
            return "OK";

        case HTTP_STATUS_BAD_REQUEST:
            return "Bad Request";

        case HTTP_STATUS_FORBIDDEN:
            return "Forbidden";

        case HTTP_STATUS_NOT_FOUND:
            return "Not Found";

        case HTTP_STATUS_METHOD_NOT_ALLOWED:
            return "Method Not Allowed";

        case HTTP_STATUS_INTERNAL_SERVER_ERROR:
            return "Internal Server Error";

        case HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE:
            return "Request Header Fields Too Large";

        default:
            return "Unknown";
    }
}


int http_send_response(
    int client_fd,
    http_status_t status,
    const char *content_type,
    off_t content_length,
    int keep_alive)
{
    const char *reason = http_status_reason(status);

    if (reason == NULL || content_type == NULL || content_length < 0) {
        return -1;
    }

    const char *connection = keep_alive ? "keep-alive" : "close";

    char response[1024];

    int length = snprintf(
        response,
        sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lld\r\n"
        "Connection: %s\r\n"
        "\r\n",
        status,
        reason,
        content_type,
        (long long)content_length,
        connection
    );

    if (length < 0 || (size_t)length >= sizeof(response)) {
        return -1;
    }

    ssize_t sent = io_send_all(
        client_fd,
        response,
        (size_t)length
    );

    if (sent < 0) {
        return -1;
    }

    if ((size_t)sent != (size_t)length) {
        fprintf(stderr, "Connection closed while sending response\n");
        return -1;
    }

    return 0;
}


int http_send_error(
    int client_fd,
    http_status_t status,
    int keep_alive)
{
    const char *body;

    switch (status) {
        case HTTP_STATUS_BAD_REQUEST:
            body =
                "<html>"
                "<body>"
                "<h1>400 Bad Request</h1>"
                "</body>"
                "</html>";
            break;

        case HTTP_STATUS_FORBIDDEN:
            body =
                "<html>"
                "<body>"
                "<h1>403 Forbidden</h1>"
                "</body>"
                "</html>";
            break;

        case HTTP_STATUS_NOT_FOUND:
            body =
                "<html>"
                "<body>"
                "<h1>404 Not Found</h1>"
                "</body>"
                "</html>";
            break;

        case HTTP_STATUS_METHOD_NOT_ALLOWED:
            body =
                "<html>"
                "<body>"
                "<h1>405 Method Not Allowed</h1>"
                "</body>"
                "</html>";
            break;

        case HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE:
            body =
                "<html>"
                "<body>"
                "<h1>431 Request Header Fields Too Large</h1>"
                "</body>"
                "</html>";
            break;

        case HTTP_STATUS_INTERNAL_SERVER_ERROR:
            body =
                "<html>"
                "<body>"
                "<h1>500 Internal Server Error</h1>"
                "</body>"
                "</html>";
            break;

        default:
            return -1;
    }

    size_t body_length = strlen(body);

    if (http_send_response(
            client_fd,
            status,
            "text/html",
            (off_t)body_length,
            keep_alive
        ) < 0) {
        return -1;
    }

    ssize_t sent = io_send_all(
        client_fd,
        body,
        body_length
    );

    if (sent < 0) {
        return -1;
    }

    if ((size_t)sent != body_length) {
        fprintf(stderr, "Connection closed while sending error body\n");
        return -1;
    }

    return 0;
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
    if (buffer == NULL || request == NULL || !http_config_valid(config)) {
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


static int http_send_bad_request_message(
    int client_fd,
    const char *message)
{
    char body[512];

    int length = snprintf(
        body,
        sizeof(body),
        "<html>"
        "<body>"
        "<h1>400 Bad Request</h1>"
        "<p>%s</p>"
        "</body>"
        "</html>",
        message
    );

    if (length < 0 || (size_t)length >= sizeof(body)) {
        return -1;
    }

    if (http_send_response(
            client_fd,
            HTTP_STATUS_BAD_REQUEST,
            "text/html",
            (off_t)length,
            0
        ) < 0) {
        return -1;
    }

    ssize_t sent = io_send_all(
        client_fd,
        body,
        (size_t)length
    );

    if (sent < 0) {
        return -1;
    }

    if ((size_t)sent != (size_t)length) {
        fprintf(
            stderr,
            "Connection closed while sending bad request body\n"
        );
        return -1;
    }

    return 0;
}


static int http_process_request(
    int client_fd,
    const char *buffer,
    size_t length,
    int *close_connection,
    int root_fd,
    const server_config_t *config)
{
    char *request_buffer = malloc(length + 1);

    if (request_buffer == NULL) {
        perror("malloc");
        *close_connection = 1;
        return -1;
    }

    memcpy(request_buffer, buffer, length);
    request_buffer[length] = '\0';

    printf("Raw HTTP request:\n%s\n", request_buffer);

    http_request_t request;

    int parse_result = http_parse_request(
        request_buffer,
        &request,
        config
    );

    free(request_buffer);

    if (parse_result != HTTP_PARSE_OK) {
        *close_connection = 1;

        if (parse_result == HTTP_PARSE_HEADERS_TOO_LARGE) {
            fprintf(stderr, "HTTP request headers too large\n");

            return http_send_error(
                client_fd,
                HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE,
                0
            );
        }

        fprintf(stderr, "Invalid HTTP request\n");

        return http_send_error(
            client_fd,
            HTTP_STATUS_BAD_REQUEST,
            0
        );
    }

    if (!http_method_allowed(request.method)) {
        *close_connection = 1;

        return http_send_error(
            client_fd,
            HTTP_STATUS_METHOD_NOT_ALLOWED,
            0
        );
    }

    /*
     * HTTP/1.1 requires exactly one Host header.
     */
    if (strcmp(request.version, "HTTP/1.1") == 0 &&
        http_validate_host(&request) < 0) {
        *close_connection = 1;

        return http_send_error(
            client_fd,
            HTTP_STATUS_BAD_REQUEST,
            0
        );
    }

    const char *framing_error = NULL;

    if (http_validate_request_framing(
            &request,
            &framing_error
        ) < 0) {
        *close_connection = 1;

        fprintf(
            stderr,
            "Unsupported request framing: %s\n",
            framing_error
        );

        return http_send_bad_request_message(
            client_fd,
            framing_error
        );
    }

    /*
     * HTTP/1.1 defaults to keep-alive.
     * HTTP/1.0 defaults to close.
     */
    if (strcmp(request.version, "HTTP/1.0") == 0) {
        *close_connection = 1;
    }

    const char *connection = http_get_header(
        &request,
        "Connection"
    );

    if (connection != NULL &&
        strcasecmp(connection, "close") == 0) {
        *close_connection = 1;
    }

    printf(
        "Parsed HTTP request:\n"
        "  Method:  %s\n"
        "  Path:    %s\n"
        "  Query:   %s\n"
        "  Version: %s\n",
        request.method,
        request.path,
        request.query,
        request.version
    );

    printf("Headers:\n");

    for (size_t i = 0; i < request.header_count; i++) {
        printf(
            "  %s: %s\n",
            request.headers[i].name,
            request.headers[i].value
        );
    }

    file_t file;

    if (file_open_path(root_fd, request.path, &file) < 0) {
        /*
         * Error responses close the connection.
         */
        *close_connection = 1;

        if (errno == ENOENT) {
            return http_send_error(
                client_fd,
                HTTP_STATUS_NOT_FOUND,
                0
            );
        }

        if (errno == EACCES || errno == EPERM) {
            return http_send_error(
                client_fd,
                HTTP_STATUS_FORBIDDEN,
                0
            );
        }

        return http_send_error(
            client_fd,
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            0
        );
    }

    printf(
        "Opened %s: fd=%d, size=%lld bytes\n",
        request.path,
        file.fd,
        (long long)file.size
    );

    const char *content_type = mime_type(request.path);

    if (content_type == NULL) {
        content_type = "application/octet-stream";
    }

    /*
     * HEAD sends the same headers as GET but no body.
     */
    int send_body = strcmp(request.method, "HEAD") != 0;

    if (http_send_response(
            client_fd,
            HTTP_STATUS_OK,
            content_type,
            file.size,
            !*close_connection
        ) < 0) {
        close(file.fd);
        return -1;
    }

    if (send_body && file_send(&file, client_fd) < 0) {
        close(file.fd);
        return -1;
    }

    close(file.fd);
    return 0;
}


int http_handle_request(
    int client_fd,
    int root_fd,
    const server_config_t *config)
{
    if (!http_config_valid(config)) {
        if (config == NULL) {
            fprintf(stderr, "Invalid HTTP configuration: config is NULL\n");
        } else {
            fprintf(
                stderr,
                "Invalid HTTP configuration:\n"
                "  buffer_size=%zu\n"
                "  max_headers=%zu (capacity=%d)\n"
                "  header_name_max=%zu (capacity=%d)\n"
                "  header_value_max=%zu (capacity=%d)\n",
                config->http_buffer_size,
                config->http_max_headers,
                HTTP_MAX_HEADERS,
                config->http_header_name_max,
                HTTP_HEADER_NAME_MAX,
                config->http_header_value_max,
                HTTP_HEADER_VALUE_MAX
            );
        }

        return -1;
    }

    char *buffer = malloc(config->http_buffer_size);

    if (buffer == NULL) {
        perror("malloc");
        return -1;
    }

    size_t buffer_used = 0;
    int close_connection = 0;
    int result = 0;

    while (!close_connection) {
        while (!http_request_complete(buffer, buffer_used)) {
            if (buffer_used >= config->http_buffer_size) {
                http_send_error(
                    client_fd,
                    HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE,
                    0
                );

                result = -1;
                goto cleanup;
            }

            ssize_t received = recv(
                client_fd,
                buffer + buffer_used,
                config->http_buffer_size - buffer_used,
                0
            );

            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }

                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    fprintf(stderr, "Client receive timeout\n");
                    result = -1;
                    goto cleanup;
                }

                perror("recv");
                result = -1;
                goto cleanup;
            }

            /*
             * Client closed the TCP connection.
             */
            if (received == 0) {
                result = 0;
                goto cleanup;
            }

            buffer_used += (size_t)received;
        }

        const char *header_end = http_find_header_end(
            buffer,
            buffer_used
        );

        if (header_end == NULL) {
            result = -1;
            goto cleanup;
        }

        size_t request_length = (size_t)(header_end - buffer);

        /*
         * Process exactly one request.
         */
        if (http_process_request(
                client_fd,
                buffer,
                request_length,
                &close_connection,
                root_fd,
                config
            ) < 0) {
            result = -1;
            goto cleanup;
        }

        /*
         * Preserve any pipelined request already received:
         *
         *     [request 1][request 2]
         *
         * becomes:
         *
         *     [request 2]
         */
        size_t remaining = buffer_used - request_length;

        if (remaining > 0) {
            memmove(
                buffer,
                buffer + request_length,
                remaining
            );
        }

        buffer_used = remaining;
    }

cleanup:
    free(buffer);
    return result;
}
