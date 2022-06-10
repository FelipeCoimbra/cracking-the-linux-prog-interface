#include <stdio.h>
#include <fcntl.h>

#include "q1.h"

void q1(char * filepath, Boolean append) {
	
	int open_flags = O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC);
	mode_t newly_created_perms = 
		S_IRUSR | S_IWUSR |
		S_IRGRP | S_IWGRP |
		S_IROTH | S_IWOTH
		;
	int fd = open(filepath, open_flags, newly_created_perms);
	if (fd == -1) {
		errExit("Error on open syscall\n");
	}

	#define BUFFER_SZ 4096
	char buffer[BUFFER_SZ];
	ssize_t nr_read;
	while ((nr_read = read(STDIN_FILENO, buffer, BUFFER_SZ)) != 0) {
		write(fd, buffer, nr_read);
		write(STDOUT_FILENO, buffer, nr_read);
	}

	if (nr_read == -1) {
		errExit("Error on read syscall\n");
	}
}
