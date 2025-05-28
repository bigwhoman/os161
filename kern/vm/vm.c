#include <types.h>
#include <proc.h>
#include <spinlock.h>
#include <synch.h>
#include <current.h>
#include <lib.h>
#include <vm.h>

void init_coremap(paddr_t , size_t );

/* Coremap Spinlock */
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;


void init_coremap(paddr_t start_paddr, size_t num_pages){
    size_t start_page;
    size_t i;
    start_page = start_paddr / PAGE_SIZE;

    for (i = 0; i < num_pages; i++)
    {
        coremap[i].allocated = 0;
        coremap[i].kernel = 0;
        coremap[i].reference_count = 0;
        coremap[i].owner = 0;
        coremap[i].last_access = 0;
        coremap[i].end = 0;
        coremap[i].start = 0;
        coremap[i.next_allocated = 0;
        page = next_page;d]
        if (i < num_pages - 1)
            coremap[i].next_free = i + 1;
        else 
            coremap[i].next_free = 0;
    }

    
    first_free_page = start_page;
    first_page_paddr = start_paddr;
    total_free_pages = num_pages;
    total_pages = num_pages;
}

void vm_bootstrap(){
    unsigned int ramsize;
    unsigned int total_pages;
    unsigned int coremap_size;
    unsigned int coremap_pages;
    paddr_t coremap_paddr;
    ramsize = ram_getsize();

    KASSERT(ramsize > 0);

    total_pages = ramsize / PAGE_SIZE;
    coremap_size = total_pages * sizeof(struct coremap_entry);
    coremap_pages = (coremap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    // coremap_lock = (struct spinlock *)PADDR_TO_KVADDR(
    //                             ram_stealmem(sizeof(struct spinlock)/PAGE_SIZE));

    coremap_paddr = ram_stealmem(coremap_pages);

    if (coremap_paddr <= 0 ){
        panic("Cannot allocate memory for coremap");
    }

    coremap = (struct coremap_entry *)PADDR_TO_KVADDR(coremap_paddr);

    paddr_t first_free_paddr = ram_getfirstfree();

    size_t manageable_pages = (ramsize - first_free_paddr) / PAGE_SIZE;

    init_coremap(first_free_paddr, manageable_pages);

    return;
}

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress){
    (void) faulttype;
    (void) faultaddress;
    return 0;
}

/*
 * Allocate/free kernel heap pages (called by kmalloc/kfree) 
 * We allocate contigous pages for kernel. 
 */
vaddr_t alloc_kpages(unsigned npages){
    spinlock_acquire(&coremap_lock);
    size_t aquired_pages,page,i;
    size_t starting_page,ending_page,prev_free_page;
    aquired_pages = 0;
    size_t max_found;
    max_found = 0; /* variable used for debugging purposes */
    prev_free_page = 0;

    /*
     * Search the whole coremap for free pages
     * Our priority is to find consequtive pages 
     * If no consequtive pages are found just take 
     * the first n free pages
     */
    for (page = 0; page < total_pages; page++)
    {
        if (page < total_pages - 1){
            if (!coremap[page + 1].allocated)
                coremap[page].next_free = page + 1;
        } else {
            
        }


        if (!coremap[page].allocated){
            prev_free_page = page;
            aquired_pages += 1;
            if (max_found < 1)
                first_free_page = page;
            if (max_found < npages)
                max_found += 1;
        } else {
            aquired_pages = 0;
        }
        if (aquired_pages == npages){
            ending_page = page;
            starting_page = page - (npages - 1);
            break;
        }
        if (max_found == npages){
            ending_page = page;
            starting_page = first_free_page;
        }
    }

    /* Make sure we have enough pages left to allocate !!*/
    if (max_found < npages)
    {
        /* TODO: fix this when fixing swap*/
        panic("not enough memory !!! -- max found : %d !!!!", max_found);
        spinlock_release(&coremap_lock);
        return 0;
    }

    /* Set start and end of our allocation */
    coremap[starting_page].start = 1;
    coremap[ending_page].end = 1;

    /* start the allocation */
    page = starting_page;
    for (i = 0; i < npages; i++)
    {
        coremap[page].allocated = 1;
        coremap[page].kernel = 1;
        if (curthread != NULL && curproc != NULL)
            coremap[page].owner = (unsigned int)(curproc->pid);
        coremap[page].reference_count += 1;
        coremap[page].allocation_size = npages;

        /* TODO: Need to fix this timestamp*/
        coremap[page].last_access = 1000;

        /* 
         * Make sure we are not in the end of allocation 
         * set the next allocated as the next free 
         */
        if (!coremap[page].end){
            coremap[page].next_allocated = coremap[page].next_free;
            coremap[page].next_free = coremap[ending_page].next_free;
            page = coremap[page].next_allocated;
        }

    } 

    paddr_t page_paddr;
    page_paddr = first_page_paddr + starting_page * PAGE_SIZE;
    
    spinlock_release(&coremap_lock);
    return PADDR_TO_KVADDR(page_paddr);
}


void free_kpages(vaddr_t addr){
    spinlock_acquire(&coremap_lock);
    size_t page_paddr,page,i,next_page;
    /* Hope this doesnt wrongly align */
    if ((addr & PAGE_FRAME) != addr) {
        panic("free_kpages: address 0x%x is not page-aligned", addr);
    }
    page_paddr = KVADDR_TO_PADDR(addr);
    page = (page_paddr - first_page_paddr)/PAGE_SIZE;
    if (!coremap[page].start){
        panic("Tried freeing non-start page");
    }
    /* TODO: Check the owner and pid */
    next_page = page;
    for (i = 0; i < coremap[page].allocation_size; i++)
    {
        if (!coremap[page].allocated)
        {
            panic("Tried freeing non-start page");
        }

        /* TODO: Make this reference counting based !!! */
        coremap[page].allocated = 0;
        coremap[page].kernel = 0;
        if (curthread != NULL && curproc != NULL)
            coremap[page].owner = 0;
        coremap[page].reference_count -= 1;
        coremap[page].allocation_size = 0;

        /* TODO: Need to fix for last page */
        coremap[page].next_free = page + i + 1;
    
        /* TODO: Need to fix this timestamp*/
        coremap[page].last_access = 1000;


        next_page = coremap[page].next_allocated;
        coremap[page].start = 0;
        coremap[page].next_allocated = 0; 
        page = next_page;
    }
    spinlock_release(&coremap_lock);
    return;
}

/*
 * Return amount of memory (in bytes) used by allocated coremap pages.  If
 * there are ongoing allocations, this value could change after it is returned
 * to the caller. But it should have been correct at some point in time.
 */
unsigned int coremap_used_bytes(void){
    return 0;
}

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown * shootdown){
    (void) shootdown;
    return;
}

