
#include <sys/types.h> /* Type definitions used by many programs */
#include <stddef.h> /* For size_t */
#include <stdio.h> /* Standard I/O functions */
#include <stdlib.h> /* Prototypes of commonly used library functions,
plus EXIT_SUCCESS and EXIT_FAILURE constants */
#include <unistd.h> /* Prototypes for many system calls */
#include <fcntl.h> /* Prototypes for open and flags */
#include <errno.h> /* Declares errno and defines error constants */
#include <string.h> /* Commonly used string-handling functions */

#include "errors.h" /* Declares our error-handling functions */
#include "utils.h"

int cmp_question(const char* candidate, int question_number) {
    static const char* prefixes[] = {
        "",
        "q",
        "question"
    };
    char target[8+3+1]; // "question" + "999" + "\0"
    char question_number_str[3+1];

    if (question_number<0 || question_number > 999) {
        fprintf(stderr, "ERROR: Invalid question number %d\n", question_number);
        exit(1);
    }
    sprintf(question_number_str, "%d", question_number);

    int equal = 0;
    for (int i = 0; i<3 && equal == 0; i++) {
        strcpy(target, prefixes[i]);
        strcat(target, question_number_str);
        equal = strncmp(candidate, target, strlen(target)+1) == 0;
    }
    
    return equal;
}


void deliver_write(int fd, const void * buffer, size_t nbytes) {
	size_t total_written = 0;
	ssize_t nwritten = write(fd, buffer+total_written, nbytes-total_written);
	while(nwritten > 0) {
		total_written += nwritten;
		nwritten = write(fd, buffer+total_written, nbytes-total_written);
	}
	if (nwritten == -1) {
		errExit("Error on writing data\n");
	}
}

void safe_close(int fd) {
	int close_st = close(fd);
	if (close_st == -1) {
		errExit("Error on close\n");
	}
}