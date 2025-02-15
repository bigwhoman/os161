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

int
l_elf(struct vnode *v, vaddr_t *entrypoint);
static

int
l_segment(struct addrspace *as, struct vnode *v,
	     off_t offset, vaddr_t vaddr,
	     size_t memsize, size_t filesize,
	     int is_executable);

static
int
l_segment(struct addrspace *as, struct vnode *v,
	     off_t offset, vaddr_t vaddr,
	     size_t memsize, size_t filesize,
	     int is_executable)
{
	struct iovec iov;
	struct uio u;
	int result;

	if (filesize > memsize) {
		kprintf("ELF: warning: segment filesize > segment memsize\n");
		filesize = memsize;
	}

	DEBUG(DB_EXEC, "ELF: Loading %lu bytes to 0x%lx\n",
	      (unsigned long) filesize, (unsigned long) vaddr);

	/*
	 * 
	 * Basically set the uio to user space 
	 * should set the iov (uio data blocks) to a ubase (used in kernel but with the caller being a user)
	 * seg flag is in user space (data/instruction)
	 */
	iov.iov_ubase = (userptr_t)vaddr;
	iov.iov_len = memsize;		 // length of the memory space
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = filesize;          // amount to read from the file
	u.uio_offset = offset;
	u.uio_segflg = is_executable ? UIO_USERISPACE : UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = as;

	result = VOP_READ(v, &u);
	if (result) {
		return result;
	}

	if (u.uio_resid != 0) {
		/* short read; problem with executable? */
		kprintf("ELF: short read on segment - file truncated?\n");
		return ENOEXEC;
	}

	/*
	 * If memsize > filesize, the remaining space should be
	 * zero-filled. There is no need to do this explicitly,
	 * because the VM system should provide pages that do not
	 * contain other processes' data, i.e., are already zeroed.
	 *
	 * During development of your VM system, it may have bugs that
	 * cause it to (maybe only sometimes) not provide zero-filled
	 * pages, which can cause user programs to fail in strange
	 * ways. Explicitly zeroing program BSS may help identify such
	 * bugs, so the following disabled code is provided as a
	 * diagnostic tool. Note that it must be disabled again before
	 * you submit your code for grading.
	 */
#if 0
	{
		size_t fillamt;

		fillamt = memsize - filesize;
		if (fillamt > 0) {
			DEBUG(DB_EXEC, "ELF: Zero-filling %lu more bytes\n",
			      (unsigned long) fillamt);
			u.uio_resid += fillamt;
			result = uiomovezeros(fillamt, &u);
		}
	}
#endif

	return result;
}

int
l_elf(struct vnode *v, vaddr_t *entrypoint)
{
	Elf_Ehdr eh;   /* Executable header */
	Elf_Phdr ph;   /* "Program header" = segment header */
	int result, i;
	struct iovec iov;
	struct uio ku;
	struct addrspace *as;

	as = proc_getas();

	/*
	 * Read the executable header from offset 0 in the file.
	 */

	uio_kinit(&iov, &ku, &eh, sizeof(eh), 0, UIO_READ);
	result = VOP_READ(v, &ku);
	if (result) {
		return result;
	}

	if (ku.uio_resid != 0) {
		/* short read; problem with executable? */
		kprintf("ELF: short read on header - file truncated?\n");
		return ENOEXEC;
	}

	/*
	 * Check to make sure it's a 32-bit ELF-version-1 executable
	 * for our processor type. If it's not, we can't run it.
	 *
	 * Ignore EI_OSABI and EI_ABIVERSION - properly, we should
	 * define our own, but that would require tinkering with the
	 * linker to have it emit our magic numbers instead of the
	 * default ones. (If the linker even supports these fields,
	 * which were not in the original elf spec.)
	 */

	if (eh.e_ident[EI_MAG0] != ELFMAG0 ||
	    eh.e_ident[EI_MAG1] != ELFMAG1 ||
	    eh.e_ident[EI_MAG2] != ELFMAG2 ||
	    eh.e_ident[EI_MAG3] != ELFMAG3 ||
	    eh.e_ident[EI_CLASS] != ELFCLASS32 ||
	    eh.e_ident[EI_DATA] != ELFDATA2MSB ||
	    eh.e_ident[EI_VERSION] != EV_CURRENT ||
	    eh.e_version != EV_CURRENT ||
	    eh.e_type!=ET_EXEC ||
	    eh.e_machine!=EM_MACHINE) {
		return ENOEXEC;
	}

	/*
	 * Go through the list of segments and set up the address space.
	 *
	 * Ordinarily there will be one code segment, one read-only
	 * data segment, and one data/bss segment, but there might
	 * conceivably be more. You don't need to support such files
	 * if it's unduly awkward to do so.
	 *
	 * Note that the expression eh.e_phoff + i*eh.e_phentsize is
	 * mandated by the ELF standard - we use sizeof(ph) to load,
	 * because that's the structure we know, but the file on disk
	 * might have a larger structure, so we must use e_phentsize
	 * to find where the phdr starts.
	 */

	for (i=0; i<eh.e_phnum; i++) {
		off_t offset = eh.e_phoff + i*eh.e_phentsize;
		uio_kinit(&iov, &ku, &ph, sizeof(ph), offset, UIO_READ);

		result = VOP_READ(v, &ku);
		if (result) {
			return result;
		}

		if (ku.uio_resid != 0) {
			/* short read; problem with executable? */
			kprintf("ELF: short read on phdr - file truncated?\n");
			return ENOEXEC;
		}

		switch (ph.p_type) {
		    case PT_NULL: /* skip */ continue;
		    case PT_PHDR: /* skip */ continue;
		    case PT_MIPS_REGINFO: /* skip */ continue;
		    case PT_LOAD: break;
		    default:
			kprintf("loadelf: unknown segment type %d\n",
				ph.p_type);
			return ENOEXEC;
		}

		// result = as_define_region(as,
		// 			  ph.p_vaddr, ph.p_memsz,
		// 			  ph.p_flags & PF_R,
		// 			  ph.p_flags & PF_W,
		// 			  ph.p_flags & PF_X);
		// if (result) {
		// 	return result;
		// }
	}

	/*
	 * Now actually load each segment.
	 * 
	 * Basically first we setup a kuio (uio for kernel buffers)
	 * Program Header Table = e_phoff
	 * For each segment(which is determined by a program header) :
	 * 		Program Header Entry[i] = Prograom Header Table + i * ph_table_entry_size
	 * 
	 */

	for (i=0; i<eh.e_phnum; i++) {
		off_t offset = eh.e_phoff + i*eh.e_phentsize;
		uio_kinit(&iov, &ku, &ph, sizeof(ph), offset, UIO_READ);

		result = VOP_READ(v, &ku);
		if (result) {
			return result;
		}

		if (ku.uio_resid != 0) {
			/* short read; problem with executable? */
			kprintf("ELF: short read on phdr - file truncated?\n");
			return ENOEXEC;
		}

		switch (ph.p_type) {
		    case PT_NULL: /* Unused Entry         - skip */ continue;
		    case PT_PHDR: /* Program Header Table - skip */ continue;
		    case PT_MIPS_REGINFO: /* skip */ continue;
		    case PT_LOAD: /* Loadable Segment (thing we want)*/ break;
		    default:
			kprintf("loadelf: unknown segment type %d\n",
				ph.p_type);
			return ENOEXEC;
		}

		result = l_segment(as, v, ph.p_offset, ph.p_vaddr,
				      ph.p_memsz, ph.p_filesz,
				      ph.p_flags & PF_X);
		if (result) {
			return result;
		}
	}

	result = as_complete_load(as);
	if (result) {
		return result;
	}

	*entrypoint = eh.e_entry;

	return 0;
}


int sys_execv(const char *program, char *argv[], int *retval){

    struct vnode *v;
    vaddr_t stackptr, entrypoint;
    int result;
    int argc;
    size_t i, all;
	(void) program;
    // size_t i;
    
    argc = sizeof(argv)/sizeof(char *);
    
	all = 0;
	for (i = 0; i < (size_t)argc; i++)
	{
		all += strlen(argv[i]) + 1;
	}

    /* First Lets open the file */
    result = vfs_open(argv[0], O_RDONLY, 0, &v);
	if (result) {
        *retval = -1;
		return result;
	}

    /* The address space should not be NULL :/ */
    KASSERT(proc_getas() != NULL);

    // as = proc_getas();

    result = l_elf(v, &entrypoint);
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