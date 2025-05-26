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
    aquired_pages = 0;
    for (page = 0; page < total_pages; page++)
    {
        if (!coremap[page].allocated){
            aquired_pages += 1; 
        } else {
            aquired_pages = 0;
        }
        if (aquired_pages == npages){
            page = page - (npages - 1);
            break;
        }
    }
    if (aquired_pages == 0){
        spinlock_release(&coremap_lock);
        return 0;
    }
    for (i = 0; i < aquired_pages; i++)
    {
        coremap[page + i].allocated = 1;
        coremap[page + i].kernel = 1;
        if (curthread != NULL && curproc != NULL)
            coremap[page + i].owner = (unsigned int)(curproc->pid);
        coremap[page + i].reference_count += 1;
        coremap[page + i].allocation_size = npages;
        coremap[page + i].next_free = coremap[page + npages -1].next_free;
        

        /* TODO: Need to fix this timestamp*/
        coremap[page + i].last_access = 1000;
    }
    coremap[page].start = 1;
    
    paddr_t page_paddr;
    page_paddr = first_page_paddr + page * PAGE_SIZE;
    
    spinlock_release(&coremap_lock);
    return PADDR_TO_KVADDR(page_paddr);
}
void free_kpages(vaddr_t addr){
    spinlock_acquire(&coremap_lock);
    size_t page_paddr,page,i;
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
    for (i = 0; i < coremap[page].allocation_size; i++)
    {
        if (!coremap[page+i].allocated)
        {
            panic("Tried freeing non-start page");
        }

        /* TODO: Make this reference counting based !!! */
        coremap[page + i].allocated = 0;
        coremap[page + i].kernel = 0;
        if (curthread != NULL && curproc != NULL)
            coremap[page + i].owner = 0;
        coremap[page + i].reference_count -= 1;
        coremap[page + i].allocation_size = 0;
        /* TODO: Need to fix for last page */
        coremap[page + i].next_free = page + i + 1;
        

        /* TODO: Need to fix this timestamp*/
        coremap[page + i].last_access = 1000;
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

