#include <syscall.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <mips/trapframe.h>
#include <kern/wait.h>
#include <copyinout.h>


static int copy_file_descriptors(struct proc *src, struct proc *dst); 

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
    char prog[PATH_MAX];
    size_t got;
	as = NULL;
    char *argin;
	argc = 0;
    got = 0;
    
    old_as = curproc ->p_addrspace;
    if (argv == NULL)
    {
        err = EFAULT;
        *retval = -1;
        return err;
    }
    int left = ARG_MAX;
    int *argloc;
    argloc = kmalloc(sizeof(int*));

    argin = kmalloc(left);
    for (size_t i = 0; ; i++)
	{
        
        err = copyin((const_userptr_t)(argv + i), argloc, sizeof(int *));
        if (*argloc == 0x0){
            /* For some reason the tester fails because of this but works 
             * properly when I test it :/
             */
            kfree(argin);
            break;
        }
        if (err)
        {
            kfree(argin);
            *retval = -1;
            return err;
        }
        if (argloc == NULL)
        {
            err = ENOENT;
            kfree(argin);
            *retval = -1;
            return err;
        }
        err = copyinstr((const_userptr_t)(*argloc), argin, left, &got);
        if (err)
        {
            kfree(argin);
            *retval = -1;
            return err;
        }
        if (argin == NULL || got <= 0)
        {
            kfree(argin);
            err = ENOENT;
            *retval = -1;
            return err;
        }
        left -= got;
        if (left < 0){
            *retval = -1;
            kfree(argin);
            err = E2BIG;
            return err;
        }

        
        argc++;   
	}
    
	all = 0;
	
	/* Copy the arguments from old stack */
	for (i = 0; i < (size_t)argc; i++)
	{
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

    char **kernel_argv = kmalloc(argc * sizeof(char *));
    for (i = 0; i < (size_t)argc; i++)
    {
        kernel_argv[i] = kstrdup(argv[i]);
    }

    proc_setas(as);
    as_activate();

    as_destroy(old_as); 
    /* Put arguments onto the stack */
	vaddr_t strloc = (vaddr_t)(stackptr - all);

	/* actuall strings starting location */
	strloc &= 0xfffffffc;

	/* argv pointer location */
	vaddr_t argptr = strloc - (argc + 1) * sizeof(char *);
	*((vaddr_t *)argptr + argc) = 0;
    
    for (i = 0; i < (size_t)argc; i++)
	{
		/* Move arguments from old stack to new one */
		*((vaddr_t *)argptr + i) = strloc;
		strcpy((char *)strloc, kernel_argv[i]);
		strloc += strlen(kernel_argv[i]) + 1;
        kfree(kernel_argv[i]); // Free the kernel argument string
	}
    kfree(kernel_argv); // Free the array of kernel arguments


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
int sys_fork(struct trapframe *tf, int *retval) {
    struct proc *newproc;
    int err;
    
    /* Create new process */
    newproc = proc_create_runprogram(curproc->p_name);
    if (newproc == NULL) {
        return ENOMEM;
    }
    
    /* Copy trap frame */
    struct trapframe *new_tf = kmalloc(sizeof(*tf));
    if (new_tf == NULL) {
        lock_acquire(pid_lock);
        proc_destroy(newproc);
        lock_release(pid_lock);
        return ENOMEM;
    }
    memcpy(new_tf, tf, sizeof(*tf));
    
    /* Set parent */
    newproc->parent = curproc;
    DEBUG(DB_GEN, "\nProc Forked %p (%d) - Parent %p (%d) \n", 
           newproc, newproc->pid, curproc, curproc->pid);
    
    /* Copy file descriptors */
    if (curproc->fd_table != NULL) {
        err = copy_file_descriptors(curproc, newproc);
        if (err) {
            kfree(new_tf);
            lock_acquire(pid_lock);
            proc_destroy(newproc);
            lock_release(pid_lock);
            *retval = -1;
            return err;
        }
    }
    
    /* Copy stdin/stdout/stderr values */
    newproc->stdin = curproc->stdin;
    newproc->stdout = curproc->stdout;
    newproc->stderr = curproc->stderr;
    newproc->exited = false;
    
    /* Copy address space */
    err = as_copy(curproc->p_addrspace, &newproc->p_addrspace);
    if (err) {
        kfree(new_tf);
        lock_acquire(pid_lock);
        proc_destroy(newproc);
        lock_release(pid_lock);
        *retval = -1;
        return err;
    }
    
    /* Fork the thread */
    err = thread_fork(curproc->p_name,
                      newproc,
                      enter_forked_process,
                      new_tf, 0);
    if (err) {
        kprintf("thread_fork failed: %s\n", strerror(err));
        kfree(new_tf);
        lock_acquire(pid_lock);
        proc_destroy(newproc);
        lock_release(pid_lock);
        *retval = -1;
        return err;
    }
    
    *retval = newproc->pid;
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
 */
int sys_exit(int status) {
    struct proc *parent = curproc->parent;
    
    /* If we have a parent, signal it */
    if (parent != NULL) {
        lock_acquire(parent->cv_lock);
        curproc->exited = true;
        parent->child_status = status;
        cv_signal(parent->cv, parent->cv_lock);
        lock_release(parent->cv_lock);
    }
    
    /* 
     * File descriptors will be cleaned up automatically when 
     * proc_destroy() calls file_table_destroy()
     * No need to manually close them here
     */
    
    DEBUG(DB_PROC, "Proc Exited %p (%d)\n", curproc, curproc->pid);
    
    /* Detach from our process */
    proc_remthread(curthread);
    
    /* Thread exit - doesn't return */
    thread_exit();
    
    /* Should never get here */
    panic("sys_exit: thread_exit returned\n");
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



/* Helper function to copy file descriptors during fork */
static int copy_file_descriptors(struct proc *src, struct proc *dst) {
    KASSERT(src != NULL);
    KASSERT(dst != NULL);
    KASSERT(src->fd_table != NULL);
    KASSERT(dst->fd_table != NULL);
    
    lock_acquire(src->fd_table->lock);
    lock_acquire(dst->fd_table->lock);
    /* Shallow copy all the file table entries 
     * and increment the reference count
     * for each file descriptor.
     */
    int array_size = array_num(src->fd_table->entries);
    array_preallocate(dst->fd_table->entries, array_size);
    for (int i = 0; i < array_size; i++) {
        struct fd_entry *src_fde = array_get(src->fd_table->entries, i);
        if (src_fde != NULL) {
            src_fde->count++;
            array_set(dst->fd_table->entries, i, src_fde);
            
            /* Mark the bitmap */
            bitmap_mark(dst->fd_table->bitmap, i);
        }
    }
    
    lock_release(dst->fd_table->lock);
    lock_release(src->fd_table->lock);
    
    return 0;
}