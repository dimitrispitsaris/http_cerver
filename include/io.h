#ifndef IO_H
#define IO_H

#include <stddef.h>
#include <sys/types.h>

ssize_t io_send_all(
    int fd,
    const void *buffer,
    size_t length
);

#endif
