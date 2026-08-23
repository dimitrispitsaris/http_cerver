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

#define DOCUMENT_ROOT "public"

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
        const char *line_end = strstr(
            line,
            "\r\n"
        );

        if (line_end == NULL) {
            return -1;
        }

        /*
         * Find ':' separating header name and value.
         */
        const char *colon = memchr(
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
        const char *value_start = colon + 1;

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

    ssize_t sent = io_send_all(
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


    ssize_t sent = io_send_all(
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

    /*
     * Parse:
     *
     * METHOD PATH VERSION
     */
    int fields = sscanf(
        buffer,
        "%15s %4095s %15s",
        request->method,
        request->path,
        request->version
    );

    if (fields != 3) {
        return -1;
    }


    /*
     * Currently support HTTP/1.0 and HTTP/1.1.
     */
    if (strcmp(request->version, "HTTP/1.1") != 0 &&
        strcmp(request->version, "HTTP/1.0") != 0) {

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
    int *close_connection)
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


    /*
     * Determine response connection behavior.
     */
    int keep_alive =
        !(*close_connection);


    printf(
        "Parsed HTTP request:\n"
        "  Method:  %s\n"
        "  Path:    %s\n"
        "  Version: %s\n",
        request.method,
        request.path,
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
     */
    file_t file;

    if (file_open_path(
            DOCUMENT_ROOT,
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
     * Determine MIME type.
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
     * HEAD sends the headers but NOT the body.
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


    /*
     * We are finished with the file.
     */
    close(file.fd);

    return 0;
}


/* ============================================================
 * Handle HTTP connection
 *
 * One TCP connection may contain multiple HTTP requests.
 * ============================================================ */

int http_handle_request(int client_fd)
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

                perror("recv");
                return -1;
            }


            /*
             * Client closed TCP connection.
             */
            if (received == 0) {
                return 0;
            }


            buffer_used += (size_t)received;
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
         * Number of bytes belonging to this request.
         *
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
                &close_connection
            ) < 0) {

            return -1;
        }


        /*
         * Remove processed request from buffer.
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

