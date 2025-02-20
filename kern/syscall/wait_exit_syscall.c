#include <syscall.h>
#include <proc.h>
#include <current.h>
#include <kern/errno.h>
#include <wait.h>
/*
 *
 *
 * This file includes the syscalls related to exit and wait since they 
 * are both related in some ways
 * 
 * 
 */




/*
 * Sys exit exit - causes the program to exit. 
 * It calls internal cleanup routines,
 *  and then performs the actual exit by calling _exit.
 * Note : For now we do not take wait into consideration and think that 
 * we only destroy the process
 * 
 * TODO : Send status of child
 */
int sys_exit(){
    /*
     * For now we take the easy way out :
     * 1. We consider that we only have 1 thread 
     * 
     * 2. We just signal the parents cv, exit the thread and destroy its process :)
     * 
     *      TODO : send the exited processes exit status to parent
     */

    /* If we have a parent (we are not init proc) 
     *  which is waiting for us, send 
     *  a signal to parent's cv channel to wake it up
    */
    if (curproc->parent != NULL && curproc->parent->waiting_for_pid == curproc -> pid){
        lock_acquire(curproc->parent->cv_lock);
            curproc->parent->waiting_for_pid = 0x0;
            cv_signal(curproc->parent->cv, curproc->parent->cv_lock);
        lock_release(curproc->parent->cv_lock);
    }

    thread_exit();
    /* We would not get here */
    return 0;
}

/*
 * Sys Wait - Wait for the process specified by pid to exit,
 * and return an encoded exit status in the integer pointed to by status.
 * If that process has exited already, waitpid returns immediately.
 * If that process does not exist, waitpid fails.
 * 
 * We simply implement this function with the use of the 
 * synchronization primitive, conditional variable
 *
 * TODO : Get status of child
*/

int sys_wait(pid_t pid, int *status, int options, int *retval){
    (void) status;
    (void) options;
    int err;

    err = 0;
    lock_acquire(pid_lock);
    struct proc *proc = array_get(process_table, (unsigned int)pid);
    if (proc == NULL){
        *retval = -1;
        return ESRCH;
    } 
    if (proc -> parent != curproc) {
        *retval = -1;
        return ECHILD;
    }
    lock_release(pid_lock);

    if (options == WNOHANG){
        *retval  = 0;
        return err;
    }

    /* Now Do The Actual Waiting 
     * We use a condvar to put the parent in a 
     * sleeping state until the child wakes it up :)
    */
    lock_acquire(curproc->cv_lock);
    curproc -> waiting_for_pid = pid;
    *retval = pid;
	while(curproc->waiting_for_pid > 0)
		cv_wait(curproc->cv, curproc->cv_lock);
	lock_release(curproc->cv_lock);

    return err;
}