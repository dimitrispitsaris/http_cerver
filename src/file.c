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

#define DEFAULT_INDEX_FILE "index.html"

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
       

	int saved_errno=errno;
	close(root_fd);

        if (saved_errno == EXDEV ||
            saved_errno == ELOOP) {

            errno = EACCES;
        }
	else{
		errno=saved_errno;
	}

        return -1;
    }


    /*
     * Verify that the resulting object is a regular file.
     */
    struct stat st;

    if (fstat(
            fd,
            &st
        ) < 0) {
	int saved_errno=errno;
	close(fd);
        close(root_fd);

	errno=saved_errno;

        return -1;
    }


    if (S_ISREG(st.st_mode)) {

        file->fd=fd;

	file->size=st.st_size;

        close(root_fd);


        return 0;
    }


 /*
     * Directory:
     *
     * Attempt to open index.html inside it.
     */


    if (S_ISDIR(st.st_mode)) {

        /*
         * The opened directory FD becomes the base
         * directory for index.html.
         */
        int index_fd =
            syscall(
                SYS_openat2,
                fd,
                DEFAULT_INDEX_FILE,
                &how,
                sizeof(how)
            );


        int saved_errno = errno;

        close(fd);
        close(root_fd);

        errno = saved_errno;


        if (index_fd < 0) {

            if (errno == EXDEV ||
                errno == ELOOP) {

                errno = EACCES;
            }

            return -1;
        }


        /*
         * Verify that index.html is a regular file.
         */
        if (fstat(
                index_fd,
                &st
            ) < 0) {

            int index_errno = errno;

            close(index_fd);

            errno = index_errno;

            return -1;
        }


        if (!S_ISREG(st.st_mode)) {

            close(index_fd);

            errno = EACCES;

            return -1;
        }


        file->fd =index_fd;

        file->size = st.st_size;

        return 0;
    }


    /*
     * We don't serve other filesystem object types.
     */

    close(fd);
    close(root_fd);

    errno = EACCES;

    return -1;
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
