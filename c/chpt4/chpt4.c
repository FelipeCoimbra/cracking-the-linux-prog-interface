#include <stdio.h>
#include <string.h>

#include "../shared/utils.h"
#include "q1.h"
#include "q2.h"

void chpt4_run(const char* q, int argc, char* argv[]) {
    const char * q1_usage = "chp4 q1 [-a] <FILEPATH (255)>\n";
    const char * q2_usage = "chpt4 q2 <SOURCE FILE> <DESTINATION FILE>\n";

    if (cmp_question(q, 1)) {
        if (argc < 2) {
            usageErr(q1_usage);
        }

        #define Q1_FILEPATH_SZ 256
        char filepath[Q1_FILEPATH_SZ] = "";
        Boolean append = FALSE;

        int read_opt;
        optind = 1;
        while ((read_opt = getopt(argc, argv, "a")) != -1) {
            if (read_opt == 'a') {
                append = TRUE;
            }
        }

        int idx = optind;
        Boolean filepath_read = FALSE;
        while (idx < argc) {
            if (!filepath_read) {
                filepath_read = TRUE;
                strncpy(filepath, argv[idx], Q1_FILEPATH_SZ);
                if (filepath[Q1_FILEPATH_SZ-1] != '\0') {
                    fprintf(stderr, "ERROR: FILEPATH over 255 characters\n");
                    usageErr(q1_usage);
                }
            } else {
                fprintf(stderr, "Skipping argument \"%s\"\n", argv[idx]);
            }
            idx++;
        }

        q1(filepath, append);

    } else if (cmp_question(q, 2)) {
        if (argc != 3) {
           usageErr(q2_usage);
        } else {
            q2(argv[1], argv[2]);
        }
    } else {
        usageErr("Chapter 4 has no solution for \"%s\"\n", q);
    }
}