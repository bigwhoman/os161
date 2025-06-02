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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <bitmap.h>
#include <vm.h>
#include <proc.h>
#include <spl.h>
#include <lib.h>



/* Addrspace Spinlock */
static struct spinlock addrspace_lock = SPINLOCK_INITIALIZER;

uint8_t get_asid(void);
/*
 * Get a new address space identifier (ASID).
 * This function should allocate a unique ASID for the new address space.
 */
uint8_t get_asid(){
	unsigned int asid;
	int result;
	int spl;
	size_t i;
	result = 0;
	spinlock_acquire(&addrspace_lock);
	bitmap_mark(asid_bitmap, 0); /* Always mark the first ASID as used */

	/* Find a free ASID */
	result = bitmap_alloc(asid_bitmap, &asid);
	if (result) {
		/* No free ASID available */
		spl = splhigh(); /* Disable interrupts */
		/* Invalidate all TLB entries */
		for (i = 0; i < NUM_TLB; i++)
		{
			tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
		}
		for ( i = 0; i < MAX_ASID; i++)
		{
			bitmap_unmark(asid_bitmap, i); /* Free all ASIDs */
		}
		bitmap_alloc(asid_bitmap, &asid); /* Allocate a new ASID */
		splx(spl);
	}
	spinlock_release(&addrspace_lock);
	return asid; 
}


/*
 * Create a new address space.
 *
 * Returns a pointer to the new address space, or NULL on failure.
 */
struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	as->stack_top = USERSTACK; /* initial stack pointer */
	as->stack_bottom = USERSTACK; /* bottom of stack */
	
	as->ref_count = 1; /* initial reference count */

	as->addrlock = lock_create("addrspace_lock");
	if (as->addrlock == NULL) {
		kfree(as);
		return NULL; /* Failed to create lock */
	}

	/*
	 * Initialize heap as needed.
	 */
	as->heap_base = 0; /* base address of heap */
	as->heap_end = 0; /* current end of the heap */


	/*
	 * Initialize the page table base address to 0.
	 * This will be set up properly when the process is loaded.
	 */
	as->pt_base = (int)NULL; /* base address of page table */

	// Initialize the linked list of memory regions.
	as->regions = NULL; /* linked list of memory regions */

	as->asid = 0; /* get a new address space identifier */	

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;
	KASSERT(old != NULL);
	KASSERT(ret != NULL);
	


	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Copy the old address space's properties to the new one.
	 */
	newas->stack_top = old->stack_top; /* Copy stack top */
	newas->stack_bottom = old->stack_bottom; /* Copy stack bottom */
	newas->heap_base = old->heap_base; /* Copy heap base */
	newas->heap_end = old->heap_end; /* Copy heap end */
	newas->pt_base = old->pt_base; /* Copy page table base address */
	newas->asid = old->asid; /* Copy ASID */
	newas->addrlock = lock_create("new_addrspace_lock");
	if (newas->addrlock == NULL) {
		kfree(newas);
		return ENOMEM; /* Failed to create lock for new address space */
	}

	lock_acquire(old->addrlock);
	/* This would probably need to be changed for COW */
	old->ref_count += 1; /* Increment reference count for the old address space */
	newas->ref_count = old->ref_count; /* Set initial reference count for the new address space */
	lock_release(old->addrlock);

	/*
	 * Shallow copy the linked list of memory regions.
	 */
	newas->regions = old->regions;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */

	KASSERT(as != NULL);
	struct vm_region *region, *next_region;
	lock_acquire(as->addrlock); /* Acquire the lock to ensure thread safety */
	as->ref_count -= 1; /* Decrement the reference count */
	if (as->ref_count > 0) {
		lock_release(as->addrlock); /* Release the lock if there are still references */
		return; /* Don't destroy the address space if there are still references */
	}
	lock_release(as->addrlock); /* Release the lock before freeing */
	while(as->regions != NULL) {
		region = as->regions;
		next_region = region->next;
		kfree(region); /* Free the region */
		as->regions = next_region; /* Move to the next region */
	}

	kfree(as);
}

/*
 * Activate the current address space. 
 * Assign a new ASID for TLB if not already set. 
 */
void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}
	if (as->asid == 0) {
		/*
		 * If the ASID is 0, it means we haven't assigned an ASID yet.
		 * Get a new ASID for this address space.
		 */
		as->asid = get_asid();
	}
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */

	KASSERT(as != NULL);
	KASSERT(memsize > 0);
	KASSERT(vaddr % PAGE_SIZE == 0);
	KASSERT((vaddr + memsize) % PAGE_SIZE == 0);
	KASSERT((vaddr + memsize) > vaddr); /* Check for overflow */
	KASSERT((vaddr + memsize) <= as->stack_top); /* Ensure it doesn't exceed user stack */

	struct vm_region *region;
	region = as->regions;
	while (region != NULL) {
		/* Check for overlapping regions */
		if ((vaddr < region->start + region->size) && (vaddr + memsize > region->start)) {
			return EEXIST; /* Overlapping region */
		}
		region = region->next;
	}

	region = kmalloc(sizeof(struct vm_region));
	if (region == NULL) {
		return ENOMEM; /* Out of memory */
	}
	region->start = vaddr;
	region->size = memsize;
	region->readable = readable;
	region->writeable = writeable;
	region->executable = executable;
	region->temp_write = 0; /* Temporary write permission not set */
	region->next = NULL; /* Insert at the beginning of the list */
	return 0;
}

/*
 * Prepare the address space for loading an executable.
 * This function is called before actually loading from an executable into the address space.
 * Set the temporary write permission for all regions to 1.
 */
int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as != NULL);
	struct vm_region *region;
	region = as->regions;
	while(region != NULL) 
	{
		region->temp_write = 1; /* Set temporary write permission for all regions */
		region = region->next; /* Move to the next region */
	}
	return 0;
}

/*
 * Complete the loading of an executable into the address space.
 * This function is called when loading from an executable is complete.
 * Set the temporary write permission for all regions to 0.
 */
int
as_complete_load(struct addrspace *as)
{
	KASSERT(as != NULL);
	struct vm_region *region;
	region = as->regions;
	while (region != NULL)
	{
		region->temp_write = 0; /* Set temporary write permission for all regions */
		region = region->next;	/* Move to the next region */
	}
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{

	KASSERT(as != NULL);
	KASSERT(stackptr != NULL);
	/* Initialize the stack pointer to the top of the user stack */
	if (as->stack_top == 0) {
		/* If the stack top is not set, set it to USERSTACK */
		as->stack_top = USERSTACK;
	}
	*stackptr = as->stack_top; /* Set the initial stack pointer */

	return 0;
}

