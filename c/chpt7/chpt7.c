#include <stdlib.h>

#include "../shared/errors.h"
#include "../shared/utils.h"
#include "q1.h"

void chpt7_run(const char* q, int argc, char* args[]) {
    const char * q1_usage = "<0 < NUM_ALLOCS <= 1000000> <BLOCK_SIZE > 0> [<FREE_STEP> > 0] [<FREE_MIN> > 0] [0 < <FREE_MAX> < num_allocs]\n";

    if (cmp_question(q, 1)) {
        if (argc < 3) {
            usageErr(q1_usage);
        }
        char * end_ptr;

        int num_allocs = strtol(args[1], &end_ptr, 10);
        if (*end_ptr != '\0' || num_allocs <= 0 || num_allocs > Q1_MAX_NUM_ALLOCS) {
            usageErr("q1_usage");
        }

        int block_size = strtol(args[2], &end_ptr, 10);
        if (*end_ptr != '\0' || block_size <= 0) {
            usageErr(q1_usage);
        }
        int free_step = (argc > 3) ? strtol(args[3], &end_ptr, 10) : 1;
        if (*end_ptr != '\0' || free_step <= 0) {
            usageErr(q1_usage);
        }
        int free_min =  (argc > 4) ? strtol(args[4], &end_ptr, 10) : 1;
        if (*end_ptr != '\0' || free_min <= 0) {
            usageErr(q1_usage);
        }
        int free_max =  (argc > 5) ? strtol(args[5], &end_ptr, 10) : num_allocs;
        if (*end_ptr != '\0' || free_max <= free_min || free_max > num_allocs) {
            usageErr(q1_usage);
        }

        chpt7_q1(num_allocs, block_size, free_step, free_min, free_max);
    } else {
        usageErr("Chapter 7 has no solution for \"%s\"\n", q);
    }
}