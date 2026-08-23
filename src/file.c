#include "file.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>


int file_open(const char *path, file_t *file)
{
    file->fd = open(path, O_RDONLY);

    if (file->fd < 0) {
        perror("open");
        return -1;
    }

    struct stat st;

    if (fstat(file->fd, &st) < 0) {
        perror("fstat");
        close(file->fd);
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        fprintf(
            stderr,
            "%s is not a regular file\n",
            path
        );

        close(file->fd);
        errno = EACCES;

        return -1;
    }

    file->size = st.st_size;

    return 0;
}


int file_open_path(
    const char *document_root,
    const char *request_path,
    file_t *file
)
{
    if (request_path[0] != '/') {
        errno = EINVAL;
        return -1;
    }

    if (strstr(request_path, "..") != NULL) {
        errno = EACCES;
        return -1;
    }

    char path[PATH_MAX];

    int written = snprintf(
        path,
        sizeof(path),
        "%s%s",
        document_root,
        request_path
    );

    if (written < 0) {
        errno = EINVAL;
        return -1;
    }

    if ((size_t)written >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return file_open(path, file);
}


ssize_t file_send(
    const file_t *file,
    int socket_fd
)
{
    off_t offset = 0;
    ssize_t total_sent = 0;

    while (total_sent < file->size) {

        ssize_t sent = sendfile(
            socket_fd,
            file->fd,
            &offset,
            file->size - total_sent
        );

        if (sent < 0) {
            perror("sendfile");
            return -1;
        }

        if (sent == 0) {
            fprintf(
                stderr,
                "sendfile transferred 0 bytes unexpectedly\n"
            );
            return -1;
        }

        total_sent += sent;
    }

    return total_sent;
}
