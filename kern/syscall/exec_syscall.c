#include <syscall.h>
#include <kern/fcntl.h>
#include <proc.h>
#include <lib.h>
#include <vnode.h>
#include <vm.h>
#include <vfs.h>
#include <addrspace.h>


int sys_execv(const char *program, char *argv[], int *retval){

    struct vnode *v;
    vaddr_t stackptr, entrypoint;
    int result;
    int argc;
    size_t i, all;
    // size_t i;
    
    argc = sizeof(argv)/sizeof(char *);
    
	all = 0;
	for (i = 0; i < (size_t)argc; i++)
	{
		all += strlen(argv[i]) + 1;
	}

    /* First Lets open the file */
    result = vfs_open((char *)program, O_RDONLY, 0, &v);
	if (result) {
        *retval = -1;
		return result;
	}

    /* The address space should not be NULL :/ */
    KASSERT(proc_getas() != NULL);

    // as = proc_getas();

    result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
        *retval = -1;
		return result;
	}

    /* Done with the file now. */
	vfs_close(v);

    /* Define the user stack in the address space */
	stackptr = USERSTACK;

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
	// panic("enter_new_process returned\n");

    *retval = -1;
    return result;
} 