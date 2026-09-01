#include "file.h"
#include "mime.h"
#include "io.h"
#include "http.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "config.h"

#define HTTP_BUFFER_SIZE 16384


/* ============================================================
 * Method validation
 * ============================================================ */

static int http_method_allowed(const char *method)
{
    if (strcmp(method, "GET") == 0) {
        return 1;
    }

    if (strcmp(method, "HEAD") == 0) {
        return 1;
    }

    return 0;
}


/* ============================================================
 * Convert one hexadecimal character to its integer value
 *
 * Example:
 *
 *     'A' -> 10
 *     'f' -> 15
 *     '7' -> 7
 *
 * Returns -1 for an invalid hexadecimal character.
 * ============================================================ */

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


/* ============================================================
 * Percent-decode an HTTP path
 *
 * Example:
 *
 *     /hello%20world.html
 *
 * becomes:
 *
 *     /hello world.html
 *
 * Invalid encodings are rejected.
 *
 * %00 is rejected because it would introduce a NUL byte
 * into a C string.
 * ============================================================ */

static int http_decode_path(
    const char *encoded,
    char *decoded,
    size_t decoded_size)
{
    size_t src = 0;
    size_t dst = 0;

    while (encoded[src] != '\0') {

        if (dst + 1 >= decoded_size) {
            return -1;
        }

        /*
         * Percent-encoded byte.
         *
         * Example:
         *
         *     %20
         *
         *     %
         *     ^^
         *     hexadecimal digits
         */
        if (encoded[src] == '%') {

            /*
             * We need two characters after '%'.
             */
            if (encoded[src + 1] == '\0' ||
                encoded[src + 2] == '\0') {

                return -1;
            }

            int high =
                http_hex_value(encoded[src + 1]);

            int low =
                http_hex_value(encoded[src + 2]);

            if (high < 0 || low < 0) {
                return -1;
            }

            unsigned char value =
                (unsigned char)((high << 4) | low);

            /*
             * NUL cannot exist inside our C string.
             */
            if (value == '\0') {
                return -1;
            }

            /*
             * Reject ASCII control characters.
             */
            if (value < 32 || value == 127) {
                return -1;
            }

            decoded[dst++] = (char)value;

            src += 3;

        } else {

            unsigned char value =
                (unsigned char)encoded[src];

            /*
             * Reject control characters.
             */
            if (value < 32 || value == 127) {
                return -1;
            }

            decoded[dst++] =
                encoded[src++];

        }
    }

    decoded[dst] = '\0';

    return 0;
}
/* ============================================================
 * Parse HTTP request target
 *
 * Separates:
 *
 *     /index.html?name=dimitris
 *
 * into:
 *
 *     path  = /index.html
 *     query = name=dimitris
 *
 * Then percent-decodes the path.
 * ============================================================ */

static int http_parse_target(
    const char *target,
    http_request_t *request)
{
    char path[4096];

    /*
     * Find the beginning of the query string.
     */
    const char *query_start =
        strchr(target, '?');


    if (query_start != NULL) {

        /*
         * Copy only the path portion.
         */
        size_t path_length =
            (size_t)(query_start - target);

        if (path_length == 0 ||
            path_length >= sizeof(path)) {

            return -1;
        }

        memcpy(
            path,
            target,
            path_length
        );

        path[path_length] = '\0';


        /*
         * Skip '?'.
         */
        query_start++;


        /*
         * Copy query string.
         */
        size_t query_length =
            strlen(query_start);

        if (query_length >=
            sizeof(request->query)) {

            return -1;
        }

        memcpy(
            request->query,
            query_start,
            query_length + 1
        );

    } else {

        /*
         * No query string.
         */
        if (strlen(target) >=
            sizeof(path)) {

            return -1;
        }

        strcpy(
            path,
            target
        );

        request->query[0] = '\0';
    }


    /*
     * HTTP origin-form request targets must
     * begin with '/'.
     */
    if (path[0] != '/') {
        return -1;
    }


    /*
     * Decode the path BEFORE passing it
     * to the filesystem layer.
     */
    if (http_decode_path(
            path,
            request->path,
            sizeof(request->path)
        ) < 0) {

        return -1;
    }


    return 0;
}


/* ============================================================
 * Check whether a complete HTTP request header section exists
 *
 * HTTP headers end with:
 *
 *     \r\n\r\n
 * ============================================================ */

static int http_request_complete(
    const char *buffer,
    size_t length)
{
    if (length < 4) {
        return 0;
    }

    for (size_t i = 0; i <= length - 4; i++) {

        if (buffer[i]     == '\r' &&
            buffer[i + 1] == '\n' &&
            buffer[i + 2] == '\r' &&
            buffer[i + 3] == '\n') {

            return 1;
        }
    }

    return 0;
}


/* ============================================================
 * Find the end of the HTTP headers
 *
 * Returns a pointer to the first byte AFTER \r\n\r\n.
 * ============================================================ */

static const char *http_find_header_end(
    const char *buffer,
    size_t length)
{
    if (length < 4) {
        return NULL;
    }

    for (size_t i = 0; i <= length - 4; i++) {

        if (buffer[i]     == '\r' &&
            buffer[i + 1] == '\n' &&
            buffer[i + 2] == '\r' &&
            buffer[i + 3] == '\n') {

            return buffer + i + 4;
        }
    }

    return NULL;
}


/* ============================================================
 * Find an HTTP header
 * ============================================================ */

static const char *http_get_header(
    const http_request_t *request,
    const char *name)
{
    for (size_t i = 0; i < request->header_count; i++) {

        if (strcasecmp(
                request->headers[i].name,
                name
            ) == 0) {

            return request->headers[i].value;
        }
    }

    return NULL;
}


/* ============================================================
 * Parse HTTP headers
 * ============================================================ */

static int http_parse_headers(
    const char *buffer,
    http_request_t *request)
{
    const char *line = strstr(buffer, "\r\n");

    if (line == NULL) {
        return -1;
    }

    /*
     * Skip the request line.
     */
    line += 2;

    while (1) {

        /*
         * Empty line means end of headers.
         */
        if (line[0] == '\r' &&
            line[1] == '\n') {

            return 0;
        }

        /*
         * Prevent too many headers.
         */
        if (request->header_count >= HTTP_MAX_HEADERS) {
            return -1;
        }

        /*
         * Find end of current header line.
         */
        const char *line_end =
            strstr(line, "\r\n");

        if (line_end == NULL) {
            return -1;
        }

        /*
         * Find ':' separating header name and value.
         */
        const char *colon =
            memchr(
                line,
                ':',
                (size_t)(line_end - line)
            );

        if (colon == NULL) {
            return -1;
        }

        /*
         * Header name.
         */
        size_t name_length =
            (size_t)(colon - line);

        if (name_length == 0 ||
            name_length >= HTTP_HEADER_NAME_MAX) {

            return -1;
        }

        memcpy(
            request->headers[
                request->header_count
            ].name,
            line,
            name_length
        );

        request->headers[
            request->header_count
        ].name[name_length] = '\0';


        /*
         * Header value begins after ':'.
         */
        const char *value_start =
            colon + 1;

        /*
         * Ignore optional whitespace after ':'.
         */
        while (value_start < line_end &&
               (*value_start == ' ' ||
                *value_start == '\t')) {

            value_start++;
        }

        size_t value_length =
            (size_t)(line_end - value_start);

        if (value_length >= HTTP_HEADER_VALUE_MAX) {
            return -1;
        }

        memcpy(
            request->headers[
                request->header_count
            ].value,
            value_start,
            value_length
        );

        request->headers[
            request->header_count
        ].value[value_length] = '\0';


        request->header_count++;

        /*
         * Move to next header.
         */
        line = line_end + 2;
    }
}


/* ============================================================
 * HTTP status reason
 * ============================================================ */

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

        default:
            return "Unknown";
    }
}


/* ============================================================
 * Send HTTP response headers
 * ============================================================ */

int http_send_response(
    int client_fd,
    http_status_t status,
    const char *content_type,
    off_t content_length,
    int keep_alive)
{
    const char *reason =
        http_status_reason(status);

    if (reason == NULL) {
        return -1;
    }

    const char *connection =
        keep_alive ? "keep-alive" : "close";

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

    if (length < 0 ||
        (size_t)length >= sizeof(response)) {

        return -1;
    }

    ssize_t sent =
        io_send_all(
            client_fd,
            response,
            (size_t)length
        );

    if (sent < 0) {
        return -1;
    }

    if ((size_t)sent != (size_t)length) {

        fprintf(
            stderr,
            "Connection closed while sending response\n"
        );

        return -1;
    }

    return 0;
}


/* ============================================================
 * Send HTTP error response
 * ============================================================ */

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

    size_t body_length =
        strlen(body);


    if (http_send_response(
            client_fd,
            status,
            "text/html",
            (off_t)body_length,
            keep_alive
        ) < 0) {

        return -1;
    }


    ssize_t sent =
        io_send_all(
            client_fd,
            body,
            body_length
        );

    if (sent < 0) {
        return -1;
    }

    if ((size_t)sent != body_length) {

        fprintf(
            stderr,
            "Connection closed while sending error body\n"
        );

        return -1;
    }

    return 0;
}


/* ============================================================
 * Parse HTTP request
 * ============================================================ */

int http_parse_request(
    const char *buffer,
    http_request_t *request)
{
    request->header_count = 0;
    request->query[0] = '\0';

    /*
     * Parse:
     *
     * METHOD TARGET VERSION
     *
     * Example:
     *
     * GET /index.html?name=dimitris HTTP/1.1
     */
    char target[4096];

    int fields = sscanf(
        buffer,
        "%15s %4095s %15s",
        request->method,
        target,
        request->version
    );

    if (fields != 3) {
        return -1;
    }


    /*
     * Currently support HTTP/1.0 and HTTP/1.1.
     */
    if (strcmp(
            request->version,
            "HTTP/1.1"
        ) != 0 &&
        strcmp(
            request->version,
            "HTTP/1.0"
        ) != 0) {

        return -1;
    }


    /*
     * Separate query string from path.
     *
     * Example:
     *
     * /index.html?name=dimitris
     *
     * becomes:
     *
     * target       = "/index.html"
     * request->query = "name=dimitris"
     */
    
    if (http_parse_target(target,request)<0)
    {
	    return -1;
    }


    /*
     * Parse headers.
     */
    if (http_parse_headers(
            buffer,
            request
        ) < 0) {

        return -1;
    }

    return 0;
}


/* ============================================================
 * Handle ONE complete HTTP request
 * ============================================================ */

static int http_process_request(
    int client_fd,
    const char *buffer,
    size_t length,
    int *close_connection,
    int root_fd)
{
    /*
     * Make a temporary null-terminated copy because
     * the current parser uses sscanf()/strstr().
     */
    char request_buffer[HTTP_BUFFER_SIZE];

    if (length >= sizeof(request_buffer)) {

        *close_connection = 1;

        return http_send_error(
            client_fd,
            HTTP_STATUS_BAD_REQUEST,
            0
        );
    }

    memcpy(
        request_buffer,
        buffer,
        length
    );

    request_buffer[length] = '\0';


    printf(
        "Raw HTTP request:\n%s\n",
        request_buffer
    );


    /*
     * Parse request.
     */
    http_request_t request;

    if (http_parse_request(
            request_buffer,
            &request
        ) < 0) {

        fprintf(
            stderr,
            "Invalid HTTP request\n"
        );

        *close_connection = 1;

        return http_send_error(
            client_fd,
            HTTP_STATUS_BAD_REQUEST,
            0
        );
    }


    /*
     * Only GET and HEAD are supported.
     */
    if (!http_method_allowed(
            request.method
        )) {

        *close_connection = 1;

        return http_send_error(
            client_fd,
            HTTP_STATUS_METHOD_NOT_ALLOWED,
            0
        );
    }


    /*
     * HTTP/1.1 requires Host.
     */
    if (strcmp(
            request.version,
            "HTTP/1.1"
        ) == 0) {

        const char *host =
            http_get_header(
                &request,
                "Host"
            );

        if (host == NULL ||
            host[0] == '\0') {

            *close_connection = 1;

            return http_send_error(
                client_fd,
                HTTP_STATUS_BAD_REQUEST,
                0
            );
        }
    }


    /*
     * Check Connection header.
     *
     * HTTP/1.1 is persistent by default.
     *
     * Connection: close explicitly requests
     * that the connection be closed.
     */
    const char *connection =
        http_get_header(
            &request,
            "Connection"
        );

    if (connection != NULL &&
        strcasecmp(
            connection,
            "close"
        ) == 0) {

        *close_connection = 1;
    }


    /*
     * HTTP/1.0 is non-persistent by default.
     */
    if (strcmp(
            request.version,
            "HTTP/1.0"
        ) == 0) {

        *close_connection = 1;
    }


    int keep_alive =
        !(*close_connection);


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

    for (size_t i = 0;
         i < request.header_count;
         i++) {

        printf(
            "  %s: %s\n",
            request.headers[i].name,
            request.headers[i].value
        );
    }


    /*
     * Open requested file.
     *
     * file_open_path() now performs secure
     * filesystem resolution using openat2().
     */
    file_t file;

    if (file_open_path(
            root_fd,
            request.path,
            &file
        ) < 0) {

        if (errno == ENOENT) {

            return http_send_error(
                client_fd,
                HTTP_STATUS_NOT_FOUND,
                keep_alive
            );
        }

        if (errno == EACCES) {

            return http_send_error(
                client_fd,
                HTTP_STATUS_FORBIDDEN,
                keep_alive
            );
        }

        *close_connection = 1;

        return http_send_error(
            client_fd,
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            0
        );
    }


    printf(
        "Opened %s: fd=%d, size=%ld bytes\n",
        request.path,
        file.fd,
        (long)file.size
    );


    /*
     * Determine MIME type using the decoded path.
     */
    const char *content_type =
        mime_type(request.path);


    /*
     * Send HTTP response headers.
     */
    if (http_send_response(
            client_fd,
            HTTP_STATUS_OK,
            content_type,
            file.size,
            keep_alive
        ) < 0) {

        close(file.fd);
        return -1;
    }


    /*
     * HEAD sends headers but NOT the body.
     */
    if (strcmp(
            request.method,
            "GET"
        ) == 0) {

        if (file_send(
                &file,
                client_fd
            ) < 0) {

            close(file.fd);
            return -1;
        }
    }


    close(file.fd);

    return 0;
}


/* ============================================================
 * Handle HTTP connection
 *
 * One TCP connection may contain multiple HTTP requests.
 * ============================================================ */

int http_handle_request(
    int client_fd,
    int root_fd,
    const server_config_t *config)
{
    char buffer[HTTP_BUFFER_SIZE];

    size_t buffer_used = 0;

    int close_connection = 0;


    while (!close_connection) {

        /*
         * Receive data until we have a complete
         * HTTP request.
         */
        while (!http_request_complete(
                    buffer,
                    buffer_used)) {

            /*
             * Buffer completely full.
             */
            if (buffer_used >= sizeof(buffer)) {

                http_send_error(
                    client_fd,
                    HTTP_STATUS_BAD_REQUEST,
                    0
                );

                return -1;
            }


            ssize_t received = recv(
                client_fd,
                buffer + buffer_used,
                sizeof(buffer) - buffer_used,
                0
            );


            if (received < 0) {

                if (errno == EINTR) {
                    continue;
                }

                if (errno == EAGAIN ||
                    errno == EWOULDBLOCK) {

                    fprintf(
                        stderr,
                        "Client receive timeout\n"
                    );

                    return -1;
                }

                perror("recv");
                return -1;
            }


            /*
             * Client closed TCP connection.
             */
            if (received == 0) {
                return 0;
            }


            buffer_used +=
                (size_t)received;
        }


        /*
         * Locate end of HTTP headers.
         */
        const char *header_end =
            http_find_header_end(
                buffer,
                buffer_used
            );

        if (header_end == NULL) {
            return -1;
        }


        /*
         * GET and HEAD currently have no request body.
         */
        size_t request_length =
            (size_t)(header_end - buffer);


        /*
         * Process exactly ONE request.
         */
        if (http_process_request(
                client_fd,
                buffer,
                request_length,
                &close_connection,
		root_fd) < 0) {

            return -1;
        }


        /*
         * Remove processed request from buffer.
         *
         * This is what allows pipelined requests:
         *
         * [request 1][request 2]
         *
         * after request 1:
         *
         * [request 2]
         */
        size_t remaining =
            buffer_used - request_length;


        if (remaining > 0) {

            memmove(
                buffer,
                buffer + request_length,
                remaining
            );
        }


        buffer_used = remaining;
    }


    return 0;
}
