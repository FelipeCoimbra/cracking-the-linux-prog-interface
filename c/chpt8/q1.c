#include <pwd.h>
#include <stdio.h>

#include "q1.h"

void chpt8_q1() {
    printf("%s %s\n", 
        getpwuid(0)->pw_name, // root user
        getpwuid(1)->pw_name // daemon user
    );
}