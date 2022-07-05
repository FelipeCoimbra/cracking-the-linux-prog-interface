#define _DEFAULT_SOURCE /** Unlocks brk() and sbrk() in glibc */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../shared/errors.h"
#include "../shared/utils.h"
#include "q1.h"

void
chpt7_q1(int num_allocs, int block_size, int free_step, int free_min, int free_max)
{
    char *ptr[Q1_MAX_NUM_ALLOCS];

    printf("Initial program break:          %10p\n", sbrk(0));

    printf("Allocating %d*%d bytes\n", num_allocs, block_size);
    for (int i = 0; i < num_allocs; i++) {
        ptr[i] = malloc(block_size);
        if (ptr[i] == NULL)
            errExit("malloc");
        else
            printf("Malloc %d, Program break: %10p\n", i+1, sbrk(0));
    }

    printf("Program break is now:           %10p\n", sbrk(0));

    printf("Freeing blocks from %d to %d in steps of %d\n",
                free_min, free_max, free_step);
    for (int i = free_min - 1; i < free_max; i += free_step)
        free(ptr[i]);

    printf("After free(), program break is: %10p\n", sbrk(0));
}

