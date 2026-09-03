#ifndef FILE_H
#define FILE_H

#include <sys/types.h>

typedef struct {
    int fd;
    off_t size;
} file_t;


int file_open_path(
    int root_fd,
    const char *request_path,
    file_t *file
);

ssize_t file_send(
    const file_t *file,
    int socket_fd
);

#endif
