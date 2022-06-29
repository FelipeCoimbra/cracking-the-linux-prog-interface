#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#include "q7.h"
#include "../shared/errors.h"
#include "../shared/utils.h"

ssize_t __writev(int fd, const struct iovec* iov, size_t iovcnt);
ssize_t __readv(int fd, const struct iovec * iov, size_t iovcnt);

void chpt5_q7() {
    char tmp_file_path_template[] = "/tmp/cracking-the-linux-prog-interface-XXXXXX";
    int tmp_fd = mkstemp(tmp_file_path_template);
    const struct iovec data[] = {
        {
            .iov_base = "Hello",
            .iov_len = 5
        },
        {
            .iov_base = " ",
            .iov_len = 1
        },
        {
            .iov_base = "world!",
            .iov_len = 6
        }
    };
    const struct iovec read_result[] = { // Ignore memory leak below, plz
        {
            .iov_base = malloc(10),
            .iov_len = 3
        },
        {
            .iov_base = malloc(10),
            .iov_len = 4
        },
        {
            .iov_base = malloc(10),
            .iov_len = 5
        }
    };

    //
    // writev
    //
    assert(__writev(tmp_fd, NULL, -1) == -1 && errno == EINVAL);
    assert(__writev(tmp_fd, NULL, UIO_MAXIOV+1) == -1 && errno == EINVAL);
    assert(__writev(-1, data, 3) == -1 && errno == EBADF);
    assert(__writev(tmp_fd, data, 0) == 0);
    assert(__writev(tmp_fd, data, 3) == 12);

    if (lseek(tmp_fd, 0, SEEK_SET) == -1) {
        errExit("lseek");
    }

    //
    // readv
    //
    assert(__readv(tmp_fd, NULL, -1) == -1 && errno == EINVAL);
    assert(__readv(tmp_fd, NULL, UIO_MAXIOV+1) == -1 && errno == EINVAL);
    assert(__readv(-1, read_result, 3) == -1 && errno == EBADF);
    assert(__readv(tmp_fd, read_result, 0) == 0);
    assert(__readv(tmp_fd, read_result, 3) == 12);
    assert(strncmp(read_result[0].iov_base, "Hel", 3) == 0);
    assert(strncmp(read_result[1].iov_base, "lo w", 4) == 0);
    assert(strncmp(read_result[2].iov_base, "orld!", 5) == 0);


    safe_close(tmp_fd);
}

ssize_t __writev(int fd, const struct iovec* iov, size_t iovcnt) {
     if (iovcnt < 0 || iovcnt > UIO_MAXIOV) {
        errno = EINVAL;
        return -1;
    }
    ssize_t total_to_write = 0;
    for (int i=0; i<iovcnt; i++) {
        if (total_to_write > SSIZE_MAX - iov[i].iov_len) {
            // Sum of iov_len overflows ssize_t
            errno = EINVAL;
            return -1;
        }
        total_to_write += iov[i].iov_len;
    }

    void * tmp_buffer = malloc(total_to_write);
    if (tmp_buffer == NULL) {
        // Fatal error at memory allocation.
        errExit("malloc");
    }

    ssize_t total_copied = 0;
    for (int i=0; i<iovcnt; i++) {
        strncpy(tmp_buffer + total_copied, iov[i].iov_base, iov[i].iov_len);
        total_copied += iov[i].iov_len;
    }

    ssize_t total_written = write(fd, tmp_buffer, total_to_write);
    if (total_written == -1) {
        // Pass write to error to caller
        return -1;
    }

    free(tmp_buffer);

    return total_written;
}

ssize_t __readv(int fd, const struct iovec * iov, size_t iovcnt) {
    if (iovcnt < 0 || iovcnt > UIO_MAXIOV) {
        errno = EINVAL;
        return -1;
    }

    ssize_t total_to_read = 0;
    for (int i=0; i<iovcnt; i++) {
        if (total_to_read > SSIZE_MAX - iov[i].iov_len) {
            // Sum of iov_len overflows ssize_t
            errno = EINVAL;
            return -1;
        }
        total_to_read += iov[i].iov_len;
    }
    
    void * tmp_buffer = malloc(total_to_read);
    if (tmp_buffer == NULL) {
        // Fatal error at memory allocation.
        errExit("malloc");
    }

    ssize_t total_read = read(fd, tmp_buffer, total_to_read);
    if (total_read == -1) {
        // Pass read error to caller
        return -1;
    }

    ssize_t total_to_copy = total_read;
    for (int i=0; i<iovcnt && total_to_copy > 0; i++) {
        ssize_t iov_elem_share =  min(total_to_copy, (ssize_t) iov[i].iov_len);
        strncpy(iov[i].iov_base, tmp_buffer + total_read-total_to_copy, iov_elem_share);
        total_to_copy -= iov_elem_share;
    }

    free(tmp_buffer);

    return total_read;
}
