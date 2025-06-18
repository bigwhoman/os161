#include <syscall.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <mips/trapframe.h>
#include <kern/wait.h>
#include <copyinout.h>


static int copy_file_descriptors(struct proc *src, struct proc *dst); 
void print_memory_contents(vaddr_t start_addr, int count);
/* execv replaces the currently executing program with
 * a newly loaded program image.
 * This occurs within one process; the process id is unchanged.
 */
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
    all = 0;
    struct array *kernel_argv;
    kernel_argv = array_create();
    
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
            for (int j = argc - 1; j >= 0; j--)
            {
                kfree(array_get(kernel_argv, j)); // Free the kernel argument strings
                array_remove(kernel_argv, j);     // Remove the string from the array
            }
            array_destroy(kernel_argv); // Free the array structure
            *retval = -1;
            return err;
        }
        if (argloc == NULL)
        {
            err = ENOENT;
            kfree(argin);
            for (int j = argc - 1; j >= 0; j--)
            {
                kfree(array_get(kernel_argv, j)); // Free the kernel argument strings
                array_remove(kernel_argv, j);     // Remove the string from the array
            }
            array_destroy(kernel_argv); // Free the array structure
            *retval = -1;
            return err;
        }
        err = copyinstr((const_userptr_t)(*argloc), argin, left, &got);
        if (err)
        {
            kfree(argin);
            for (int j = argc - 1; j >= 0; j--)
            {
                kfree(array_get(kernel_argv, j)); // Free the kernel argument strings
                array_remove(kernel_argv, j);     // Remove the string from the array
            }
            array_destroy(kernel_argv); // Free the array structure
            *retval = -1;
            return err;
        }
        if (argin == NULL || got <= 0)
        {
            kfree(argin);
            for (int j = argc - 1; j >= 0; j--)
            {
                kfree(array_get(kernel_argv, j)); // Free the kernel argument strings
                array_remove(kernel_argv, j);     // Remove the string from the array
            }
            array_destroy(kernel_argv); // Free the array structure
            err = ENOENT;
            *retval = -1;
            return err;
        }
        left -= got;
        if (left < 0){
            *retval = -1;
            kfree(argin);
            for (int j = argc - 1; j >= 0; j--)
            {
                kfree(array_get(kernel_argv, j)); // Free the kernel argument strings
                array_remove(kernel_argv, j); // Remove the string from the array
            }
            array_destroy(kernel_argv); // Free the array structure
            err = E2BIG;
            return err;
        }

        
        argc++;   
        array_add(kernel_argv, kstrdup(argin),NULL);
        all += got + 1;
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
    


    // show_valid_tlb_entries();
    // kprintf("before destroy--------------\n");
    as_destroy(old_as);
    // kprintf("after destroy---------------\n");
    // show_valid_tlb_entries();



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
    vaddr_t strloc = (vaddr_t)(stackptr - all);

	/* actuall strings starting location */
	strloc &= 0xfffffffc;

	/* argv pointer location */
    int empty_arg = 0;
	vaddr_t argptr = strloc - (argc + 1) * sizeof(char *);
	err = copyout(&empty_arg, (userptr_t)((vaddr_t *)argptr + argc), sizeof(vaddr_t));
    if (err) {
        kprintf("copyout failed: %s\n", strerror(err));
        kfree(argin);
        for (int j = argc - 1; j >= 0; j--)
        {
            kfree(array_get(kernel_argv, j)); // Free the kernel argument strings
            array_remove(kernel_argv, j);     // Remove the string from the array
        }
        array_destroy(kernel_argv); // Free the array structure
        *retval = -1;
        return err;
    }
    

    for (i = 0; i < (size_t)argc; i++)
	{
        

		/* Move arguments from old stack to new one */
        err = copyout(&strloc, (userptr_t)((vaddr_t *)argptr + i), sizeof(vaddr_t));

        /* For debug purposes ----------------------*/
        // int *dumloc = kmalloc(sizeof(vaddr_t)); // Allocate memory first!
        // copyin((userptr_t)((vaddr_t *)argptr + i), dumloc, sizeof(vaddr_t));
        // kprintf("argv[%zu] pointer = 0x%08x\n", i, *dumloc);
        /*----------------------------------------- */



        if (err)
        {
            kprintf("copyout failed: %s\n", strerror(err));
            kfree(argin);
            for (int j = argc - 1; j >= 0; j--)
            {
                kfree(array_get(kernel_argv, j)); // Free the kernel argument strings
                array_remove(kernel_argv, j);     // Remove the string from the array
            }
            array_destroy(kernel_argv); // Free the array structure
            *retval = -1;
            return err;
        }
        // strcpy((char *)strloc, kernel_argv[i]);
        char *kernel_arg = array_get(kernel_argv, i);
        err = copyoutstr(kernel_arg, (userptr_t)strloc, strlen(kernel_arg) + 1, NULL);
        
        /* -------------------------------- */
        // char *debug_str = kmalloc(strlen(kernel_arg) + 1);
        // copyinstr((userptr_t)strloc, debug_str, strlen(kernel_arg) + 1, NULL);
        // kprintf("argv[%zu] = \"%s\" at 0x%08x\n", i, debug_str, (unsigned int)strloc);
        /* -------------------------------- */



        if (err)
        {
            kprintf("copyout failed: %s\n", strerror(err));
            kfree(argin);
            for (int j = argc - 1; j >= 0; j--)
            {
                kfree(array_get(kernel_argv, j)); // Free the kernel argument strings
                array_remove(kernel_argv, j);     // Remove the string from the array
            }
            array_destroy(kernel_argv); // Free the array structure
            *retval = -1;
            return err;
        }
        strloc += strlen(kernel_arg) + 1;
        kfree(kernel_arg); // Free the kernel argument string
	}
    int arr_num = array_num(kernel_argv);
    for (int j = arr_num - 1; j >= 0; j--)
    {
        array_remove(kernel_argv, j);     // Remove the string from the array
    }
    array_destroy(kernel_argv); // Free the array structure


    // print_memory_contents(0x4000b0, 10); // Debugging: print stack contents

    /* Warp to user mode. */
	enter_new_process(argc /*argc*/, (void *)argptr /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  argptr , entrypoint);


    *retval = -1;
    return err;
} 



void print_memory_contents(vaddr_t start_addr, int count) {
    kprintf("=== Virtual Memory Contents (starting at 0x%08x) ===\n", (unsigned int)start_addr);
    
    for (int i = 0; i < count; i++) {
        vaddr_t current_addr = start_addr + (i * sizeof(int));
        int value;
        int err = copyin((userptr_t)current_addr, &value, sizeof(int));
        
        if (err) {
            kprintf("[%02d] 0x%08x: <read error>\n", i, (unsigned int)current_addr);
        } else {
            // Print as both hex and try as string pointer
            kprintf("[%02d] 0x%08x: 0x%08x", i, (unsigned int)current_addr, value);
            
            // Try to interpret as string pointer
            kprintf("\n");
        }
    }
    kprintf("=== End Memory Content ===\n");
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

            if (!bitmap_isset(dst->fd_table->bitmap, i))
                /* Mark the bitmap */
                bitmap_mark(dst->fd_table->bitmap, i);
        }
        else {
            /* If the source file descriptor is NULL, we can set the destination to NULL */
            array_set(dst->fd_table->entries, i, NULL);
            if (bitmap_isset(dst->fd_table->bitmap, i))
                /* Unmark the bitmap */
                bitmap_unmark(dst->fd_table->bitmap, i);
        }
    }
    
    lock_release(dst->fd_table->lock);
    lock_release(src->fd_table->lock);
    
    return 0;
}
