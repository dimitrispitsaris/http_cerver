#include "io.h"
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>

ssize_t io_send_all(
    int fd,
    const void *buffer,
    size_t length)
{
    const char *data = buffer;
    size_t total_sent = 0;

    while (total_sent < length) {

        ssize_t sent = send(
            fd,
            data + total_sent,
            length - total_sent,
            0);

        if (sent < 0) {

            if (errno == EINTR) {
                continue;  //Continue if sys interrupt
            }

            perror("send");
            return -1;
        }

        if (sent == 0) {
            break;
        }

        total_sent += (size_t)sent;
    }

    return (ssize_t)total_sent;
}
