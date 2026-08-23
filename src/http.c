#include "http.h"
#include "file.h"
#include "mime.h"
#include "io.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>
#include <strings.h>
#define DOCUMENT_ROOT "public"






//Method validator function
static int http_method_allowed(const char *method)
{
	if (strcmp(method,"GET")==0)
	{return 1;}

	if (strcmp(method,"HEAD")==0)
	{return 1;}

	return 0;
}


//Helper function to see if we get all headers from the TCP stream
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

        if (request->header_count >= HTTP_MAX_HEADERS) {
            return -1;
        }

        const char *line_end = strstr(
            line,
            "\r\n"
        );

        if (line_end == NULL) {
            return -1;
        }

        const char *colon = memchr(
            line,
            ':',
            line_end - line
        );

        if (colon == NULL) {
            return -1;
        }

        size_t name_length = colon - line;

        if (name_length == 0 ||
            name_length >= HTTP_HEADER_NAME_MAX) {

            return -1;
        }

        memcpy(
            request->headers[request->header_count].name,
            line,
            name_length
        );

        request->headers[
            request->header_count
        ].name[name_length] = '\0';


        const char *value_start = colon + 1;

        while (value_start < line_end &&
               (*value_start == ' ' ||
                *value_start == '\t')) {

            value_start++;
        }

        size_t value_length =
            line_end - value_start;

        if (value_length >= HTTP_HEADER_VALUE_MAX) {
            return -1;
        }

        memcpy(
            request->headers[request->header_count].value,
            value_start,
            value_length
        );

        request->headers[
            request->header_count
        ].value[value_length] = '\0';


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

        default:
            return "Unknown";
    }
}

int http_send_response(
    int client_fd,
    http_status_t status,
    const char *content_type,
    off_t content_length
)
{
    const char *reason = http_status_reason(status);

    if (reason == NULL) {
        return -1;
    }

    char response[1024];

    int length = snprintf(
        response,
        sizeof(response),

        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lld\r\n"
        "Connection: close\r\n"
        "\r\n",

        status,
        reason,
        content_type,
        (long long)content_length
    );

    if (length < 0 ||
        (size_t)length >= sizeof(response)) {

        return -1;
    }

    ssize_t sent = io_send_all(
        client_fd,
        response,
        (size_t)length);

    if (sent < 0) {
        perror("send");
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
int http_send_error(int client_fd, http_status_t status)
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

    size_t body_length = strlen(body);

    if (http_send_response(
            client_fd,
            status,
            "text/html",
            body_length
        ) < 0) {

        return -1;
    }
    ssize_t sent = io_send_all(
    client_fd,
    body,
    body_length);

    if (sent < 0) {
	perror("send");
        return -1;
    }

    if ((size_t)sent != body_length) {
        fprintf(
           stderr,
            "Connection closed while sending error body\n");
       return -1;
    }

    return 0;
}


int http_parse_request(
    const char *buffer,
    http_request_t *request)
{
    request->header_count=0;


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

    if (strcmp(request->version, "HTTP/1.1") != 0 &&
        strcmp(request->version, "HTTP/1.0") != 0) {

        return -1;
    }
    if (http_parse_headers(buffer,request)<0){
       return -1;
    }
    return 0;
}


int http_handle_request(int client_fd)
{
    char recv_buff[10000];

    size_t total_bytes = 0;

    memset(
        recv_buff,
        0,
        sizeof(recv_buff)
    );

    while (!http_request_complete(
        recv_buff,
        total_bytes )) {

        if (total_bytes >= sizeof(recv_buff) - 1) {
            fprintf(stderr, "HTTP request too large\n");

            return http_send_error(
                client_fd,
                HTTP_STATUS_BAD_REQUEST);
        }

        ssize_t recv_bytes = recv(
            client_fd,
            recv_buff + total_bytes,
            sizeof(recv_buff) - 1 - total_bytes,
            0);

        if (recv_bytes < 0) {

            if (errno == EINTR) {
                continue;
            }

            perror("recv");
            return -1;
        }

        if (recv_bytes == 0) {
            return 0;
        }

        total_bytes += recv_bytes;

        recv_buff[total_bytes] = '\0';
    }

    printf(
        "Raw HTTP request:\n%s\n",
        recv_buff);

    http_request_t request;

    if (http_parse_request(
            recv_buff,
            &request
        ) < 0) {

        fprintf(stderr, "Invalid HTTP request\n");

        return http_send_error(
            client_fd,
            HTTP_STATUS_BAD_REQUEST
        );
    }

    if (!http_method_allowed(request.method))
	{return http_send_error(client_fd,HTTP_STATUS_METHOD_NOT_ALLOWED);}

    if (strcmp(request.version, "HTTP/1.1") == 0) {

    const char *host =
        http_get_header(&request, "Host");

       if (host == NULL || host[0] == '\0') {

           return http_send_error(
                client_fd,
                HTTP_STATUS_BAD_REQUEST );
         }
     }

    printf("Parsed HTTP request:\n");
    printf("  Method: %s\n", request.method);
    printf("  Path:   %s\n", request.path);
    printf("  Version: %s\n", request.version);

    printf("Parsed HTTP request:\n");
    printf("  Method:  %s\n", request.method);
    printf("  Path:    %s\n", request.path);
    printf("  Version: %s\n", request.version);
    printf("Headers:\n");

    for (size_t i = 0; i < request.header_count; i++) {

	    printf(
	        "  %s: %s\n",
	        request.headers[i].name,
	        request.headers[i].value );
	}

    file_t file;

    if (file_open_path(
            DOCUMENT_ROOT,
            request.path,
            &file
        ) < 0) {

        if (errno == ENOENT) {

            return http_send_error(
                client_fd,
                HTTP_STATUS_NOT_FOUND
            );
        }

        if (errno == EACCES) {

            return http_send_error(
                client_fd,
                HTTP_STATUS_FORBIDDEN
            );
        }

        return http_send_error(
            client_fd,
            HTTP_STATUS_INTERNAL_SERVER_ERROR
        );
    }


    printf(
        "Opened %s: fd=%d, size=%ld bytes\n",
        request.path,
        file.fd,
        (long)file.size
    );


    const char *content_type =
        mime_type(request.path);


    if (http_send_response(
            client_fd,
            HTTP_STATUS_OK,
            content_type,
            file.size
        ) < 0) {

        close(file.fd);
        return -1;
    }


    if (file_send(
            &file,
            client_fd
        ) < 0) {

        close(file.fd);
        return -1;
    }


    close(file.fd);

    return 0;
}
