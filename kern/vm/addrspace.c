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
#include <cpu.h>
#include <current.h>
#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <array.h>
#include <cpu.h>
#include <spl.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <threadlist.h>
#include <threadprivate.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <addrspace.h>
#include <mainbus.h>
#include <vnode.h>


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
	size_t i;
	result = 0;
	spinlock_acquire(&addrspace_lock);
	if(!bitmap_isset(asid_bitmap, 0)) {
		bitmap_mark(asid_bitmap, 0); /* Always mark the first ASID as used */
	}
	/* Find a free ASID */
	result = bitmap_alloc(asid_bitmap, &asid);
	if (result) {
		/* No free ASID available */


		/* Invalidate all TLB entries for all cores */
		all_tlb_shootdown(); 
		for ( i = 1; i < MAX_ASID; i++)
		{
			bitmap_unmark(asid_bitmap, i); /* Free all ASIDs */
		}
		bitmap_alloc(asid_bitmap, &asid); /* Allocate a new ASID */

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
	as->pt = kmalloc(sizeof(struct page_table));
	if (as->pt == NULL) {
		lock_destroy(as->addrlock);
		kfree(as);
		return NULL; /* Failed to allocate page table */
	}	

    memset(as->pt, 0, sizeof(struct page_table)); // Initialize the new page table
	// Initialize the linked list of special memory regions.
	as->regions = NULL; /* linked list of memory regions */

	as->stack_region = kmalloc(sizeof(struct vm_region));
	if (as->stack_region == NULL) {
		kfree(as->pt);
		lock_destroy(as->addrlock);
		kfree(as);
		return NULL; /* Failed to allocate stack region */
	}
	// Initialize the stack region
	as->stack_region->start = as->stack_top; /* Start at the top of the stack */
	as->stack_region->size = 0; /* Size of the stack region */
	as->stack_region->readable = 1; /* Stack is readable */
	as->stack_region->writeable = 1; /* Stack is writeable */
	as->stack_region->executable = 0; /* Stack is not executable */
	as->stack_region->temp_write = 0; /* Temporary write permission not set */
	as->stack_region->next = NULL; /* No next region */
	// Initialize the heap region
	as->heap_region = kmalloc(sizeof(struct vm_region));
	if (as->heap_region == NULL) {
		kfree(as->stack_region);
		kfree(as->pt);
		lock_destroy(as->addrlock);
		kfree(as);
		return NULL; /* Failed to allocate heap region */
	}
	as->heap_region->start = as->heap_base; /* Start at the base of the heap */
	as->heap_region->size = 0; /* Size of the heap region */
	as->heap_region->readable = 1; /* Heap is readable */
	as->heap_region->writeable = 1; /* Heap is writeable */
	as->heap_region->executable = 0; /* Heap is not executable */
	as->heap_region->temp_write = 0; /* Temporary write permission not set */
	as->heap_region->next = NULL; /* No next region */
	// Initialize the regions linked list


	as->asid = 0; /* get a new address space identifier */	

	return as;
}


/* Copy the address space of old process into a new address space, 
 * returning the new address space in ret.
 * This function is used for process creation, such as fork.
 * The copy is a deep copy of the address space,
 */
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
	newas->asid = 0; /* TODO: Not sure -- Copy ASID */
	newas->addrlock = lock_create("new_addrspace_lock");
	if (newas->addrlock == NULL) {
		kfree(newas);
		return ENOMEM; /* Failed to create lock for new address space */
	}


	/*
	 * Deep copy the linked list of memory regions.
	 */
	struct vm_region *old_region = old->regions;
	struct vm_region *prev_region = NULL;
	while (old_region != NULL) {
		struct vm_region *new_region = kmalloc(sizeof(struct vm_region));
		if (new_region == NULL) {
			as_destroy(newas); /* Clean up if allocation fails */
			return ENOMEM; /* Out of memory */
		}
		/* Copy the properties of the old region to the new region */
		new_region->start = old_region->start;
		new_region->size = old_region->size;
		new_region->readable = old_region->readable;
		new_region->writeable = old_region->writeable;
		new_region->executable = old_region->executable;
		new_region->temp_write = old_region->temp_write;
		new_region->next = NULL; /* Insert at the beginning of the list */
		if (newas->regions == NULL)
			newas->regions = new_region; /* Set the new region as the first region */	
		else
			prev_region->next = new_region; /* Link the new region to the previous one */


		prev_region = new_region; /* Update the previous region to the new one */

		
		old_region = old_region->next; /* Move to the next region in the old address space */
	}	

	memcpy(newas->stack_region, old->stack_region, sizeof(struct vm_region)); /* Copy stack region */
	memcpy(newas->heap_region, old->heap_region, sizeof(struct vm_region)); /* Copy heap region */


	/* Copy Page Tables */
	kfree(newas->pt); /* Free the old page table structure */
	newas->pt = (struct page_table *)copy_page_table(old->pt, 1); /* Copy the page table */



	shootdown_all_asid(old->asid);


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
	while(as->regions != NULL) {
		region = as->regions;
		next_region = region->next;
		kfree(region); /* Free the region */
		as->regions = next_region; /* Move to the next region */
	}
	/* Free the stack region */
	if (as->stack_region != NULL) {
		kfree(as->stack_region);
	}
	/* Free the heap region */
	if (as->heap_region != NULL) {
		kfree(as->heap_region);
	}
	
	/* Free the address space structure */
	if (as->asid != 0) { 
        spinlock_acquire(&addrspace_lock);
        // Send shootdown to ALL other CPUs
		shootdown_all_asid(as->asid);
		bitmap_unmark(asid_bitmap, as->asid);
        spinlock_release(&addrspace_lock);
    }
	/* Free the page table */
	free_page_table(as->pt, 1); /* Free the page table */
	kfree(as->pt);				/* Free the page table structure */
	lock_destroy(as->addrlock); /* Destroy the lock */

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

	
	// Shootdown on every context switch
	// TODO: Will change this later
	tlb_shootdown_all();
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


	KASSERT(as != NULL);
	KASSERT(memsize > 0);
	KASSERT((vaddr + memsize) > vaddr); /* Check for overflow */
	KASSERT((vaddr + memsize) <= as->stack_top); /* Ensure it doesn't exceed user stack */

	struct vm_region *region,*last_region;
	region = as->regions;
	last_region = region;
	while (region != NULL) {
		/* Check for overlapping regions */
		if ((vaddr < region->start + region->size) && (vaddr + memsize > region->start)) {
			return EEXIST; /* Overlapping region */
		}
		last_region = region; /* Keep track of the last region */
		/* Move to the next region */
		region = region->next;
	}

	region = kmalloc(sizeof(struct vm_region));
	if (region == NULL) {
		return ENOMEM; /* Out of memory */
	}
	region->start = vaddr;
	region->size = memsize;
	region->readable = (readable | executable) ? 1 : 0;
	region->writeable = writeable ? 1 : 0;
	region->executable = executable ? 1 : 0;
	region->temp_write = 0; /* Temporary write permission not set */
	region->next = NULL; /* Insert at the beginning of the list */

	if (last_region != NULL){
		last_region->next = region; /* Link the new region to the last one */
	} else {
		as->regions = region; /* If no regions, set as the first region */
	}

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

