#include <syscall.h>
#include <kern/fcntl.h>
#include <proc.h>
#include <lib.h>
#include <vnode.h>
#include <vm.h>
#include <vfs.h>
#include <addrspace.h>
#include <types.h>
#include <kern/errno.h>
#include <uio.h>
#include <current.h>
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
    int result;
    int argc;
    size_t i, all;
    char prog[64];
	as = NULL;
	argc = 0;

	old_as = curproc ->p_addrspace;

	for (size_t i = 0; ; i++)
	{
		if(*(argv + i) == NULL)
			break;
		argc++;
	}

	/* TODO : Check for valid program name !!! */
	copyinstr((const_userptr_t)program, prog, 63,(size_t *)retval);

	/* We might need to change this in the future!!! */
	/* TODO : Draw the structure of the program */
	
    
	all = 0;
	
	/* Copy the arguments from old stack */
	for (i = 0; i < (size_t)argc; i++)
	{
		all += strlen(argv[i]) + 1;
	}
	
	
	result = open_copy_prog(prog, &as, &entrypoint);
	if (result){
		proc_setas(old_as);
		as_activate();
		*retval = -1;
		return result;
	}

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		proc_setas(old_as);
		as_activate();
		*retval = -1;
		return result;
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
    return result;
} 