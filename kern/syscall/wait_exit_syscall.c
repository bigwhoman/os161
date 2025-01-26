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
 * Note : For now we do not take wait into consideration and think that 
 * we only destroy the process
 */
int sys_exit(){
    /*
     * For now we take the easy way out :
     * 1. We consider that we only have 1 thread 
     * 
     * 2. We just exit that thread and destroy its process :)
     */

    thread_exit();
    proc_destroy(curproc);
    return 0;
}