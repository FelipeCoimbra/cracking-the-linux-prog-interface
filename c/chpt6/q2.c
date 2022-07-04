#include <setjmp.h>
#include <stdio.h>

#include "q2.h"

void jump_target(jmp_buf *stack_info) {
    int longjmp_value;
    if ((longjmp_value = setjmp(*stack_info)) == 0) {
        printf("Setup land point with setjmp\n");
    } else {
        printf("Successfully jumped into jump_target. Received value %d\n", longjmp_value);
    }
}

void chpt6_q2() {
    jmp_buf stack_info;
    jump_target(&stack_info);
    longjmp(stack_info, 12345); // Jump into jump_target, but it has already returned.
    // Should segfault!
}
