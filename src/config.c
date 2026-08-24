#include "config.h"

void config_init(server_config_t *config)
{
    config->port = DEFAULT_PORT;
    config->backlog = DEFAULT_BACKLOG;

    config->client_timeout = DEFAULT_CLIENT_TIMEOUT;

    config->document_root = DEFAULT_DOCUMENT_ROOT;

    config->http_buffer_size =
        DEFAULT_HTTP_BUFFER_SIZE;

    config->http_max_headers =
        DEFAULT_HTTP_MAX_HEADERS;

    config->http_header_name_max =
        DEFAULT_HTTP_HEADER_NAME_MAX;

    config->http_header_value_max =
        DEFAULT_HTTP_HEADER_VALUE_MAX;
}
