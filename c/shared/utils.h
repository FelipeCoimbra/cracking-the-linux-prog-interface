#ifndef __SHARED_UTILS_H__
#define __SHARED_UTILS_H__

#include <stddef.h> /* For size_t */

typedef enum { FALSE, TRUE } Boolean;

#define min(m,n) ((m) < (n) ? (m) : (n))
#define max(m,n) ((m) > (n) ? (m) : (n))

/**
 * question_number must be in range [0,999]
 */
int cmp_question(const char* candidate, int question_number);

/**
 * Like a write, but is guaranteed to deliver all bytes (or die trying!)
 */
void deliver_write(int fd, const void * buffer, size_t nbytes);

/**
 * Like a close, but is guaranteed to close successfully (or die trying!)
 */
void safe_close(int fd);

#endif