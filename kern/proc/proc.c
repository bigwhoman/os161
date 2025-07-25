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
static void proctable_add(struct proc *proc);
static struct fd_entry *create_console_fd(int fd_num);
/*
 * Create a proc structure.
 */
static struct proc *proc_create(const char *name)
{
    struct proc *proc;
    unsigned int rip = 1;

    proc = kmalloc(sizeof(*proc));
    if (proc == NULL) {
        return NULL;
    }
    
    proc->p_name = kstrdup(name);
    if (proc->p_name == NULL) {
        kfree(proc);
        return NULL;
    }

    proc->p_numthreads = 0;
    spinlock_init(&proc->p_lock);

    /* VM fields */
    proc->p_addrspace = NULL;

    /* VFS fields */
    proc->p_cwd = NULL;
    
    /* Initialize stdin/stdout/stderr */
    proc->stdin = STDIN_FILENO;
    proc->stdout = STDOUT_FILENO;
    proc->stderr = STDERR_FILENO;
    proc->exited = false;
    proc->child_status = 0;

    /* Create file table */
    proc->fd_table = NULL;
    if (strcmp(name, "[kernel]") != 0) {
        proc->fd_table = file_table_create();
        if (proc->fd_table == NULL) {
            kfree(proc->p_name);
            kfree(proc);
            return NULL;
        }
        
		/* Initialize file descriptor bitmap */
		proc->fd_table->bitmap = bitmap_create(MAX_FD);
		if (proc->fd_table->bitmap == NULL) {
			file_table_destroy(proc->fd_table);
			kfree(proc->p_name);
			kfree(proc);
			return NULL;
		}
        /* Initialize stdin, stdout, stderr */
        for (int i = 0; i < 3; i++) {
            struct fd_entry *fde = create_console_fd(i);
            if (fde == NULL) {
                /* Cleanup on failure */
                for (int j = 0; j < i; j++) {
                    struct fd_entry *prev = array_get(proc->fd_table->entries, j);
                    if (prev) {
                        vfs_close(prev->vnode);
                        kfree(prev->path);
                        kfree(prev);
                    }
                }
                file_table_destroy(proc->fd_table);
                kfree(proc->p_name);
                kfree(proc);
                return NULL;
            }
            
            /* Add to file table */
            lock_acquire(proc->fd_table->lock);
            array_add(proc->fd_table->entries, fde, NULL);
            bitmap_mark(proc->fd_table->bitmap, i);
            lock_release(proc->fd_table->lock);
        }
    }

    /* Setup parent */
    if (strcmp(name, "[kernel]") != 0 && proc != curproc) {
        proc->parent = curproc;
    } else {
        proc->parent = NULL;
    }

    /* Setup synchronization */
    proc->cv = cv_create(name);
    if (proc->cv == NULL) {
        if (proc->fd_table != NULL) {
            file_table_destroy(proc->fd_table);
        }
        kfree(proc->p_name);
        kfree(proc);
        return NULL;
    }
    
    proc->cv_lock = lock_create(name);
    if (proc->cv_lock == NULL) {
        cv_destroy(proc->cv);
        if (proc->fd_table != NULL) {
            file_table_destroy(proc->fd_table);
        }
        kfree(proc->p_name);
        kfree(proc);
        return NULL;
    }

    /* Set process pid */
    if (strcmp(name, "[kernel]") != 0) {
        lock_acquire(pid_lock);
        proctable_add(proc);
        lock_release(pid_lock);
    } else {
        /* Kernel process setup */
        bitmap_mark(pid_bitmap, 0);
        array_add(process_table, NULL, &rip);
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

	if (proc->fd_table != NULL) {
        file_table_destroy(proc->fd_table);
        proc->fd_table = NULL;
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
	// 	lock_destroy(proc -> fd_lock[i]);
	// }

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
 * or might not be current.>ch, and any other implicit uses
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
 *>
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


/* Create a new file table */
struct file_table *file_table_create(void) {
    struct file_table *ft = kmalloc(sizeof(struct file_table));
    if (ft == NULL) {
        return NULL;
    }
    
    ft->entries = array_create();
    if (ft->entries == NULL) {
        kfree(ft);
        return NULL;
    }
    
    ft->bitmap = bitmap_create(MAX_FD);
    if (ft->bitmap == NULL) {
        array_destroy(ft->entries);
        kfree(ft);
        return NULL;
    }
    
    ft->lock = lock_create("file_table");
    if (ft->lock == NULL) {
        bitmap_destroy(ft->bitmap);
        array_destroy(ft->entries);
        kfree(ft);
        return NULL;
    }
    
    return ft;
}

/* Destroy a file table */
void file_table_destroy(struct file_table *ft) {
    if (ft == NULL) return;
    
    /* Clean up all file descriptors */
    lock_acquire(ft->lock);
	int array_size = array_num(ft->entries);
    for (int i = array_size - 1; i >= 0; i--) {
        struct fd_entry *fde = array_get(ft->entries, i);
        if (fde != NULL) {
            if (fde->vnode != NULL) {
                vfs_close(fde->vnode);
				/* Maybe we will change it later */
				fde->count--;
            }
			if (fde->count == 0) {
				if (fde->path != NULL)
				{
					kfree(fde->path);
				}
				if (fde->lock != NULL && fde->lock != console_lock)
				{
					lock_destroy(fde->lock);
				}
				kfree(fde);
			}
		}
		array_remove(ft->entries, i);
	}
	array_destroy(ft->entries);
    lock_release(ft->lock);
    bitmap_destroy(ft->bitmap);
    lock_destroy(ft->lock);
    kfree(ft);
}

/* Create fd_entry for console */
static struct fd_entry *create_console_fd(int fd_num) {
    struct fd_entry *fde = kmalloc(sizeof(struct fd_entry));
    if (fde == NULL) return NULL;
    
    const char *console = "con:";
    int flag = fd_num ? O_WRONLY : O_RDONLY;
    
    int ret = vfs_open(kstrdup(console), flag, 0, &fde->vnode);
    if (ret) {
        kfree(fde);
        return NULL;
    }
    
    fde->pos = 0;
    fde->lock = console_lock;  /* Shared console lock */
    fde->count = 1;
    fde->mode = 0;
    fde->flags = flag;
    fde->path = kstrdup(console);
    
    return fde;
}