#include <stdio.h>
#include <string.h>

#include "../shared/utils.h"
#include "q1.h"

void chpt3_run(const char* q, const char* args[]) {
    if (cmp_question(q, 1)) {
        q1();
    } else {
        fprintf(stderr, "Chapter 3 has no solution for \"%s\"\n", q);
    }
}