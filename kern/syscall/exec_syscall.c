#include <syscall.h>
#include <kern/fcntl.h>
#include <proc.h>
#include <lib.h>
#include <vnode.h>
#include <vm.h>
#include <vfs.h>
#include <addrspace.h>


int sys_execv(const char *program, char **args, int *retval){
    struct addrspace *as;
    struct vnode *v;
    vaddr_t stackptr, entrypoint;
    int result;
    int argc;
    // size_t i;
    
    argc = sizeof(args)/sizeof(char *);
    

    /* First Lets open the file */
    result = vfs_open((char *)program, O_RDONLY, 0, &v);
	if (result) {
        *retval = -1;
		return result;
	}

    /* The address space should not be NULL :/ */
    KASSERT(proc_getas() != NULL);

    as = proc_getas();

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
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
        *retval = -1;
		return result;
	}

    /* Warp to user mode. */
	enter_new_process(argc /*argc*/, NULL /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	// panic("enter_new_process returned\n");

    *retval = -1;
    return result;
} 