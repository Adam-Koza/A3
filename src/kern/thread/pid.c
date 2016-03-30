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
 * Process ID management.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/wait.h>
#include <limits.h>
#include <lib.h>
#include <array.h>
#include <clock.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <pid.h>
#include <signal.h>

/*
 * Simple structure to store child process id's
 * It a linked list.
 * To initiate, it will store it's own id as first child.
 * (Could be any dummy data, as it is not suppose to be accessed)
 */
struct childpids {
	pid_t pi_cpid; // process id of child thread.
	struct childpids *nextchild; // next child
};

//Adds a new child process id given list head
static
void addchildpid(struct childpids *listchilds, pid_t pi_cpid){
	struct childpids *newHead;
	newHead = kmalloc(sizeof(struct childpids));
	newHead->pi_cpid = pi_cpid;
	newHead->nextchild = listchilds;
	listchilds = newHead;
}
// Free the whole linked list structure
// Should be done when pidinfo destroy
static
void freelistchilds(struct childpids *listchilds){
	struct childpids *newHead;
	while (listchilds->nextchild != NULL){ //The first pid is itself, which we don't want to touch{
		newHead = listchilds->nextchild;
		kfree(listchilds);
		listchilds = newHead;
	}
	kfree(listchilds);
}

/*
 * Structure for holding PID and return data for a thread.
 *
 * If pi_ppid is INVALID_PID, the parent has gone away and will not be
 * waiting. If pi_ppid is INVALID_PID and pi_exited is true, the
 * structure can be freed.
 */
struct pidinfo {
	pid_t pi_pid;			// process id of this thread
	pid_t pi_ppid;			// process id of parent thread
	struct childpids *pi_spids;		// process ids of children of thread
	volatile bool pi_exited;	// true if thread has exited
	volatile bool detached;		// true if thread is detached
	int pi_exitstatus;		// status (only valid if exited)
	int flag;
	struct cv *pi_cv;		// use to wait for thread exit
};


/*
 * Global pid and exit data.
 *
 * The process table is an el-cheapo hash table. It's indexed by
 * (pid % PROCS_MAX), and only allows one process per slot. If a
 * new pid allocation would cause a hash collision, we just don't
 * use that pid.
 */
static struct lock *pidlock;		// lock for global exit data
static struct pidinfo *pidinfo[PROCS_MAX]; // actual pid info
static pid_t nextpid;			// next candidate pid
static int nprocs;			// number of allocated pids

/*
 * pi_get: look up a pidinfo in the process table.
 * Moved up as it is needed in pidinfo_create
 */
static
struct pidinfo *
pi_get(pid_t pid)
{
	struct pidinfo *pi;

	KASSERT(pid>=0);
	KASSERT(pid != INVALID_PID);
	KASSERT(lock_do_i_hold(pidlock));

	pi = pidinfo[pid % PROCS_MAX];
	if (pi==NULL) {
		return NULL;
	}
	if (pi->pi_pid != pid) {
		return NULL;
	}
	return pi;
}

/*
 * Create a pidinfo structure for the specified pid.
 */
static
struct pidinfo *
pidinfo_create(pid_t pid, pid_t ppid)
{
	struct pidinfo *pi;

	KASSERT(pid != INVALID_PID);

	pi = kmalloc(sizeof(struct pidinfo));
	if (pi==NULL) {
		return NULL;
	}

	pi->pi_cv = cv_create("pidinfo cv");
	if (pi->pi_cv == NULL) {
		kfree(pi);
		return NULL;
	}

	// Initialize the pid's children list
	// Will add it's own pid as first item.
	// This is a dummy first node that shouldn't ever be read.
	pi->pi_spids = kmalloc(sizeof(struct childpids));
	pi->pi_spids->nextchild = NULL;
	pi->pi_spids->pi_cpid = pi->pi_pid;

	pi->pi_pid = pid;
	pi->pi_ppid = ppid;
	pi->pi_exited = false;
	pi->pi_exitstatus = 0xbaad;  /* Recognizably invalid value */
	pi->detached = false;

	// Store pid into parent's children list
	struct pidinfo *ppi;
	if (pid != BOOTUP_PID){
		ppi = pi_get(ppid);
		addchildpid(ppi->pi_spids, pid);
	}

	return pi;
}

/*
 * Clean up a pidinfo structure.
 */
static
void
pidinfo_destroy(struct pidinfo *pi)
{
	KASSERT(pi->pi_exited == true);
	KASSERT(pi->pi_ppid == INVALID_PID);
	cv_destroy(pi->pi_cv);
	freelistchilds(pi->pi_spids);
	kfree(pi);
}

////////////////////////////////////////////////////////////

/*
 * pid_bootstrap: initialize.
 */
void
pid_bootstrap(void)
{
	int i;

	pidlock = lock_create("pidlock");
	if (pidlock == NULL) {
		panic("Out of memory creating pid lock\n");
	}

	/* not really necessary - should start zeroed */
	for (i=0; i<PROCS_MAX; i++) {
		pidinfo[i] = NULL;
	}

	pidinfo[BOOTUP_PID] = pidinfo_create(BOOTUP_PID, INVALID_PID);
	if (pidinfo[BOOTUP_PID]==NULL) {
		panic("Out of memory creating bootup pid data\n");
	}

	nextpid = PID_MIN;
	nprocs = 1;
}

/*
 * pi_get: look up a pidinfo in the process table.
 */
static
struct pidinfo *
pi_get(pid_t pid)
{
	struct pidinfo *pi;

	KASSERT(pid>=0);
	KASSERT(pid != INVALID_PID);
	KASSERT(lock_do_i_hold(pidlock));

	pi = pidinfo[pid % PROCS_MAX];
	if (pi==NULL) {
		return NULL;
	}
	if (pi->pi_pid != pid) {
		return NULL;
	}
	return pi;
}

/*
 * pi_put: insert a new pidinfo in the process table. The right slot
 * must be empty.
 */
static
void
pi_put(pid_t pid, struct pidinfo *pi)
{
	KASSERT(lock_do_i_hold(pidlock));

	KASSERT(pid != INVALID_PID);

	KASSERT(pidinfo[pid % PROCS_MAX] == NULL);
	pidinfo[pid % PROCS_MAX] = pi;
	nprocs++;
}

/*
 * pi_drop: remove a pidinfo structure from the process table and free
 * it. It should reflect a process that has already exited and been
 * waited for.
 */
static
void
pi_drop(pid_t pid)
{
	struct pidinfo *pi;

	KASSERT(lock_do_i_hold(pidlock));

	pi = pidinfo[pid % PROCS_MAX];
	KASSERT(pi != NULL);
	KASSERT(pi->pi_pid == pid);

	pidinfo_destroy(pi);
	pidinfo[pid % PROCS_MAX] = NULL;
	nprocs--;
}

////////////////////////////////////////////////////////////

/*
 * Helper function for pid_alloc.
 */
static
void
inc_nextpid(void)
{
	KASSERT(lock_do_i_hold(pidlock));

	nextpid++;
	if (nextpid > PID_MAX) {
		nextpid = PID_MIN;
	}
}

/*
 * pid_alloc: allocate a process id.
 */
int
pid_alloc(pid_t *retval)
{
	struct pidinfo *pi;
	pid_t pid;
	int count;

	KASSERT(curthread->t_pid != INVALID_PID);

	/* lock the table */
	lock_acquire(pidlock);

	if (nprocs == PROCS_MAX) {
		lock_release(pidlock);
		return EAGAIN;
	}

	/*
	 * The above test guarantees that this loop terminates, unless
	 * our nprocs count is off. Even so, assert we aren't looping
	 * forever.
	 */
	count = 0;
	while (pidinfo[nextpid % PROCS_MAX] != NULL) {

		/* avoid various boundary cases by allowing extra loops */
		KASSERT(count < PROCS_MAX*2+5);
		count++;

		inc_nextpid();
	}

	pid = nextpid;

	pi = pidinfo_create(pid, curthread->t_pid);
	if (pi==NULL) {
		lock_release(pidlock);
		return ENOMEM;
	}

	pi_put(pid, pi);

	inc_nextpid();

	lock_release(pidlock);

	*retval = pid;
	return 0;
}

/*
 * pid_unalloc - unallocate a process id (allocated with pid_alloc) that
 * hasn't run yet.
 */
void
pid_unalloc(pid_t theirpid)
{
	struct pidinfo *them;

	KASSERT(theirpid >= PID_MIN && theirpid <= PID_MAX);

	lock_acquire(pidlock);
	// kprintf("I'm in unalloc");
	them = pi_get(theirpid);
	KASSERT(them != NULL);
	KASSERT(them->pi_exited == false);
	KASSERT(them->pi_ppid == curthread->t_pid);

	/* keep pidinfo_destroy from complaining */
	them->pi_exitstatus = 0xdead;
	them->pi_exited = true;
	them->pi_ppid = INVALID_PID;

	pi_drop(theirpid);

	lock_release(pidlock);
}

/*
 * pid_detach - disavows interest in the child thread's exit status, so 
 * it can be freed as soon as it exits. May only be called by the
 * parent thread.
 */
int
pid_detach(pid_t childpid)
{
	// Return EINVAL if pid for child is invalid.
	if (childpid == INVALID_PID
			|| childpid == BOOTUP_PID){
		return EINVAL;
	}
	
	// Initialize pidinfo struct
	struct pidinfo *pi;
	lock_acquire(pidlock);

	// Use pi_get() to find info of childpid
	pi = pi_get(childpid);
	
	// If childpid is already exited and no thread can be found.
	if (pi == NULL) {
		lock_release(pidlock);
		return ESRCH;
	}


	// Check if pi is invalid or in a joinable state.
	if (pi->detached == true || pi->pi_ppid != curthread->t_pid){
		lock_release(pidlock);
		return EINVAL;
	}

	
	// Check if pi has exited, if it has then drop childpid.
	// Else, set detached state to true.
	if (pi->pi_exited == true){
		pi_drop(childpid);
	}else {
		pi->detached = true;
	}

	// Release pidlock.
	lock_release(pidlock);
	return 0;
}

/*
 * pid_exit 
 *  - sets the exit status of this thread (i.e. curthread). 
 *  - disowns children. 
 *  - if dodetach is true, children are also detached. 
 *  - wakes any thread waiting for the curthread to exit. 
 *  - frees the PID and exit status if the curthread has been detached.
 *  - must be called only if the thread has had a pid assigned. (Done)
 */
void
pid_exit(int status, bool dodetach, struct thread *cur)
{
	struct pidinfo *my_pi;
	

	lock_acquire(pidlock);
	my_pi = pi_get(cur->t_pid);
	KASSERT(my_pi != NULL);
	// Set status, and set exited to true
	my_pi->pi_exitstatus = status;
	my_pi->pi_exited = true;
	my_pi->pi_ppid = INVALID_PID; // For pi_drop

	lock_release(pidlock);

	// If dodeach, will detach itself and all children
	struct childpids *childlist_pt;
	if (dodetach) {
		// detach self
		pid_detach(my_pi->pi_pid);
		// detach children
		childlist_pt = my_pi->pi_spids;
		while (childlist_pt != NULL){
				pid_detach(childlist_pt->pi_cpid);
				childlist_pt = childlist_pt->nextchild; // Next
			}
	}

	lock_acquire(pidlock);

	// Wake up threads waiting on cur
	cv_broadcast(my_pi->pi_cv, pidlock);


	//If exiting thread was detached not in this function, free PID and exit status
	if ((my_pi->detached == true)){
			pi_drop(cur->t_pid);
	}


	lock_release(pidlock);


}

/*
 * pid_join - returns the exit status of the thread associated with
 * targetpid as soon as it is available. If the thread has not yet 
 * exited, curthread waits unless the flag WNOHANG is sent. 
 *
 */
int
pid_join(pid_t targetpid, int *status, int flags)
{	
	//Determine if targetpid is a valid process id.
	if (targetpid == INVALID_PID || targetpid == BOOTUP_PID ||
			PID_MIN > targetpid || PID_MAX < targetpid){
		return EINVAL * -1;
	}

	//Determine if targetpid is the same as the current (calling thread).
	if (targetpid == curthread->t_pid){
		return EDEADLK * -1;
	}
	
	//Grab lock.
	lock_acquire(pidlock);
	
	//Look up a pidinfo of targetpid in the process table.
	struct pidinfo *targetinfo;
	targetinfo = pi_get(targetpid);
	
	//Check if targetpid thread exists.
	if (targetinfo == NULL){
		lock_release(pidlock);
		return ESRCH * -1;
	}

	//Make sure that targetpid has not been detached and is in a joinable state.
	if (targetinfo->detached){
		lock_release(pidlock);
		return EINVAL * -1;
	}
	
	//If the target thread has not been exited and WHNOHANG flag has not been sent
	//make current thread wait.
	if (targetinfo->pi_exited == false) {
		if (flags != WNOHANG){
			//Release the supplied lock, go to sleep, and, after waking up again,
			//re-acquire the lock.
			cv_wait(targetinfo->pi_cv, pidlock);
		} else {
			//Release the supplied lock, return successful operation.
			lock_release(pidlock);
			return 0;
		}
	}
	
	//If status is non empty, we store the exit status of targetpid.
	if (status != NULL){
		*status = targetinfo->pi_exitstatus;
	}

	//Release the lock, return targetpid.
	lock_release(pidlock);
	return targetpid;
	
}
/*
 * pid_setflag - sets the flag of the given procces.
 * returns ERSCH if either process or flag is invalid.
 *
 */
int
pid_setflag(pid_t pid, int flag){

	// check if flag is valid
		if (flag < 0 || flag > 31){
			return -EINVAL;
		}
	// ok... check if flag is implemented
		if (flag != SIGINT &&
				flag != SIGKILL && flag != SIGTERM &&
				flag != SIGSTOP && flag != SIGCONT &&
				flag != SIGHUP && flag != SIGWINCH &&
				flag != SIGINFO && flag != 0){
			return -EUNIMP;
		}
		lock_acquire(pidlock);
		if (pid > PID_MAX || pid < PID_MIN || pid == INVALID_PID){
			lock_release(pidlock);
			return -ESRCH;
		}
		// initialize struct now!
		struct pidinfo* pi = pi_get(pid);
		// if pi is fucked, return lock.
		if (!pi){
			lock_release(pidlock);
			return -ESRCH;
		}
		// set flag and return -0 for success!
		pi->flag=flag;
		lock_release(pidlock);
		return 0;
		
}
/*
 * get flag from pid.
 */
int
pid_getflag(pid_t pid)
{
	// GET pidlock
	lock_acquire(pidlock);
	// check that pid that's passed in is actually correct.
	if (pid > PID_MAX || pid < PID_MIN || pid == INVALID_PID){
                       lock_release(pidlock);
                        return ESRCH;
        }
	struct pidinfo* pi = pi_get(pid);
	if (pi==NULL){
                        lock_release(pidlock);
                        return ESRCH;
        }
	int flag = pi->flag;
	lock_release(pidlock);
	return flag;
}
