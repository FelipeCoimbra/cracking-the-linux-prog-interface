#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shared/errors.h"
#include "../shared/utils.h"
#include "q1.h"
#include "q2.h"
#include "q3.h"
#include "q4.h"

void chpt5_run(const char* q, int argc, char* argv[]) {
    const char * q1_usage = "chpt5 q1 <FILEPATH (255)> <OFFSET>\n";
    const char * q3_usage = "chpt5 q3 <FILEPATH (255)> <NUM BYTES> [x]\n";

    #define Q1_FILEPATH_SZ 256
    char filepath[Q1_FILEPATH_SZ] = "";

    if (cmp_question(q, 1)) {
        if (argc != 3) {
            usageErr(q1_usage);
        }

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

    } else if (cmp_question(q, 2)) {
        chpt5_q2();
    } else if (cmp_question(q, 3)) {
        if (argc < 3) {
            usageErr(q3_usage);
        }

        strncpy(filepath, argv[1], Q1_FILEPATH_SZ);
        if (filepath[Q1_FILEPATH_SZ-1] != '\0') {
            usageErr(q3_usage);
        }

        char *parsing_end;
        long num_bytes = strtol(argv[2], &parsing_end, 10);
        if (*parsing_end != '\0') {
            usageErr(q3_usage);
        } else if (num_bytes <= 0) {
            errExit("ERROR: Number of bytes %ld is nonpositive\n", num_bytes);
        }

        chpt5_q3(filepath, num_bytes, argc < 4);

    } else if (cmp_question(q, 4)) {
        chpt5_q4();
    } else {
        usageErr("Chapter 4 has no solution for \"%s\"\n", q);
    }
}