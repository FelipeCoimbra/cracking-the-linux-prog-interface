#include <fcntl.h>
#include <unistd.h>

#include "../shared/errors.h"
#include "../shared/utils.h"
#include "q3.h"

void chpt5_q3(const char * filepath, long num_bytes, Boolean append) {
    int open_flags = O_WRONLY | O_CREAT | (append ? O_APPEND : 0);
    mode_t create_flags = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    int fd = open(filepath, open_flags, create_flags);
    if (fd == -1) {
        errExit("Failed to open %s. append=%d\n", filepath, append);
    }

    const char data[] = "a";
    while (num_bytes > 0) {
        if (!append && lseek(fd, 0, SEEK_END) == -1) {
            errExit("Error at lseek after writing %d bytes.\n", num_bytes);
        }
        deliver_write(fd, data, 1);
        num_bytes--;
    }

    safe_close(fd);
}
