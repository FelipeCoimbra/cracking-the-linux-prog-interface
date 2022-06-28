#include <fcntl.h>
#include <unistd.h>

#include "../shared/utils.h"
#include "q6.h"

void chpt5_q6() {
    const char* file = "/home/vagrant/out";
    int fd1, fd2, fd3;
    fd1 = open(file, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    fd2 = dup(fd1);
    fd3 = open(file, O_RDWR);
    write(fd1, "Hello,", 6);
    write(fd2, "world", 6);
    lseek(fd2, 0, SEEK_SET);
    write(fd1, "HELLO,", 6);
    write(fd3, "Gidday", 6);

    safe_close(fd1);
    safe_close(fd3);
}