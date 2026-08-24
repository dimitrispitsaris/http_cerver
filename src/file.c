#define _GNU_SOURCE

#include "file.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <linux/openat2.h>
#include <stdio.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
/* ============================================================
 * Open a regular file using an existing filesystem path
 * ============================================================ */

int file_open(
    const char *path,
    file_t *file)
{
    file->fd =
        open(
            path,
            O_RDONLY | O_CLOEXEC
        );

    if (file->fd < 0) {
        return -1;
    }

    struct stat st;

    if (fstat(
            file->fd,
            &st
        ) < 0) {

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


    file->size =
        st.st_size;

    return 0;
}


/* ============================================================
 * Open requested HTTP path safely beneath document root
 *
 * openat2() is used so the kernel performs path resolution
 * relative to the document root.
 *
 * RESOLVE_BENEATH:
 *
 *     prevents escaping the document root with ".."
 *
 * RESOLVE_NO_SYMLINKS:
 *
 *     prevents symbolic links from escaping the document root
 * ============================================================ */

int file_open_path(
    const char *document_root,
    const char *request_path,
    file_t *file)
{
    /*
     * HTTP paths must begin with '/'.
     */
    if (request_path[0] != '/') {

        errno = EINVAL;

        return -1;
    }


    /*
     * Open the document root itself.
     */
    int root_fd =
        open(
            document_root,
            O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC
        );

    if (root_fd < 0) {
        return -1;
    }


    /*
     * openat2() receives a path relative to root_fd.
     *
     * HTTP:
     *
     *     /index.html
     *
     * becomes:
     *
     *     index.html
     */
    const char *relative_path =
        request_path;

    while (*relative_path == '/') {
        relative_path++;
    }


    /*
     * Empty path means the document root itself.
     *
     * This will subsequently fail the regular-file check,
     * which currently gives 403.
     */
    if (*relative_path == '\0') {
        relative_path = ".";
    }


    struct open_how how = {
        .flags =
            O_RDONLY | O_CLOEXEC,

        .resolve =
            RESOLVE_BENEATH |
            RESOLVE_NO_SYMLINKS
    };


    int fd =
        syscall(
            SYS_openat2,
            root_fd,
            relative_path,
            &how,
            sizeof(how)
        );


    int saved_errno = errno;

    close(root_fd);

    errno = saved_errno;


    if (fd < 0) {

        /*
         * openat2() can report EXDEV when the path attempts
         * to escape beneath the root.
         *
         * ELOOP can occur because we explicitly reject
         * symbolic links.
         *
         * Treat both as forbidden.
         */
        if (errno == EXDEV ||
            errno == ELOOP) {

            errno = EACCES;
        }

        return -1;
    }


    file->fd = fd;


    /*
     * Verify that the resulting object is a regular file.
     */
    struct stat st;

    if (fstat(
            file->fd,
            &st
        ) < 0) {

        close(file->fd);

        return -1;
    }


    if (!S_ISREG(st.st_mode)) {

        close(file->fd);

        errno = EACCES;

        return -1;
    }


    file->size =
        st.st_size;

    return 0;
}


/* ============================================================
 * Send file through socket
 * ============================================================ */

ssize_t file_send(
    const file_t *file,
    int socket_fd)
{
    off_t offset = 0;

    ssize_t total_sent = 0;


    while (total_sent < file->size) {

        ssize_t sent =
            sendfile(
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
