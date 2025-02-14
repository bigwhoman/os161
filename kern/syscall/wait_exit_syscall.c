#include <syscall.h>
#include <proc.h>
#include <current.h>
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

    /* If we have a parent (we are not init proc), send 
     *  a signal to parent's cv channel to wake it up
    */
    if (curproc->parent != NULL){
        lock_acquire(curproc->parent->cv_lock);
        cv_signal(curproc->parent->cv, curproc->parent->cv_lock);
        lock_release(curproc->parent->cv_lock);
    }

    thread_exit();
    proc_destroy(curproc);
    return 0;
}

/*
 * Sys Wait - Wait for the process specified by pid to exit,
 * and return an encoded exit status in the integer pointed to by status.
 * If that process has exited already, waitpid returns immediately.
 * If that process does not exist, waitpid fails.
 * 
 * We simply implement this function with the use of the 
 * synchronization primitive, conditional variables
 * when sys_wait is called a conditional vaiable is 
*/

int sys_wait(pid_t pid, int *status, int options, int *retval){
    (void) pid;
    (void) status;
    (void) options;
    (void) retval;

    return 0;
}