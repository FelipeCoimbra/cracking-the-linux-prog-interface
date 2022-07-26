#include <assert.h>
#include <errno.h>
#include <pwd.h>
#include <stddef.h>
#include <string.h>

#include "../shared/errors.h"
#include "q2.h"

struct passwd * __getpwnam(const char *);

void chpt8_q2() {
    struct passwd * found;
    assert(__getpwnam(NULL) == NULL && errno == ENOENT);
    assert(__getpwnam("") == NULL && errno == ENOENT);
    assert((found = __getpwnam("root")) != NULL &&  strcmp(found->pw_name, "root") == 0);
    assert(__getpwnam("afjdnajskldfn") == NULL && errno == ENOENT);
}

/**
 * Return NULL and set errno to ENOENT when no user with name is found.
 */
struct passwd * __getpwnam(const char * name) {
    if (name == NULL || name[0] == '\0') {
        errno = ENOENT;
        return NULL;
    }
    
    setpwent();
    struct passwd * entry;
    do {
        // errno should be set to 0 before every call to getpwent() in order to check 
        // if there has been an underlying error or there just ain't any entries left
        // https://man7.org/linux/man-pages/man3/getpwent.3.html
        errno = 0;
        entry = getpwent();
    } while (entry != NULL && strcmp(entry->pw_name, name) != 0);
    endpwent();

    if (entry == NULL && errno == 0) {
        //
        // Reached end of list
        //
        errno = ENOENT;
    }

    return entry;
}