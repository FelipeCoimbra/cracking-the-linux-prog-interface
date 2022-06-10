#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * question_number must be in range [0,999]
 */
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