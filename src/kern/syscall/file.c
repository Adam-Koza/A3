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
#include <kern/fcntl.h>
#include <vnode.h>
#include <vfs.h>
#include <current.h>
#include <file.h>
#include <synch.h>
#include <syscall.h>
#include <lib.h>

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

	// filename is an invalid pointer
	if (filename == NULL){
		return EFAULT;
	}

	struct vnode *newFile;
	int result;


	//find fd from file table of curthread
	int fd = 0;

	//lock_acquire(curthread->t_filetable->t_lock);

	while (curthread->t_filetable->t_entries[fd] != NULL){
		if (fd > __OPEN_MAX)
			return EMFILE; // File table full
		fd++;
	}
	// We have an fd!
	*retfd = fd;
	// Most done in  vfs_open, will check for valid flags
	result = vfs_open(filename, flags, (mode_t)mode, &newFile);
	// If error, return with that error
	if (result){
		return result;}

	newFile->offset = 0; //Set the initial offset to 0


	// Set the table entry to vnode of the new file
	curthread->t_filetable->t_entries[fd] = newFile;

	//lock_release(curthread->t_filetable->t_lock);

	// Success
	return 0;
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

	if (&fd == NULL) {return EBADF;}

	// first check open count
	// if 1, decrement, and undo op

	lock_acquire(curthread->t_filetable->t_lock);

	struct vnode *fileToClose = curthread->t_filetable->t_entries[fd];

	// If more then one prosses is using this file, caused by fork()

	// Will prevent a forked node closing or otherwise altering
	lock_acquire(fileToClose->v_lock);

	if (fileToClose->vn_refcount > 1){
		// Wont close file, just get rid of reference for this process
		curthread->t_filetable->t_entries[fd] = NULL;
		VOP_DECREF(fileToClose);
	}
	else{
		// Will close file, as it's the only one being referenced.
		curthread->t_filetable->t_entries[fd] = NULL;
		vfs_close(fileToClose);
	}
	lock_release(fileToClose->v_lock);
	lock_release(curthread->t_filetable->t_lock);

	// else decrement, and remove ptr from table.
	// DO NOT free as another process has it open.
     // (void)fd;

	return 0;
}

/*
* filetable_gen
* pretty straightforward -- allocate the space,
* create the lock, and initialize all entries to NULL. */

int
filetable_gen(struct thread *da_thread)
{
	// Declare file descriptor.
	int fd;
	char name[10] = "dumb_name";

	// Make sure file table doesn't already exist.
	if (da_thread->t_filetable != NULL) {return EINVAL;}

	// Allocate memory for the new filetable.
	da_thread->t_filetable = (struct filetable *)kmalloc(sizeof(struct filetable));
	if (da_thread->t_filetable == NULL) {return ENOMEM;}

	// Create lock.
	da_thread->t_filetable->t_lock = lock_create(name);

	// Lock down the file table.
	lock_acquire(da_thread->t_filetable->t_lock);

	// Initialize all file descriptor entries to NULL.
	for (fd = 0; fd < __OPEN_MAX; fd++){
		da_thread->t_filetable->t_entries[fd] = NULL;
	}

	// Lock down the file table.
	lock_release(da_thread->t_filetable->t_lock);

	// Return success.
	return 0;

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

	// Pass work to filetable_gen.
	result = filetable_gen(curthread);
	if (result) {return result;}

	// Lock down the file table.
	lock_acquire(curthread->t_filetable->t_lock);

	// [stdin]  Setup file descriptor, add to the filetable at index 0.
	result = file_open(filename, O_RDONLY, 0, &fd);
	if (result) {lock_release(curthread->t_filetable->t_lock); return result;} // If an error occurred, return error.

	// [stdout] Setup file descriptor, add to the filetable at index 1.
	result = file_open(filename, O_WRONLY, 0, &fd);
	if (result) {lock_release(curthread->t_filetable->t_lock); return result;} // If an error occurred, return error.

	// [stderr] Setup file descriptor, add to the filetable at index 2.
	result = file_open(filename, O_WRONLY, 0, &fd);
	if (result) {lock_release(curthread->t_filetable->t_lock); return result;} // If an error occurred, return error.

	// Release lock on file table.
	lock_release(curthread->t_filetable->t_lock);

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

    // Lock down the current thread.
    lock_acquire(curthread->t_filetable->t_lock);

    for (file_d = 0; file_d < __OPEN_MAX; file_d++) {
    	struct vnode *entry = ft->t_entries[file_d];
    	if (entry != NULL) {file_close(file_d);}
    }
    kfree(ft);

    // Release lock on current thread.
    lock_release(curthread->t_filetable->t_lock);
}	


/* 
 * You should add additional filetable utility functions here as needed
 * to support the system calls.  For example, given a file descriptor
 * you will want some sort of lookup function that will check if the fd is 
 * valid and return the associated vnode (and possibly other information like
 * the current file position) associated with that open file.
 */


/* END A3 SETUP */
