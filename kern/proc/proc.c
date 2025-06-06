/*
 * Copyright (c) 2013
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
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */
#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <kern/unistd.h>
#include <generic/console.h>
/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;
struct lock *console_lock;
static void proctable_add(struct proc *proc);

/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	/* Dummy values - ignore them */
	unsigned int rip;
	size_t fd;
	int ret;
	rip = 1;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		proc = NULL;
		return NULL;
	}

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	proc->max_fd = MAX_FD;

	proc->stdin = STDIN_FILENO;
	proc->stdout = STDOUT_FILENO;
	proc->stderr = STDERR_FILENO; 
	proc->exited = false;
	if (strcmp(name, "[kernel]") && proc != curproc)
		for (fd = 0; fd < MAX_FD; fd++)
		{
			proc->fd_count[fd] = (unsigned int *)kmalloc(sizeof(unsigned int *));
			*proc->fd_count[fd] = 0;
			if (fd < 3)
			{
				const char *console = "con:";
				int flag = fd ? O_WRONLY : O_RDONLY;
				struct vnode *stdio_vnode; 
				ret = vfs_open(kstrdup(console), flag, 0, &stdio_vnode);
				if (ret)
				{
					kprintf("stdio is f..d up, aborting...");
					kfree(proc);
					return NULL;
				}
				proc->fd_table[fd] = stdio_vnode;
				*proc->fd_count[fd] += 1;
				proc -> fd_lock[fd] = console_lock;
				proc->fd_flags[fd] = flag;
			}
			else
			{
				proc->fd_table[fd] = NULL;
				proc->fd_lock[fd] = lock_create("FD Lock");
				proc->fd_flags[fd] = 0;
			}

			/* Not really sure about my way of implementation */
			proc->fd_pos[fd] = (off_t *)kmalloc(sizeof(off_t *));
			*proc->fd_pos[fd] = 0;
			proc -> fd_mode[fd] = -1;	
			proc -> fd_path[fd] = NULL;
		}
	proc->child_status = 0;
	

	/* Setup its Parent 
		If a proc is its own parent just set the parent to NULL
	*/
	if (strcmp(name, "[kernel]") && proc != curproc)
		proc -> parent = curproc;

	/* Setup the condvar (and condvar lock) for this (Needed for Wait) */
	proc -> cv = cv_create(name);
	proc -> cv_lock = lock_create(name);

	

	/* Set the process pid */
	
	if (strcmp(name, "[kernel]") && proc != curproc){
		lock_acquire(pid_lock);
        proctable_add(proc);
        lock_release(pid_lock);
	} else {
		/* Process 0 is NULL - Init is the 1st process
		 * Make sure pid is consistant with 
		*/
		
		bitmap_mark(pid_bitmap, 0);
		array_add(process_table, 0x0, &rip);
		proctable_add(proc);
	}
	return proc;
}

static void proctable_add(struct proc *proc)
{
    unsigned int rip;
    rip = 0;
    bitmap_alloc(pid_bitmap, &proc->pid);	
    if (proc->pid >= array_num(process_table))
    {
        array_add(process_table, proc, &rip);
        KASSERT(proc->pid == rip);
    }
    else
    {
        array_set(process_table, proc->pid, proc);
    }
}

/* TODO : Refactor the whole process adding to table functionality*/
// proc_add_to_table(){

// }

/*
 * Destroy a proc structure.
 *
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);
	// size_t i;
	DEBUG(DB_PROC, "Proc Destroyed %p (%d) By %p (%d) \n", proc, proc -> pid, curproc, curproc->pid);
	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */
	
	

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}

	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

	/* Empty Pid in pid bitmap */
	
	bitmap_unmark(pid_bitmap, proc->pid);
	// array_remove(process_table, (unsigned int)proc->pid);
	array_set(process_table, proc->pid, NULL);
	


	/* Destroy File Locks*/
	// for (i = 0; i < MAX_FD; i++)
	// {
	// 	kfree(proc->fd_pos[i]);
	// 	lock_destroy(proc -> fd_lock[i]);
	// }
	

	/* Free the file-descriptor table 
		** Hoping things dont blow up by this :)
		** Things blew up so I removed this 
	*/	
	

	if(proc->p_name != NULL)
	 	kfree(proc->p_name);

	proc->parent = NULL;
	cv_destroy(proc->cv);
	lock_destroy(proc->cv_lock);

	kfree(proc);
	proc = NULL;
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	pid_lock = lock_create("Pid Lock");
	pid_bitmap = bitmap_create(PID_MAX);
	console_lock = lock_create("Console Lock");
	/* TODO : Clean this before last process shutdown */
	process_table = array_create();
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 * 
 * TODO : Setup parent here
 * 
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);


	spl = splhigh();
	/* Seems like reference counting is not a good option to destroy procs D: */	
	// 	proc_destroy(t -> t_proc);
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwis
    pe the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}
