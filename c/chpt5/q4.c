#define _GNU_SOURCE /* For the O_PATH flag */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "../shared/errors.h"
#include "q4.h"

/**
 * My implementation of the dup(2) syscall.
 */
int __dup(int old_fd);
/**
 * My implementation of the dup2(2) syscall.
 */
int __dup2(int old_fd, int newfd);

void chpt5_q4() {
    int tmp_fd = open("/tmp", O_RDWR | O_TMPFILE);
    int tmp_fd_flags = fcntl(tmp_fd, F_GETFL);

    //
    // dup(int)
    //
    assert(__dup(-1) == -1 && errno == EBADF);
    assert(__dup(0) == 4);
    assert(__dup(1) == 5);
    assert(__dup(2) == 6);
    assert(__dup(tmp_fd) == 7);
    
    //
    // dup2(int, int)
    //
    assert(__dup2(-1, 8) == -1 && errno == EBADF); // If old_fd is invalid, set EBADF error
    assert(__dup2(0, 8) == 8); // Normal behavior
    assert(__dup2(8, 8) == 8); // If old_fd is valid and old_fd == new_fd, just return the descriptor
    assert(__dup2(8, -1) == -1 && errno == EBADF); // If new_fd is invalid, set EBADF error
    assert(__dup2(-1, tmp_fd) == -1 && errno == EBADF && fcntl(tmp_fd, F_GETFL) == tmp_fd_flags); // If old_fd is invalid, don't close new_fd
    assert(__dup2(0, tmp_fd) == tmp_fd && fcntl(tmp_fd, F_GETFL) != tmp_fd_flags); // If new_fd is already open, close new_fd before duplication. File status flags should be different for new fd.
}

int __dup(int old_fd) {
    if (fcntl(old_fd, F_GETFD) == -1) {
        if (errno == EBADF) {
            return -1;
        } else {
            errExit("Failed to get descriptor status for old_fd=%d", old_fd);
        }
    }
    return fcntl(old_fd, F_DUPFD, 0);
}

/**
 * Note: this is not done atomically.
 */
int __dup2(int old_fd, int new_fd) {
    if (fcntl(old_fd, F_GETFD) == -1) {
        if (errno == EBADF) {
            return -1;
        }
        // Unexpected error.
        errExit("Failed to get descriptor status for old_fd=%d", old_fd);
    }

    if (old_fd == new_fd) {
        // Descriptors are equal, do nothing, return them.
        return old_fd;
    }

    if (fcntl(new_fd, F_GETFD) != -1) {
        // new_fd is already open.
        // close it silently. Don't report errors.
        close(new_fd);
    } else if (errno != EBADF) {
        // Unexpected error
        errExit("Failed to get descriptor status for new_fd=%d", new_fd);
    }

    if (fcntl(old_fd, F_DUPFD, new_fd) == -1) {
        if (errno == EINVAL) {
            // new_fd is an invalid value for a descriptor
            errno = EBADF;
            return -1;
        }
        // Unexpected error.
        errExit("Failed to duplicate descriptor from %d to %d", old_fd, new_fd);
    }

    return new_fd;
}