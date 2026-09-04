#include "http_response.h"
#include "io.h"
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/sendfile.h>
#include <sys/socket.h>

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

int http_build_response_headers(
    char *buffer,
    size_t capacity,
    http_status_t status,
    const char *content_type,
    off_t content_length,
    int keep_alive)
{
    if (buffer == NULL ||
        capacity == 0 ||
        content_type == NULL ||
        content_length < 0) {
        return -1;
    }

    const char *reason =
        http_status_reason(status);

    if (reason == NULL) {
        return -1;
    }

    const char *connection =
        keep_alive ? "keep-alive" : "close";

    int length = snprintf(
        buffer,
        capacity,
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
        (size_t)length >= capacity) {
        return -1;
    }

    return length;
}

int http_send_response(
    int client_fd,
    http_status_t status,
    const char *content_type,
    off_t content_length,
    int keep_alive)
{
    char response[1024];

    int length =
        http_build_response_headers(
            response,
            sizeof(response),
            status,
            content_type,
            content_length,
            keep_alive
        );

    if (length < 0) {
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

int http_send_bad_request_message(
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


void http_response_init(
    http_response_t *response)
{
    if (response == NULL) {
        return;
    }

    memset(response, 0, sizeof(*response));

    response->file_fd = -1;
}

void http_response_destroy(
    http_response_t *response)
{
    if (response == NULL) {
        return;
    }

    if (response->file_fd >= 0) {
        close(response->file_fd);
        response->file_fd = -1;
    }

    free(response->body);
    response->body = NULL;

    response->headers_length = 0;
    response->headers_sent = 0;

    response->body_length = 0;
    response->body_sent = 0;

    response->file_size = 0;
    response->file_offset = 0;

    response->send_body = 0;
    response->complete = 0;
}

int http_response_set_body(
    http_response_t *response,
    const char *body,
    size_t body_length)
{
    if (response == NULL ||
        body == NULL) {
        return -1;
    }

    char *copy = malloc(body_length);

    if (copy == NULL) {
        return -1;
    }

    memcpy(copy, body, body_length);

    free(response->body);

    response->body = copy;
    response->body_length = body_length;
    response->body_sent = 0;

    return 0;
}

int http_response_prepare_error(
    http_response_t *response,
    http_status_t status,
    const char *message)
{
    if (response == NULL) {
        return -1;
    }

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

    if (message != NULL) {
        body = message;
    }

    size_t body_length = strlen(body);

    if (http_response_set_body(
            response,
            body,
            body_length
        ) < 0) {
        return -1;
    }

    int headers_length =
        http_build_response_headers(
            response->headers,
            sizeof(response->headers),
            status,
            "text/html",
            (off_t)body_length,
            0
        );

    if (headers_length < 0) {
        return -1;
    }

    response->headers_length = (size_t)headers_length;
    response->headers_sent = 0;
    response->send_body = 1;
    response->complete = 0;

    return 0;
}

int http_response_send_blocking(
    int client_fd,
    http_response_t *response)
{
    if (response == NULL ||
        response->complete) {
        return -1;
    }

    /*
     * Send response headers.
     */
    while (response->headers_sent <
           response->headers_length) {
        ssize_t sent = send(
            client_fd,
            response->headers +
                response->headers_sent,
            response->headers_length -
                response->headers_sent,
            0
        );

        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("send");
            return -1;
        }

        if (sent == 0) {
            return -1;
        }

        response->headers_sent +=
            (size_t)sent;
    }

    /*
     * HEAD responses contain headers only.
     */
    if (!response->send_body) {
        response->complete = 1;
        return 0;
    }

    /*
     * Send an in-memory response body.
     */
    while (response->body_sent <
           response->body_length) {
        ssize_t sent = send(
            client_fd,
            response->body +
                response->body_sent,
            response->body_length -
                response->body_sent,
            0
        );

        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("send");
            return -1;
        }

        if (sent == 0) {
            return -1;
        }

        response->body_sent +=
            (size_t)sent;
    }

    /*
     * Send a static file.
     */
    if (response->file_fd >= 0) {
        while (response->file_offset <
               response->file_size) {
            off_t remaining =
                response->file_size -
                response->file_offset;

            ssize_t sent = sendfile(
                client_fd,
                response->file_fd,
                &response->file_offset,
                remaining
            );

            if (sent < 0) {
                if (errno == EINTR) {
                    continue;
                }

                perror("sendfile");
                return -1;
            }

            if (sent == 0) {
                return -1;
            }
        }
    }

    response->complete = 1;

    return 0;
}
