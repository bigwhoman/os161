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
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <elf.h>
#include <copyinout.h>

/* execv replaces the currently executing program with
 * a newly loaded program image.
 * This occurs within one process; the process id is unchanged.
 *
 *
 * TODO : Handle Errors */
int sys_execv(const char *program, char *argv[], int *retval){
	struct addrspace *as;
    vaddr_t stackptr, entrypoint;
    int result;
    int argc;
    size_t i, all;
    char prog[64];
	as = NULL;
	argc = 0;
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
	
	char *kernel_args[argc];

	/* Copy the arguments from old stack*/
	for (i = 0; i < (size_t)argc; i++)
	{
		kernel_args[i] = kmalloc(strlen(argv[i]) + 1);
		all += strlen(argv[i]) + 1;
	
		/* Should we do this or copyinstr ????? */
		strcpy(kernel_args[i], argv[i]);
	}
	
	curproc -> p_addrspace = NULL;
	as = NULL;
	
	result = open_copy_prog(prog, &as, &entrypoint);
	if (result)
		return result;

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}	

	/* Put arguments onto the stack */
	vaddr_t strloc = (vaddr_t)(stackptr - all);
	strloc &= 0xfffffffc;
	vaddr_t argptr = strloc - (argc + 1) * sizeof(char *);
	*((vaddr_t *)argptr + argc) = 0;
	for (i = 0; i < (size_t)argc; i++)
	{
		*((vaddr_t *)argptr + i) = strloc;
		strcpy((char *)strloc, kernel_args[i]);
		strloc += strlen(kernel_args[i]) + 1;
	}


	/* Warp to user mode. */
	enter_new_process(argc-1 /*argc*/, (void *)argptr /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  argptr , entrypoint);


    *retval = -1;
    return result;
} 