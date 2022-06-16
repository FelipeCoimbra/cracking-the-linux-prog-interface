/**
 * 	Tell the processor to use the GNU library.
 * 	This unlocks the SEEK_DATA, SEEK_HOLE, and fallocate functionalities.
 * 	NOTE: This makes the code non-portable, as it works only in certain unix variations.
*/
#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif

#include <fcntl.h> /** open and mode_t flags */
#include <linux/falloc.h>
#include <sys/types.h> /** size types */
#include <unistd.h> /** syscall protoypes */

#include "q2.h"
#include "../shared/errors.h"
#include "../shared/utils.h"

/**
 * Like a write, but is guaranteed to deliver all bytes (or die trying!)
 */
void deliver_write(int fd, const void * buffer, size_t nbytes) {
	size_t total_written = 0;
	ssize_t nwritten = write(fd, buffer+total_written, nbytes-total_written);
	while(nwritten > 0) {
		total_written += nwritten;
		nwritten = write(fd, buffer+total_written, nbytes-total_written);
	}
	if (nwritten == -1) {
		errExit("Error on writing data\n");
	}
}

void safe_close(int fd) {
	int close_st = close(fd);
	if (close_st == -1) {
		errExit("Error on input close\n");
	}
}

// Solution for book assignment
void q2_std(const char* src_file, const char* dst_file);
// A 2nd version for the assignment.
// Any null values read are turned into holes, independent of being written values or holes.
void q2_nulls_into_holes(const char* src_file, const char* dst_file);

void q2(const char* src_file, const char* dst_file) {
	#ifdef CHPT4_Q2_TURN_NULLS_INTO_HOLES
		q2_nulls_into_holes(src_file, dst_file);
	#else
		q2_std(src_file, dst_file);
	#endif
}

void q2_std(const char* src_file, const char* dst_file) {

	int src_fd = open(src_file, O_RDONLY);
	if (src_fd == -1) {
		errExit("Error on src open\n");
	}

	mode_t dst_creat_permissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	int dst_fd = open(dst_file, O_WRONLY | O_CREAT | O_TRUNC, dst_creat_permissions);
	if (dst_fd == -1) {
		errExit("Error on dst open\n");
	}

	// Calculate file size
	const off_t src_file_size = lseek(src_fd, 0, SEEK_END);
	if (src_file_size < 0) {
		errExit("Failed to retrieve file size\n");
	}

	#define BUFFER_SZ 4096
	char buffer[BUFFER_SZ];
	ssize_t nread;
	off_t data_begin;
	off_t data_end = 0; // Start supposing file starts with a hole

	while (TRUE) {
		//
		// Try to move to next data region
		//
		data_begin = lseek(src_fd, data_end, SEEK_DATA);
		if (data_begin < data_end) {
			if (errno == ENXIO) {
				// There's no more data to be read
				// This indicates data_end points to EOF or to a hole at the end of the file.
				// Check if it is a hole. If so, clone it in the destination
				int posix_error_code;
				if (data_end < src_file_size) {
					// Reached one last hole
					if ((posix_error_code = posix_fallocate(dst_fd, data_end, src_file_size-data_end)) != 0) {
						errExitEN(posix_error_code, "Failed to allocate extra space for hole at the end of destination\n");
					}
					if (fallocate(dst_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, data_end, src_file_size-data_end) == -1) {
						errExit("Failed to punch hole at the end of destination\n");
					}
				} else {
					// Reached EOF.
				}
				break;
			} else {
				// Any other errno is a real error
				errExit("Failed to move to closest data region starting at %ld\n", (long) data_end);
			}
		} else {
			// Moved data_begin to the beginning of the next data region
			// Now move the data_end accordingly
		}
		data_end = lseek(src_fd, data_begin, SEEK_HOLE);
		if (data_end < data_begin) {
			errExit("Failed to find the end of data region beginning at: %ld\n", (long) data_begin);
		}
		
		// Set cursors
		lseek(src_fd, data_begin, SEEK_SET);
		lseek(dst_fd, data_begin, SEEK_SET);

		size_t read_within_region = 0;
		while ((nread = read(src_fd, buffer, min(BUFFER_SZ, data_end-data_begin-read_within_region))) > 0) {
			read_within_region += nread;
			deliver_write(dst_fd, buffer, nread);
		}
		if (nread == -1) {
			errExit("Failed to read file between offsets %ld - %ld\n", (long) (data_begin+read_within_region), (long) data_end);
		}
	}

	safe_close(src_fd);
	int fsync_st = fsync(dst_fd);
	if (fsync_st == -1) {
		errExit("Error on output fsync\n");
	}
	safe_close(dst_fd);
}

void q2_nulls_into_holes(const char* src_file, const char* dst_file) {
	
	int src_fd = open(src_file, O_RDONLY);
	if (src_fd == -1) {
		errExit("Error on src open\n");
	}

	mode_t dst_creat_permissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	int dst_fd = open(dst_file, O_WRONLY | O_CREAT | O_TRUNC, dst_creat_permissions);
	if (dst_fd == -1) {
		errExit("Error on dst open\n");
	}

	#define BUFFER_SZ 4096
	char buffer[BUFFER_SZ];
	ssize_t nread;
	size_t total_read = 0;
	Boolean inside_hole = FALSE;
	off_t hole_begin, hole_end;
	while ((nread = read(src_fd, buffer, BUFFER_SZ)) > 0) {
		total_read += nread;
		ssize_t nonhole_begin = 0; // Assume beginning of bytes read is not inside a hole
		
		for (ssize_t i=0; i<nread; i++) {
			if (buffer[i] == 0 && !inside_hole) {
				// Beginning of a hole.
				// This could actually be a real sequence of null bytes. In this case we will hole those out.
				// Won't be a problem in further reads and will save some space, but might break something in real applications, who knows.
				inside_hole = TRUE;
				hole_begin = lseek(src_fd, 0, SEEK_CUR) - (nread-i);
				if (hole_begin < 0) {
					errExit("Error on hole_begin fseek\nTotal bytes read: %zu\nFailure pos on buffer: %d\n", total_read, i);
				}
				// Write bytes read so far, if there are any
				if (i > nonhole_begin) {
					deliver_write(dst_fd, buffer+nonhole_begin, i-nonhole_begin);
				}
			} else if (inside_hole && buffer[i] != 0) {
				// End of hole.
				inside_hole = FALSE;
				nonhole_begin = i;
				hole_end = lseek(src_fd, 0, SEEK_CUR) - (nread - i);
				if (hole_end < 0) {
					errExit("Error on hole_begin fseek\nTotal bytes read: %zu\nFailure pos on buffer: %zd\n", total_read, i);
				}
				// Advance write cursor
				off_t st = lseek(dst_fd, hole_end-hole_begin, SEEK_CUR);
				if (st < hole_end-hole_begin) {
					errExit("Error on dst fseek\nTotal bytes read: %zu\nFailure pos on buffer: %zd\n", total_read, i);
				}
			}
		}

		if (!inside_hole) {
			// We have some bytes outside any hole in the end of the buffer
			deliver_write(dst_fd, buffer+nonhole_begin, nread-nonhole_begin);
		}
	}

	if (nread == -1) {
		errExit("Error on read. Total bytes read = %zu\n", total_read);
	}

	safe_close(src_fd);
	int fsync_st = fsync(dst_fd);
	if (fsync_st == -1) {
		errExit("Error on output fsync\n");
	}
	safe_close(dst_fd);
}
