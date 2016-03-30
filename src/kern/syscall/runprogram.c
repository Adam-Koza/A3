/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <thread.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>

/*
 * helper_size_to_allocate(string)
 * Helper function, given a string will return size of space needed (in divisions of 4)
 * Example: if string (null \0 terminating) is of size 5, will return value 8.
*/
static int helper_size_to_allocate(char *arg){
        int length = strlen(arg);
        int isTooSmall;
        int toReturn = 4;
        isTooSmall = length / 4;
        //if 0 we are done, it fits
        //if 1 or bigger, 
        toReturn = toReturn + (4 * isTooSmall);
        return toReturn;
}

/*
 * insertArgToStack (Stack pointer in userspace, string to insert)
 * Given a pointer to address in user space, moves the pointer down
 * and then will insert a string into it.
 *
 */
static int insertArgToStack(vaddr_t *stcPt, char *arg)
{
	int length = helper_size_to_allocate(arg);
	*stcPt = *stcPt - length;
	int result;
	result  = copyout(arg, (userptr_t)(*stcPt), length); // &returnLength);
	return result;

}

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */
int
runprogram(char *progname, unsigned long nargs, char **args)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new thread. */
	KASSERT(curthread->t_addrspace == NULL);

	/* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_addrspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_addrspace, &stackptr);
	// userptr_t addrOfArg = (userptr_t)stackptr; //mOVED
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		return result;
	}

	//Will store my pointers of arguments here, put in stack later.
	vaddr_t ptrToArgAddressInStack[nargs];
	
	// Had issues with data being stored at start of the stack.
	// Moving down to avoid writing/reading issue.
	stackptr = stackptr - 4;

	// Insert strings (my arguments), and store the address (in stack)
	// which we get from stack pointer into my array I defined earlier.
	unsigned long i;
	for (i = 0; i <	nargs; i++){
		result = insertArgToStack(&stackptr, args[i]);
		if (result){
			return result;
		}
		ptrToArgAddressInStack[i] = stackptr;
	}

	//Add a Null 4 byte buffer
	int fourBlank = (int)NULL;
	stackptr = stackptr - 4;
	result = copyout(&fourBlank, (userptr_t)stackptr, 4);

	if (result){
		return result;
	}

	// Insert the address values into the stack
	for (i = nargs; i > 0 ; i--){
		stackptr = stackptr - 4; 
		copyout(&ptrToArgAddressInStack[(i - 1)], (userptr_t)stackptr, 4);

	}
	userptr_t addrOfArg = (userptr_t) (stackptr);

	/* Warp to user mode. */
	enter_new_process(nargs /*argc*/, addrOfArg /*userspace addr of argv*/,
			  stackptr, entrypoint);
	
	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}



	

