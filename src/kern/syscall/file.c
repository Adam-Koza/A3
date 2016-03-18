/* BEGIN A3 SETUP */
/*
 * File handles and file tables.
 * New for ASST3
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/unistd.h>
#include <file.h>
#include <syscall.h>

/*** openfile functions ***/

/*
 * file_open
 * opens a file, places it in the filetable, sets RETFD to the file
 * descriptor. the pointer arguments must be kernel pointers.
 * NOTE -- the passed in filename must be a mutable string.
 * 
 * A3: As per the OS/161 man page for open(), you do not need 
 * to do anything with the "mode" argument.
 */
int
file_open(char *filename, int flags, int mode, int *retfd)
{
	(void)filename;
	(void)flags;
	(void)retfd;
	(void)mode;


	return EUNIMP;
}


/* 
 * file_close
 * Called when a process closes a file descriptor.  Think about how you plan
 * to handle fork, and what (if anything) is shared between parent/child after
 * fork.  Your design decisions will affect what you should do for close.
 */
int
file_close(int fd)
{
        (void)fd;

	return EUNIMP;
}

/*** filetable functions ***/

/* 
 * filetable_init
 * pretty straightforward -- allocate the space, set up 
 * first 3 file descriptors for stdin, stdout and stderr,
 * and initialize all other entries to NULL.
 * 
 * Should set curthread->t_filetable to point to the
 * newly-initialized filetable.
 * 
 * Should return non-zero error code on failure.  Currently
 * does nothing but returns success so that loading a user
 * program will succeed even if you haven't written the
 * filetable initialization yet.
 */

int
filetable_init(void)
{
	// Make sure filetable doesn't already exist.
	if (curthread->t_filetable != NULL) {return EINVAL;}

	// Declare file descriptor.
	int fd, result;

	// Setup filename path.
	char filename[5];
	strcpy(filename, "con:");

	// Allocate memory for the new filetable.
	curthread->t_filetable = (struct filetable *)kmalloc(sizeof(struct filetable));
	if (curthread->t_filetable == NULL) {return ENOMEM;}

	// Initialize all file descriptor entries to NULL.
	for (fd = 0; fd < __OPEN_MAX; fd++){
		curthread->t_filetable->t_entries[fd] = NULL;
	}

	// [stdin]  Setup file descriptor, add to the filetable at index 0.
	result = file_open(filename, O_RDONLY, 0, &fd);
	if (result) {return result;} // If an error occurred, return error.

	// [stdout] Setup file descriptor, add to the filetable at index 1.
	result = file_open(filename, O_WRONLY, 0, &fd);
	if (result) {return result;} // If an error occurred, return error.

	// [stderr] Setup file descriptor, add to the filetable at index 2.
	result = file_open(filename, O_WRONLY, 0, &fd);
	if (result) {return result;} // If an error occurred, return error.

	// Otherwise, return success.
	return 0;
}	

/*
 * filetable_destroy
 * closes the files in the file table, frees the table.
 * This should be called as part of cleaning up a process (after kill
 * or exit).
 */
void
filetable_destroy(struct filetable *ft)
{
    int file_d;
    for (file_d = 0; file_d < __OPEN_MAX; file_d++) {
    	struct filetable_entry *entry = ft->t_entries[file_d];
    	if (entry == NULL) {file_close(file_d);}
    }
    kfree(ft);
}	


/* 
 * You should add additional filetable utility functions here as needed
 * to support the system calls.  For example, given a file descriptor
 * you will want some sort of lookup function that will check if the fd is 
 * valid and return the associated vnode (and possibly other information like
 * the current file position) associated with that open file.
 */


/* END A3 SETUP */
