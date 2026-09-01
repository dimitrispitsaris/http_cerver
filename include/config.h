#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#define DEFAULT_PORT 8080
#define DEFAULT_BACKLOG 10
#define DEFAULT_CLIENT_TIMEOUT 10
#define DEFAULT_DOCUMENT_ROOT "public"

#define DEFAULT_HTTP_BUFFER_SIZE 16384
#define DEFAULT_HTTP_MAX_HEADERS 64
#define DEFAULT_HTTP_HEADER_NAME_MAX 128
#define DEFAULT_HTTP_HEADER_VALUE_MAX 4096

typedef struct {
    int port;
    int backlog;
    int client_timeout;

    const char *document_root;

    size_t http_buffer_size;
    size_t http_max_headers;
    size_t http_header_name_max;
    size_t http_header_value_max;

} server_config_t;

void config_init(server_config_t *config);

int config_parse_args(server_config_t *config,int argc, char **argv);
#endif
