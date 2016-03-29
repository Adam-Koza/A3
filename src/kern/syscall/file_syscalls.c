/* BEGIN A3 SETUP */
/* This file existed for A1 and A2, but has been completely replaced for A3.
 * We have kept the dumb versions of sys_read and sys_write to support early
 * testing, but they should be replaced with proper implementations that 
 * use your open file table to find the correct vnode given a file descriptor
 * number.  All the "dumb console I/O" code should be deleted.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/seek.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <syscall.h>
#include <vfs.h>
#include <vnode.h>
#include <uio.h>
#include <kern/fcntl.h>
#include <kern/unistd.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <copyinout.h>
#include <synch.h>
#include <file.h>

/* This special-case global variable for the console vnode should be deleted 
 * when you have a proper open file table implementation.
 */
struct vnode *cons_vnode=NULL; 

/* This function should be deleted, including the call in main.c, when you
 * have proper initialization of the first 3 file descriptors in your 
 * open file table implementation.
 * You may find it useful as an example of how to get a vnode for the 
 * console device.
 */
void dumb_consoleIO_bootstrap()
{
  int result;
  char path[5];

  /* The path passed to vfs_open must be mutable.
   * vfs_open may modify it.
   */

  strcpy(path, "con:");
  result = vfs_open(path, O_RDWR, 0, &cons_vnode);

  if (result) {
    /* Tough one... if there's no console, there's not
     * much point printing a warning...
     * but maybe the bootstrap was just called in the wrong place
     */
    kprintf("Warning: could not initialize console vnode\n");
    kprintf("User programs will not be able to read/write\n");
    cons_vnode = NULL;
  }
}

/*
 * mk_useruio
 * sets up the uio for a USERSPACE transfer. 
 */
static
void
mk_useruio(struct iovec *iov, struct uio *u, userptr_t buf, 
	   size_t len, off_t offset, enum uio_rw rw)
{

	iov->iov_ubase = buf;
	iov->iov_len = len;
	u->uio_iov = iov;
	u->uio_iovcnt = 1;
	u->uio_offset = offset;
	u->uio_resid = len;
	u->uio_segflg = UIO_USERSPACE;
	u->uio_rw = rw;
	u->uio_space = curthread->t_addrspace;
}

/*
 * sys_open
 * just copies in the filename, then passes work to file_open.
 * You have to write file_open.
 * 
 */
int
sys_open(userptr_t filename, int flags, int mode, int *retval)
{
	char *fname;
	int result;

	if ( (fname = (char *)kmalloc(__PATH_MAX)) == NULL) {
		return ENOMEM;
	}

	result = copyinstr(filename, fname, __PATH_MAX, NULL);
	if (result) {
		kfree(fname);
		return result;
	}

	result =  file_open(fname, flags, mode, retval);
	kfree(fname);
	return result;
}

/* 
 * sys_close
 * You have to write file_close.
 */
int
sys_close(int fd)
{
	return file_close(fd);
}

/* 
 * sys_dup2
 * 
 * Design: dup2 will incerement the refetence counter.
 * So have to call close on both new and old fd
 * to actually fully close the file.
 *
 */
int
sys_dup2(int oldfd, int newfd, int *retval)
{
	//Fist, check that file descriptors are unique
	if (oldfd == newfd){
		//Do nothing
		*retval = 0;
		return 0;
	}

	if (&oldfd == NULL || &newfd == NULL){
		*retval = -1;
		return EBADF;
	}


	// See if newfd is already being used. It's an open file
	if (curthread->t_filetable->t_entries[newfd] != NULL){
		sys_close(newfd);
	}

	// Set add the vnode pointer to the file table at index newfd.
	curthread->t_filetable->t_entries[newfd] = curthread->t_filetable->t_entries[oldfd];
	// Increment reference
	VOP_INCREF(curthread->t_filetable->t_entries[newfd]);

	*retval = 0;
	return 0;
}

/*
 * sys_read
 * calls VOP_READ.
 * 
 * A3: This is the "dumb" implementation of sys_write:
 * it only deals with file descriptors 1 and 2, and 
 * assumes they are permanently associated with the 
 * console vnode (which must have been previously initialized).
 *
 * In your implementation, you should use the file descriptor
 * to find a vnode from your file table, and then read from it.
 *
 * Note that any problems with the address supplied by the
 * user as "buf" will be handled by the VOP_READ / uio code
 * so you do not have to try to verify "buf" yourself.
 *
 * Most of this code should be replaced.
 */
int
sys_read(int fd, userptr_t buf, size_t size, int *retval)
{
	struct uio user_uio;
	struct iovec user_iov;
	int result;
	// Offset is in vnode
	//int offset = 0; // Probably needs to be saved elsewhere, don't want to read from start all the time

	// We will get off set from v_node

	/* Make sure we were able to init the cons_vnode */
	//if (cons_vnode == NULL) {
	//  return ENODEV;
	//}

	//EINVAL <- invalid parameter
	if (size <= 0){
		*retval = -1;
		return EINVAL;
	}

	/* better be a valid file descriptor */
	if (fd > __OPEN_MAX) {
		*retval = -1;
		return EBADF;
	}

	// Lock down the file table.
	lock_acquire(curthread->t_filetable->t_lock);

	// Using FD get vnode from procces's filetable
	// Note: Open should have been used b4 to load the needed info onto the filetable.
	struct vnode *fileToRead = curthread->t_filetable->t_entries[fd];

	if (fileToRead == NULL){
		*retval = -1;
		lock_release(curthread->t_filetable->t_lock);
		return EBADF;
	}

	off_t offset = fileToRead->offset;

	/* set up a uio with the buffer, its size, and the current offset */
	mk_useruio(&user_iov, &user_uio, buf, size, offset, UIO_READ);

	/* does the read */
	result = VOP_READ(fileToRead, &user_uio);
	if (result) {
		lock_release(curthread->t_filetable->t_lock);
		return result;
	}

	/*
	 * The amount read is the size of the buffer originally, minus
	 * how much is left in it.
	 */
	*retval = size - user_uio.uio_resid;

	// Update the offset
	fileToRead->offset = offset + *retval;

	// Release the file table.
	lock_release(curthread->t_filetable->t_lock);

	return 0;
}

/*
 * sys_write
 * calls VOP_WRITE.
 *
 * A3: This is the "dumb" implementation of sys_write:
 * it only deals with file descriptors 1 and 2, and 
 * assumes they are permanently associated with the 
 * console vnode (which must have been previously initialized).
 *
 * In your implementation, you should use the file descriptor
 * to find a vnode from your file table, and then read from it.
 *
 * Note that any problems with the address supplied by the
 * user as "buf" will be handled by the VOP_READ / uio code
 * so you do not have to try to verify "buf" yourself.
 *
 * Most of this code should be replaced.
 */

int
sys_write(int fd, userptr_t buf, size_t len, int *retval) 
{
	struct uio user_uio;
	struct iovec user_iov;
	int result, offset;

	// Check for invalid parameter.
	if (len <= 0) {*retval = -1; return EINVAL;}

	// Check for invalid file descriptor.
	if (fd > __OPEN_MAX) {*retval = -1; return EBADF;}

	// Lock down the file table.
	lock_acquire(curthread->t_filetable->t_lock);

	// Grab vnode using fd from procces's filetable and obtain it's offset.
	struct vnode *fileToWrite = curthread->t_filetable->t_entries[fd];
	offset = fileToWrite->offset;

	// Make sure vnode exists.
	if (fileToWrite == NULL){
		lock_release(curthread->t_filetable->t_lock); *retval = -1; return EBADF;
	}

	// Setup a uio with the buffer, its size, and it's current offset.
	mk_useruio(&user_iov, &user_uio, buf, len, offset, UIO_WRITE);

	// Pass work to VOP_WRITE.
	if ((result = VOP_WRITE(fileToWrite, &user_uio))) {
		lock_release(curthread->t_filetable->t_lock); *retval = -1; return result;
	}

	// Set return value to the original size of the buffer, minus how much is left in it.
	*retval = len - user_uio.uio_resid;

    // Set new offset.
	fileToWrite->offset = user_uio.uio_offset;

    // Release the file table.
    lock_release(curthread->t_filetable->t_lock);

    // Success.
    return 0;
}

/*
 * sys_lseek
 * 
 */
int
sys_lseek(int fd, off_t pos, int whence, off_t *retval)
{
	// Make sure fd is valid
	if (fd > __OPEN_MAX) {
		*retval = -1;
		return EBADF;
	}

	// Get the vnode to alter offset in
	struct vnode *toSeek;
	toSeek = curthread->t_filetable->t_entries[fd];

	// First check the vnode is valid:
	if (toSeek == NULL){
		*retval = -1;
		return EBADF;
	}
	// Get the old offset
	off_t oldOffSet = toSeek->offset;

	// Will be used to set new offset.
	off_t toSetOffSet;

	struct stat *fileInfo;

	// Use whence to figure out what to do:
	switch(whence){
		case SEEK_SET: //pos is new offset
			toSetOffSet = pos;
			break;
		case SEEK_CUR: //pos + current offset, is new offset
			toSetOffSet = pos + oldOffSet;
			break;
		case SEEK_END: //size of file + pos, is new offset
			// Need to get size of file
			// Create a stat struct, use sys_fsta to populate struct, get size.
			fileInfo = (struct stat *)kmalloc(sizeof(struct stat));
			// kmalloc success?
			if (fileInfo == NULL){
				return ENOMEM;
			}
			// use sys_fstat to populate struct
			int result;
			result = sys_fstat(fd, (userptr_t) fileInfo);
			// success on syscall?
			if (result) {
				*retval = -1;
				return result;
			}

			toSetOffSet = (fileInfo->st_size) + pos;
			break;

		default:
			// invalid flag
			*retval = -1;
			return EINVAL;
	}
	// Offset cannot be negative
	if (toSetOffSet < 0){
		*retval = -1;
		return EINVAL;
	}
	int seekResult;
	seekResult = VOP_TRYSEEK(toSeek, toSetOffSet);
	if (seekResult){
		//failed
		*retval = -1;
		return seekResult;
	}
	else{
		//success, set offset.
		toSeek->offset = toSetOffSet;
		*retval = 0;
		return 0;
	}
}


/*
 * sys_chdir
 * Just copies in the pathname, then passes work to vfs_chdir.
 * 
 */
int
sys_chdir(userptr_t pathname)
{
    char *path;
    int result;

    // Allocate memory for new path variable.
    if (!(path = (char *)kmalloc(__PATH_MAX))) {return ENOMEM;}

    // Copy in the given pathname into new path variable.
    if ((result = copyinstr(pathname, path, __PATH_MAX, NULL))){
        kfree(path); return result;
    }

    // Pass work to vsf_chdir.
    if ((result = vfs_chdir(path))) {return result;}

    return 0;

}

/*
 * sys___getcwd
 * 
 */
int
sys___getcwd(userptr_t buf, size_t buflen, int *retval)
{

    int result;
	struct uio user_uio;
	struct iovec user_iov;

	// Set up a uio with the buffer, its size, and offset of 0.
    mk_useruio(&user_iov, &user_uio, buf, buflen, 0, UIO_READ);

    // Pass work to vsf_getcwd.
    if ((result = vfs_getcwd(&user_uio))) {*retval = -1; return result;}

    // Set return value to the original size of the buffer, minus how much is left in it.
    *retval = buflen - user_uio.uio_resid;

    // Return Success.
    return 0;

}

/*
 * sys_fstat: returns 0 on success and -1 on error. retrieves uio referenced
 * by fd and stores it into the stat structure pointed to by statptr.
 */
int
sys_fstat(int fd, userptr_t statptr)
{

	// initialize neccesary structs for mk_useruio.
	struct vnode *file;
    struct uio u_uio;
    struct iovec u_iov;
    // for some statistic
    struct stat st;
    int result;

    // make sure fd is ok.
    if (fd >= __OPEN_MAX || fd < 0){
        return EBADF;
    }

    // now we get the lock.
    lock_acquire(curthread->t_filetable->t_lock);

    // get the fe from t_entries!
    if (!(file = curthread->t_filetable->t_entries[fd])){
        kprintf("bad fd for fstat!");
        lock_release(curthread->t_filetable->t_lock);
        return EBADF;
    }

    if ((result = VOP_STAT(file, &st))){
    	lock_release(curthread->t_filetable->t_lock);
    	return result;
    }
    // set up uio for r/w
    mk_useruio(&u_iov, &u_uio, statptr, sizeof(struct stat), 0, UIO_READ);

    // copy stat data to uio defined by u_uio.
    if ((result = uiomove(&st,sizeof(struct stat),&u_uio))){
    	lock_release(curthread->t_filetable->t_lock);
    	return result;
    }

    // release
    lock_release(curthread->t_filetable->t_lock);
    return result;
}

/*
 * sys_getdirentry
 */
int
sys_getdirentry(int fd, userptr_t buf, size_t buflen, int *retval)
{
    int result, offset;
    struct uio user_uio;
    struct iovec user_iov;
    struct vnode* entry;

    // Lock down the file table.
    lock_acquire(curthread->t_filetable->t_lock);

    // Grab file table entry and it's offset.
    entry = curthread->t_filetable->t_entries[fd];
    offset = entry->offset;

    // Set up a uio with the buffer, its size, and offset.
    mk_useruio(&user_iov, &user_uio, buf, buflen, offset, UIO_READ);

    // Pass work to VOP_GETDIRENTRY.
    if((result = VOP_GETDIRENTRY(entry, &user_uio))){
        lock_release(curthread->t_filetable->t_lock); *retval = -1; return result;
    }

    // Set return value to the original size of the buffer, minus how much is left in it.
    *retval = buflen - user_uio.uio_resid;

    // Add new offset.
    entry->offset = user_uio.uio_offset;

    // Release the file table.
    lock_release(curthread->t_filetable->t_lock);

    // Success.
    return 0;
}


/* END A3 SETUP */




