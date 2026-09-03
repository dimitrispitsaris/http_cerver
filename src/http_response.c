#include "http_response.h"
#include "io.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>


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
