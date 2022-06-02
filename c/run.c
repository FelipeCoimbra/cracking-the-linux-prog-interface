#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chpt3/chpt3.h"

/**
 * chpt_number must be in range [0,64]
 */
int cmp_chpt(const char* candidate, int chpt_number) {
    static const char* prefixes[] = {
        "",
        "c",
        "chp",
        "chpt",
        "chapter"
    };
    char target[7+2+1]; // "chapter" + "64" + "\0"
    char chpt_number_str[2+1];

    if (chpt_number<0 || chpt_number > 64) {
        fprintf(stderr, "ERROR: Invalid chapter number %d\n", chpt_number);
        exit(1);
    }
    sprintf(chpt_number_str, "%d", chpt_number);

    int equal = 0;
    for (int i = 0; i<4 && equal == 0; i++) {
        strcpy(target, prefixes[i]);
        strcat(target, chpt_number_str);
        equal = strncmp(candidate, target, strlen(target)+1) == 0;
    }
    
    return equal;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: run CHAPTER QUESTION [...ARGS]\n");
        exit(1);
    }
    if (cmp_chpt(argv[1], 3)) {
        chpt3_run(argv[2], NULL);
    } else {
        fprintf(stderr, "No solutions for chapter %s\n", argv[1]);
    }
    return 0;
}