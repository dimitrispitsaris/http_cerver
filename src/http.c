#include "http.h"
#include "http_parse.h"
#include "http_response.h"
#include "file.h"
#include "mime.h"
#include "config.h"
#include "http_connection.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
                *reason =
                    "Duplicate Content-Length headers are not supported";
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
    const char *end =
        content_length + strlen(content_length);

    /*
     * Header parsing removes leading OWS but preserves trailing OWS.
     */
    while (end > start &&
           (end[-1] == ' ' || end[-1] == '\t')) {
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

        unsigned int digit =
            (unsigned int)(*p - '0');

        if (length >
            (ULLONG_MAX - digit) / 10) {
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


static int http_prepare_response(
    const char *buffer,
    size_t length,
    http_response_t *response,
    int *close_connection,
    int root_fd,
    const server_config_t *config)
{
    if (buffer == NULL ||
        response == NULL ||
        close_connection == NULL ||
        !http_config_valid(config)) {
        return -1;
    }

    http_response_init(response);

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
            fprintf(
                stderr,
                "HTTP request headers too large\n"
            );

            return http_response_prepare_error(
                response,
                HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE,
                NULL
            );
        }

        fprintf(
            stderr,
            "Invalid HTTP request\n"
        );

        return http_response_prepare_error(
            response,
            HTTP_STATUS_BAD_REQUEST,
            NULL
        );
    }

    if (!http_method_allowed(request.method)) {
        *close_connection = 1;

        return http_response_prepare_error(
            response,
            HTTP_STATUS_METHOD_NOT_ALLOWED,
            NULL
        );
    }

    /*
     * HTTP/1.1 requires exactly one Host header.
     */
    if (strcmp(request.version, "HTTP/1.1") == 0 &&
        http_validate_host(&request) < 0) {
        *close_connection = 1;

        return http_response_prepare_error(
            response,
            HTTP_STATUS_BAD_REQUEST,
            NULL
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

        return http_response_prepare_error(
            response,
            HTTP_STATUS_BAD_REQUEST,
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

    const char *connection =
        http_get_header(&request, "Connection");

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

    if (file_open_path(
            root_fd,
            request.path,
            &file
        ) < 0) {

        /*
         * Error responses close the connection.
         */
        *close_connection = 1;

        if (errno == ENOENT) {
            return http_response_prepare_error(
                response,
                HTTP_STATUS_NOT_FOUND,
                NULL
            );
        }

        if (errno == EACCES ||
            errno == EPERM) {
            return http_response_prepare_error(
                response,
                HTTP_STATUS_FORBIDDEN,
                NULL
            );
        }

        return http_response_prepare_error(
            response,
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            NULL
        );
    }

    printf(
        "Opened %s: fd=%d, size=%lld bytes\n",
        request.path,
        file.fd,
        (long long)file.size
    );

    const char *content_type =
        mime_type(request.path);

    if (content_type == NULL) {
        content_type = "application/octet-stream";
    }

    /*
     * HEAD sends the same headers as GET but no body.
     */
    int send_body =
        strcmp(request.method, "HEAD") != 0;

    int headers_length =
        http_build_response_headers(
            response->headers,
            sizeof(response->headers),
            HTTP_STATUS_OK,
            content_type,
            file.size,
            !*close_connection
        );

    if (headers_length < 0) {
        close(file.fd);
        return -1;
    }

    response->headers_length =
        (size_t)headers_length;

    response->headers_sent = 0;

    response->file_fd = file.fd;
    response->file_size = file.size;
    response->file_offset = 0;

    response->body = NULL;
    response->body_length = 0;
    response->body_sent = 0;

    response->send_body = send_body;

    /*
     * For HEAD there is no body to transmit.
     * For GET the file remains open in the response state.
     */
    response->complete = send_body ? 0 : 0;

    return 0;
}


int http_process_connection(
    http_connection_t *connection,
    int client_fd,
    int root_fd,
    const server_config_t *config)
{
    if (connection == NULL ||
        !http_config_valid(config)) {
        return -1;
    }

    while (http_connection_request_ready(connection)) {
        size_t request_length =
            http_connection_request_length(
                connection
            );

        if (request_length == 0) {
            return -1;
        }

        if (http_prepare_response(
                http_connection_data(connection),
                request_length,
                &connection->response,
                &connection->close_connection,
                root_fd,
                config
            ) < 0) {
            return -1;
        }

	if (http_response_send_blocking(client_fd,&connection->response)<0){
		return -1;
	}

	http_response_destroy(&connection->response);

        http_connection_consume(
            connection,
            request_length
        );

	if (connection->close_connection){
		break;
	}

        /*
         * The response has now been prepared.
         *
         * The transport layer will send it.
         *
         * For the current fork backend this will still
         * eventually be done synchronously.
         */
    }

    return 0;
}
