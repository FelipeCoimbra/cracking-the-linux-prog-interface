
#include "../shared/errors.h"
#include "../shared/utils.h"
#include "q2.h"

void chpt6_run(const char* q, int argc, char* args[]) {
    if (cmp_question(q, 2)) {
        chpt6_q2();
    } else {
        usageErr("Chapter 6 has no solution for \"%s\"\n", q);
    }
}