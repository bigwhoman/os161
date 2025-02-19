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
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than runprogram() does.
 */

#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>

// int open_copy_prog(char *progname, struct addrspace **as, vaddr_t *entrypoint);

int open_copy_prog(char *progname, struct addrspace **as, vaddr_t *entrypoint){
	// kprintf("open and copy program : %s\n",progname);
	struct vnode *v;
	int result;
	// size_t i;
	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* We should be a new process. */
	KASSERT(proc_getas() == NULL);

	/* Create a new address space. */
	*as = as_create();
	if (*as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(*as);
	as_activate();


	// int X[20];


	// kprintf("------------------\n");
	/* Load the executable. */
	result = load_elf(v, entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}
	
	// kprintf("entry point is : %x ----- \n", *entrypoint);
	// memcpy(X, (void *)(*entrypoint), sizeof(X));
    
	// for ( i = 0; i < 4; i++)
	// {
	// 	kprintf("X : %x\n", X[i]);
	// }	
	

	/* Done with the file now. */
	vfs_close(v);
	return 0;
}

/*
 * Load program "progname" and start running it in usermode.
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 * 
 * 
 * 
 */
int
runprogram(char *progname, int argc, char *argv[])
{
	struct addrspace *as;
	vaddr_t entrypoint, stackptr;
	int result;
	size_t i, all;
	all = 0;
	as = NULL;
	for (i = 0; i < (size_t)argc; i++)
	{
		all += strlen(argv[i]) + 1;
	}
	
	
	result = open_copy_prog(progname, &as, &entrypoint);
	if (result)
		return result;
	

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}	

	/* Put arguments on the stack */

	vaddr_t strloc = (vaddr_t)(stackptr - all);
	strloc &= 0xfffffffc;
	vaddr_t argptr = strloc - argc * sizeof(char *); 

	for (i = 0; i < (size_t)argc; i++)
	{
		*((vaddr_t *)argptr + i) = strloc;
		strcpy((char *)strloc, argv[i]);
		strloc += strlen(argv[i]) + 1;
	}




	/* Warp to user mode. */
	enter_new_process(argc-1 /*argc*/, (void *)argptr /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  argptr , entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

