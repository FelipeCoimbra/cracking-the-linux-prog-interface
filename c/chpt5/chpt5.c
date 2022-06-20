#include <stdio.h>
#include <string.h>

#include "../shared/errors.h"
#include "../shared/utils.h"
#include "q1.h"

void chpt5_run(const char* q, int argc, char* argv[]) {
    const char * q1_usage = "chpt5 q1 <FILEPATH (255)> <OFFSET>\n";

    if (cmp_question(q, 1)) {
        if (argc != 3) {
            usageErr(q1_usage);
        }

        #define Q1_FILEPATH_SZ 256
        char filepath[Q1_FILEPATH_SZ] = "";
        strncpy(filepath, argv[1], Q1_FILEPATH_SZ);
        if (filepath[Q1_FILEPATH_SZ-1] != '\0') {
            usageErr(q1_usage);
        }
        char *parsing_end;
        long long offset = strtoll(argv[2], &parsing_end, 10);
        if (*parsing_end != '\0') {
            usageErr(q1_usage);
        } else if (offset <= 0) {
            errExit("ERROR: Offset %lld is nonpositive\n", offset);
        }

        chpt5_q1(filepath, offset);

    } else {
        usageErr("Chapter 4 has no solution for \"%s\"\n", q);
    }
}