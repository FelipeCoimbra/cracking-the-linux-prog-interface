#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "q3.h"

extern char** environ;

int __setenv(const char * name, const char * value, int overwrite);
int __unsetenv(const char * name);

//
// For debugging 
//
void printenv() {
    char ** ptr = environ;
    while (*ptr != NULL) {
        printf("%s\n", *ptr);
        ptr++;
    }
}

void chpt6_q3() {
    printf("Initial environment list:\n\n");
    printenv();
    //
    // setenv
    //
    assert(__setenv(NULL, NULL, 0) == -1 && errno == EINVAL);
    assert(__setenv("", NULL, 0) == -1 && errno == EINVAL);
    assert(__setenv("=", NULL, 0) == -1 && errno == EINVAL);
    assert(__setenv("SHELL=", NULL, 0) == -1 && errno == EINVAL);
    assert(__setenv("SHELL", NULL, 0) == 0);
    assert(__setenv("SHELL", NULL, 1) == 0 && strcmp(getenv("SHELL"), "") == 0);
    assert(__setenv("SHELL", "/bin/sh", 1) == 0 && strcmp(getenv("SHELL"), "/bin/sh") == 0);
    assert(__setenv("MY_SWEET_ENV_VARIABLE", "eKlklyutIesXrcrdDRTcdFHGBhKnhNjnjiLJ", 0) == 0 && strcmp(getenv("MY_SWEET_ENV_VARIABLE"),  "eKlklyutIesXrcrdDRTcdFHGBhKnhNjnjiLJ") == 0);
    assert(__setenv("MY_NOT_SO_SWEET_ENV_VARIABLE", "42", 1) == 0 && strcmp(getenv("MY_NOT_SO_SWEET_ENV_VARIABLE"),  "42") == 0);
    printf("\nAfter setenv:\n\n");
    printenv();
    //
    // unsetenv
    //
    assert(__unsetenv(NULL) == -1 && errno == EINVAL);
    assert(__unsetenv("") == -1 && errno == EINVAL);
    assert(__unsetenv("=") == -1 && errno == EINVAL);
    assert(__unsetenv("SHELL=") == -1 && errno == EINVAL);
    assert(__unsetenv("somevariablenamethatisnotlisted") == 0);
    assert(__unsetenv("MY_NOT_SO_SWEET_ENV_VARIABLE") == 0 && getenv("MY_NOT_SO_SWEET_ENV_VARIABLE") == NULL);
    assert(__unsetenv("OLDPWD") == 0 && getenv("OLDPWD") == NULL);
    printf("\nAfter unsetenv:\n\n");
    printenv();
}

int __setenv(const char * name, const char * value, int overwrite) {
    if (name == NULL || name[0] == '\0' || name[0] == '=') {
        errno = EINVAL;
        return -1;
    }
    size_t name_len;
    for (name_len = 1; name[name_len] != '\0'; name_len++) {
        if (name[name_len] == '=') {
            errno = EINVAL;
            return -1;
        }
    }

    if (!overwrite && getenv(name) != NULL) {
        // Variable exists and we must not overwrite
        return 0;
    }

    size_t value_len = value == NULL ? 0 : strlen(value);

    char * new_env_buffer = (char *) malloc(name_len + 1 + value_len + 1); // <name>=<value> string
    if (new_env_buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }
    
    strncpy(new_env_buffer, name, name_len);
    new_env_buffer[name_len] = '=';
    if (value) {
        strcpy(new_env_buffer + name_len + 1, value);
    } else {
        new_env_buffer[name_len + 1] = '\0';
    }

    //
    // Note that if there is an environment string already, existing pointers to it won't break
    // but will become outdated - the string they point to is not part of the environment list anymore.
    //
    if (putenv(new_env_buffer) != 0) {
        return -1;
    }

    return 0;
}

//
// NOTE: In this implementation we do not guarantee the list order is maintained.
//
int __unsetenv(const char * name) {
    if (name == NULL || name[0] == '\0' || name[0] == '=') {
        errno = EINVAL;
        return -1;
    }
    size_t name_len;
    for (name_len = 1; name[name_len] != '\0'; name_len++) {
        if (name[name_len] == '=') {
            errno = EINVAL;
            return -1;
        }
    }

    char ** env_ptr = environ;
    char ** env_var_found = NULL;

    while (*env_ptr != NULL && env_var_found == NULL) {
        size_t env_varname_len = 0;
        while ((*env_ptr)[env_varname_len] != '=') {
            env_varname_len++;
        }
        if (name_len == env_varname_len && strncmp(name, *env_ptr, name_len) == 0) {
            // Found env variable
            env_var_found = env_ptr;
        } else {
            env_ptr++;
        }
    }

    if (env_var_found) {
        // Find last variable of environment list
        while (*(env_ptr+1) != NULL) {
            env_ptr++;
        }
        if (env_ptr != env_var_found) {
            // Swap contents of entry to be deleted with last list item
            #define CHAR_PTR_CONTENT_XOR(a, b) (char *) ((long long) *a ^ (long long) *b)
            *env_ptr =  CHAR_PTR_CONTENT_XOR(env_ptr, env_var_found);
            *env_var_found = CHAR_PTR_CONTENT_XOR(env_ptr, env_var_found);
            *env_ptr = CHAR_PTR_CONTENT_XOR(env_ptr, env_var_found);
        }
        // Delete last list item
        *env_ptr = NULL;
    }

    return 0;
}