#include <stdlib.h>

#include "../shared/errors.h"
#include "../shared/utils.h"
#include "q1.h"
#include "q2.h"

void chpt8_run(const char* q, int argc, char* args[]) {
    if (cmp_question(q, 1)) {
        chpt8_q1();
    } else if (cmp_question(q, 2)) {
        chpt8_q2();
    } else {
        usageErr("Chapter 8 has no solution for \"%s\"\n", q);
    }
}