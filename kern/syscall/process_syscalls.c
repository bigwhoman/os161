#include <syscall.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <mips/trapframe.h>
#include <kern/wait.h>
#include <copyinout.h>


/* execv replaces the currently executing program with
 * a newly loaded program image.
 * This occurs within one process; the process id is unchanged.
 *
 *
 * TODO : Handle Errors */
int sys_execv(const char *program, char *argv[], int *retval){
	struct addrspace *as;
	struct addrspace *old_as;
    vaddr_t stackptr, entrypoint;
    int err;
    int argc;
    size_t i, all;
    char *argin; 
    char prog[PATH_MAX];
    size_t got;
	as = NULL;
	argc = 0;
    

	old_as = curproc ->p_addrspace;
    if (argv == NULL)
    {
        err = EFAULT;
        *retval = -1;
        return err;
    }

    for (size_t i = 0; ; i++)
	{
        argin = kmalloc(PATH_MAX);
        err = copyinstr((const_userptr_t)(argv + i), argin, PATH_MAX, &got);
        if (err)
        {
            kfree(argin);
            *retval = -1;
            return err;
        }
        if (argin == NULL || got <= 0)
        {
            err = ENOENT;
            *retval = -1;
            return err;
        }
       
        kfree(argin);
        if(*(argv + i) == NULL)
			break;
		argc++;
        
	}
    
	all = 0;
	
	/* Copy the arguments from old stack */
	for (i = 0; i < (size_t)argc; i++)
	{
        argin = kmalloc(PATH_MAX);
        err = copyinstr((const_userptr_t)(argv[i]), argin, PATH_MAX, &got);
        if (err)
        {
            kfree(argin);
            *retval = -1;
            return err;
        }
        if (argin == NULL || got <= 0)
        {
            err = ENOENT;
            *retval = -1;
            return err;
        }
        /* Need kfree but somehow test fails when we do kfree here :/// */  
		all += strlen(argv[i]) + 1;
	}

	err = copyinstr((const_userptr_t)program, prog, PATH_MAX,(size_t *)retval);

    if (err)
    {
        *retval = -1;
        return err;
    }
    if (program == NULL || *retval <= 0 || 
            argc <= 0 || all <= 0)
    {
        err = ENOENT;
        *retval = -1;
        return err;
    }

    err = open_copy_prog(prog, &as, &entrypoint);
	if (err){
		proc_setas(old_as);
		as_activate();
		*retval = -1;
		return err;
	}

	/* Define the user stack in the address space */
	err = as_define_stack(as, &stackptr);
	if (err) {
		/* p_addrspace will go away when curproc is destroyed */
		proc_setas(old_as);
		as_activate();
		*retval = -1;
		return err;
	}

	

	/* Put arguments onto the stack */
	char* foo; /* this is the variable to move things between old and new address space */
	vaddr_t strloc = (vaddr_t)(stackptr - all);

	/* actuall strings starting location */
	strloc &= 0xfffffffc;

	/* argv pointer location */
	vaddr_t argptr = strloc - (argc + 1) * sizeof(char *);
	*((vaddr_t *)argptr + argc) = 0;
	for (i = 0; i < (size_t)argc; i++)
	{

		/* Move arguments from old stack to new one */
		/* TODO : copy address space instead of this (do it after fixing virtual memory)*/
		proc_setas(old_as);
		as_activate();

		foo = kmalloc(strlen(argv[i]) + 1);
		strcpy(foo, argv[i]);

		proc_setas(as);
		as_activate();

		*((vaddr_t *)argptr + i) = strloc;
		strcpy((char *)strloc, foo);
		strloc += strlen(foo) + 1;
		kfree(foo);
		foo = NULL;
	}

	/* Destroy old address space after migration is done */
	as_destroy(old_as);

	/* Warp to user mode. */
	enter_new_process(argc /*argc*/, (void *)argptr /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  argptr , entrypoint);


    *retval = -1;
    return err;
} 


/* fork duplicates the currently running process. 
 * The two copies are identical, except that one (
 * the "new" one, or "child"), has a new, unique process id,
 *  and in the other (the "parent") the process id is unchanged. 
 */
int sys_fork(struct trapframe *tf, int *retval){
    struct proc *newproc;
    // struct addrspace *new_as;
    // vaddr_t stackptr;
    int err;
	newproc = proc_create_runprogram(curproc -> p_name /* name */);
	if (newproc == NULL) {
		return ENOMEM;
    }

	

    struct trapframe *new_tf;
    new_tf = kmalloc(sizeof(*tf));

    memcpy(new_tf, tf, sizeof(*tf));

    newproc -> parent = curproc;
    DEBUG(DB_GEN, "\nProc Forked %p (%d) - Parent %p (%d) \n", newproc, newproc -> pid, curproc, curproc->pid);
    /* VFS fields */

    for (size_t i = 0; i < MAX_FD; i++)
    {
        lock_acquire(curproc -> fd_lock[i]);
        newproc -> fd_table[i] = curproc -> fd_table[i];
        newproc -> fd_pos[i] = curproc -> fd_pos[i];
        newproc -> fd_lock[i] = curproc -> fd_lock[i];
        newproc -> fd_flags[i] = curproc -> fd_flags[i];
         
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

    
    err = thread_fork(curproc -> p_name/* thread name */,
			newproc /* new process */,
			enter_forked_process /* thread function */,
			new_tf /* thread arg */, 0 /* thread arg */);
	if (err) {
		kprintf("thread_fork failed: %s\n", strerror(err)); 
	    lock_acquire(pid_lock);	
		proc_destroy(newproc);
        lock_release(pid_lock);
        *retval = -1;
		return err;
	}
    *retval = newproc -> pid;
    return 0;
}

/* getpid returns the process id of the current process. */
int sys_getpid(){
    return curproc -> pid;
}


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
int sys_exit(int status){
    /*
     * For now we take the easy way out :
     * 1. We consider that we only have 1 thread 
     * 
     * 2. We just signal the parents cv, exit the thread and destroy its process :)
     * 
     * 
     */

    /* If we have a parent (we are not init proc) 
     *  which is waiting for us, send 
     *  a signal to parent's cv channel to wake it up
    */
    struct proc* parent = curproc->parent;
    if (parent != NULL)
    {
        lock_acquire(parent->cv_lock);
        curproc->exited = true;
        // curproc->parent->waiting_for_pid = 0x0;
        parent->child_status = status;
        cv_signal(curproc->parent->cv, curproc->parent->cv_lock);

        /* Close all file descriptors
         *
         */
        int close_ret;
        int close_err;
        close_ret = 0;
        for (size_t fd = 0; fd < MAX_FD; fd++)
        {
            if (curproc->fd_table[fd] != NULL)
            {
                    close_err = sys_close(fd, &close_ret);
                    if (close_err)
                    {
                        kprintf("Error in closing fd number : %d\n", fd);
                    } 
            }
        }
        DEBUG(DB_PROC, "Proc Exited %p\n", curproc);
        /*
         * Detach from our process. You might need to move this action
         * around, depending on how your wait/exit works.
         */
        proc_remthread(curthread);
        lock_release(parent->cv_lock);
        thread_exit();
    }
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
    int err;
    err = 0;
    if (pid < 0 || pid >= PID_MAX)
    {
        *retval = -1;
        return ESRCH;
    }
    if (options != WNOHANG && options != WAIT_ANY
        && options != WUNTRACED && options != WAIT_MYPGRP){
            *retval = -1;
            err = EINVAL;
            return err;
    }
    int x; 
    err = copyin((const_userptr_t)status, &x, sizeof(int));
    if (err && status != NULL)
    {
        *retval = -1;
        return err;
    }
    if (((int)status & 0x3) != 0){
       *retval = -1;
       err = EFAULT;
       return err; 
    }
    
        
    lock_acquire(pid_lock);
    struct proc *proc;
    proc = array_get(process_table, (unsigned int)pid);
    
    if (proc == NULL){
        *retval = -1;
        lock_release(pid_lock);
        return ESRCH;
    } 
    if (proc -> parent != curproc) {
        *retval = -1;
        lock_release(pid_lock);
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
    *retval = pid;
    DEBUG(DB_GEN, "\nProc %p (%d) waiting on %p (%d) \n", curproc, curproc->pid, proc, proc->pid);
    while (proc->exited != true)
        cv_wait(curproc->cv, curproc->cv_lock);
    DEBUG(DB_GEN, "\nProc %p (%d) done waiting on %p (%d) \n", curproc, curproc->pid, proc, proc->pid);
    lock_acquire(pid_lock);	
    proc_destroy(proc);
	lock_release(pid_lock);	
    if (status != NULL){
        int encode = 0;
        switch(curproc->child_status){
            case __WEXITED :
                encode = _MKWAIT_EXIT(curproc->child_status);
                break;
            case __WCORED :
                encode = _MKWAIT_CORE(curproc->child_status);
                break;
            case __WSIGNALED :
                encode = _MKWAIT_SIG(curproc->child_status);
                break;
            case __WSTOPPED :
                encode = _MKWAIT_STOP(curproc->child_status);
                break;

            /* Not really sure about the default case */
            default :
                encode = _MKWAIT_EXIT(curproc->child_status);
                break;

        }
        *status = encode;
    }
	lock_release(curproc->cv_lock);

    err = 0;
    return err;
}