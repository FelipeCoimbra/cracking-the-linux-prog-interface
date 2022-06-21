
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#include "../shared/errors.h"
#include "../shared/utils.h"
#include "q5.h"

void chpt5_q5() {

    mode_t create_perms = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    int fd = open("/home/vagrant/out", O_RDWR | O_CREAT | O_TRUNC, create_perms);
    if (fd == -1) {
        errExit("open");
    }

    int dup_fd = dup(fd);
    if (dup_fd == -1) {
        errExit("dup");
    }

    int fd_fflags = fcntl(fd, F_GETFL);
    if (fd_fflags == -1) {
        errExit("fd_fflags");
    }
    int dup_fd_fflags = dup_fd_fflags = fcntl(fd, F_GETFL);
    if (dup_fd_fflags == -1) {
        errExit("dup_fd_fflags");
    }

    assert(fd_fflags == dup_fd_fflags); // Should share the same file status flags

    const off_t random_offset = 758194654;
    if (lseek(fd, random_offset, SEEK_SET) == -1) {
        errExit("lseek");
    }

    assert(lseek(dup_fd, 0, SEEK_CUR) == random_offset); // Should share the same file offset

    safe_close(fd);
}