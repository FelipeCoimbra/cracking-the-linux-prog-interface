/**
 * Note: This is exercise is redundant in x86-64 architectures.
 */
#define _FILE_OFFSET_BITS 64 // Makes the off_t type have 8 bytes (64 bits)

#include <sys/stat.h>
#include <fcntl.h>

#include "../shared/errors.h"
#include "../shared/utils.h"
#include "q1.h"

void chpt5_q1(const char* filepath, long long offset) {
    int fd;
    off_t off = offset;

    fd = open(filepath, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd == -1)
        errExit("open");

    if (lseek(fd, off, SEEK_SET) == -1)
        errExit("lseek");

    if (write(fd, "test", 4) == -1)
        errExit("write");
}
