#include <fcntl.h>
#include <unistd.h>

#include "../shared/errors.h"
#include "../shared/utils.h"
#include "q2.h"

void chpt5_q2() {

    int fd = open("/home/vagrant/out", O_WRONLY | O_EXCL | O_APPEND);
    if (fd == -1) {
        errExit("open\n");
    }

    if (lseek(fd, 0, SEEK_SET) == -1) {
        errExit("lseek\n");
    }
    
    deliver_write(fd, "datadatadata\nmoredatamoredatamoredata\n\n", 39);

    safe_close(fd);
}