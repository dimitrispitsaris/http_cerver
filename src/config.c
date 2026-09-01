#include "config.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>


static int parse_positive_int(const char *value,int *result){
	
	char *end=NULL;
	
	 errno=0;
	
	long parsed=strtol(value,&end,10);

	if (errno !=0 || end==value || *end !='\0' || parsed <= 0 || parsed >= INT_MAX){
		return -1;
	}

	*result=(int)parsed;

	return 0;
}


static int parse_positive_size(const char *value,size_t *result){
	char *end=NULL;

	errno=0;

	unsigned long long parsed= strtoull(value,&end,10);

	if (errno !=0 || end==value || *end != '\0' || parsed <= 0 || parsed >= INT_MAX){
		return -1;
	}

	*result=(size_t)parsed;
	return 0;

}



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


int config_parse_args(
    server_config_t *config,
    int argc,
    char **argv)
{
    for (int i = 1; i < argc; i++) {

        if (strcmp(argv[i], "--port") == 0) {

            if (i + 1 >= argc ||
                parse_positive_int(
                    argv[++i],
                    &config->port
                ) < 0) {

                fprintf(
                    stderr,
                    "Invalid value for --port\n"
                );

                return -1;
            }

            if (config->port > 65535) {
                fprintf(
                    stderr,
                    "Port must be between 1 and 65535\n"
                );

                return -1;
            }

        } else if (
            strcmp(argv[i], "--backlog") == 0) {

            if (i + 1 >= argc ||
                parse_positive_int(
                    argv[++i],
                    &config->backlog
                ) < 0) {

                fprintf(
                    stderr,
                    "Invalid value for --backlog\n"
                );

                return -1;
            }

        } else if (
            strcmp(argv[i], "--timeout") == 0) {

            if (i + 1 >= argc ||
                parse_positive_int(
                    argv[++i],
                    &config->client_timeout
                ) < 0) {

                fprintf(
                    stderr,
                    "Invalid value for --timeout\n"
                );

                return -1;
            }

        } else if (
            strcmp(argv[i], "--root") == 0) {

            if (i + 1 >= argc) {
                fprintf(
                    stderr,
                    "Missing value for --root\n"
                );

                return -1;
            }

            config->document_root = argv[++i];

        } else if (
            strcmp(argv[i], "--http-buffer-size") == 0) {

            if (i + 1 >= argc ||
                parse_positive_size(
                    argv[++i],
                    &config->http_buffer_size
                ) < 0) {

                fprintf(
                    stderr,
                    "Invalid value for --http-buffer-size\n"
                );

                return -1;
            }

        } else if (
            strcmp(argv[i], "--max-headers") == 0) {

            if (i + 1 >= argc ||
                parse_positive_size(
                    argv[++i],
                    &config->http_max_headers
                ) < 0) {

                fprintf(
                    stderr,
                    "Invalid value for --max-headers\n"
                );

                return -1;
            }

        } else if (
            strcmp(argv[i], "--header-name-max") == 0) {

            if (i + 1 >= argc ||
                parse_positive_size(
                    argv[++i],
                    &config->http_header_name_max
                ) < 0) {

                fprintf(
                    stderr,
                    "Invalid value for --header-name-max\n"
                );

                return -1;
            }

        } else if (
            strcmp(argv[i], "--header-value-max") == 0) {

            if (i + 1 >= argc ||
                parse_positive_size(
                    argv[++i],
                    &config->http_header_value_max
                ) < 0) {

                fprintf(
                    stderr,
                    "Invalid value for --header-value-max\n"
                );

                return -1;
            }

        } else {

            fprintf(
                stderr,
                "Unknown option: %s\n",
                argv[i]
            );

            return -1;
        }
    }

    return 0;
}
