#include "mime.h"

#include <string.h>

const char *mime_type(const char *path)
{
    const char *extension = strrchr(path, '.');

    if (extension == NULL) {
        return "application/octet-stream";
    }

    if (strcmp(extension, ".html") == 0 ||
        strcmp(extension, ".htm") == 0) {
        return "text/html";
    }

    if (strcmp(extension, ".css") == 0) {
        return "text/css";
    }

    if (strcmp(extension, ".js") == 0) {
        return "application/javascript";
    }

    if (strcmp(extension, ".json") == 0) {
        return "application/json";
    }

    if (strcmp(extension, ".png") == 0) {
        return "image/png";
    }

    if (strcmp(extension, ".jpg") == 0 ||
        strcmp(extension, ".jpeg") == 0) {
        return "image/jpeg";
    }

    if (strcmp(extension, ".gif") == 0) {
        return "image/gif";
    }

    if (strcmp(extension, ".svg") == 0) {
        return "image/svg+xml";
    }

    if (strcmp(extension, ".txt") == 0) {
        return "text/plain";
    }

    return "application/octet-stream";
}

