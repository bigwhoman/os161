#include <syscall.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <mips/trapframe.h>

/* fork duplicates the currently running process. 
 * The two copies are identical, except that one (
 * the "new" one, or "child"), has a new, unique process id,
 *  and in the other (the "parent") the process id is unchanged. 
 */
int sys_fork(struct trapframe *tf, int *retval){
    struct proc *newproc;
    // struct addrspace *new_as;
    // vaddr_t stackptr;
    int result;
	newproc = proc_create_runprogram(curproc -> p_name /* name */);
	if (newproc == NULL) {
		return ENOMEM;
    }

	

    struct trapframe *new_tf;
    new_tf = kmalloc(sizeof(*tf));

    memcpy(new_tf, tf, sizeof(*tf));

    newproc -> parent = curproc;
    DEBUG(DB_GEN, "Proc Forked %p (%d) - Parent %p (%d) \n", newproc, newproc -> pid, curproc, curproc->pid);
    /* VFS fields */

    for (size_t i = 0; i < MAX_FD; i++)
    {
        lock_acquire(curproc -> fd_lock[i]);
        newproc -> fd_table[i] = curproc -> fd_table[i];
        newproc -> fd_pos[i] = curproc -> fd_pos[i];
        newproc -> fd_lock[i] = curproc -> fd_lock[i];
         
       /* We need to add the locking system for this */ 
        newproc -> fd_count[i] = curproc -> fd_count[i];
        if (newproc->fd_table[i] != NULL){
            *newproc->fd_count[i] += 1;
            VOP_INCREF(newproc -> fd_table[i]);
        }
        lock_release(curproc -> fd_lock[i]); 
    }

    newproc -> stdin = curproc -> stdin;
    newproc -> stdout = curproc -> stdout;
    newproc -> stderr = curproc -> stderr;
    newproc -> exited = false;

    // as_define_stack(newproc->p_addrspace, &stackptr);

    /* VM fields */
    as_copy(curproc -> p_addrspace, &newproc->p_addrspace);

    
    result = thread_fork(curproc -> p_name/* thread name */,
			newproc /* new process */,
			enter_forked_process /* thread function */,
			new_tf /* thread arg */, 0 /* thread arg */);
	if (result) {
		kprintf("thread_fork failed: %s\n", strerror(result)); 
	    lock_acquire(pid_lock);	
		proc_destroy(newproc);
        lock_release(pid_lock);
        *retval = -1;
		return result;
	}
    *retval = newproc -> pid;
    return 0;
}

/* getpid returns the process id of the current process. */
int sys_getpid(){
    return curproc -> pid;
}