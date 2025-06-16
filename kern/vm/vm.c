#include <types.h>
#include <spl.h>
#include <proc.h>
#include <spinlock.h>
#include <synch.h>
#include <current.h>
#include <lib.h>
#include <vm.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <cpu.h>


void save_tlb_state_to_page_tables(void);
void init_coremap(paddr_t , size_t );
paddr_t get_last_level_pt(vaddr_t vaddr, struct addrspace *as);
unsigned int coremap_alloc_userpage(void);
/* Coremap Spinlock */
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;


void init_coremap(paddr_t start_paddr, size_t num_pages){
    size_t start_page;
    size_t i;
    start_page = start_paddr / PAGE_SIZE;

    for (i = 0; i < start_page; i++){
        coremap[i].allocated = 1;
        coremap[i].kernel = 1;
        coremap[i].reference_count = 1;
        coremap[i].owner = 0;
        coremap[i].last_access = 1000;
        coremap[i].end = 1;
        coremap[i].start = 1;
        coremap[i].next_allocated = i;
        if (i < num_pages - 1)
            coremap[i].next_free = i + 1;
        else 
            coremap[i].next_free = 0;
    }

    for (i = 0; i < num_pages; i++)
    {
        coremap[start_page + i].allocated = 0;
        coremap[start_page + i].kernel = 0;
        coremap[start_page + i].reference_count = 0;
        coremap[start_page + i].owner = 0;
        coremap[start_page + i].last_access = 0;
        coremap[start_page + i].end = 0;
        coremap[start_page + i].start = 0;
        coremap[start_page + i].next_allocated = 0;
        if (i < num_pages - 1)
            coremap[start_page + i].next_free = start_page + i + 1;
        else 
            coremap[start_page + i].next_free = start_page;
    }

    
    first_free_page = start_page;
    first_page = start_page;
    first_page_paddr = start_paddr;
    total_free_pages = num_pages;
    total_pages = num_pages;
    used_pages = start_page;
    asid_bitmap = bitmap_create(MAX_ASID);
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

    coremap_paddr = ram_stealmem(coremap_pages);

    if (coremap_paddr <= 0 ){
        panic("Cannot allocate memory for coremap");
    }

    coremap = (struct coremap_entry *)PADDR_TO_KVADDR(coremap_paddr);

    paddr_t first_free_paddr = ram_getfirstfree();

    size_t manageable_pages = ramsize / PAGE_SIZE;

    init_coremap(first_free_paddr, manageable_pages);

    return;
}



/*
 * Allocate/free kernel heap pages (called by kmalloc/kfree) 
 * We allocate contigous pages for kernel. 
 */
vaddr_t alloc_kpages(unsigned npages){
    spinlock_acquire(&coremap_lock);
    size_t page,i;
    size_t starting_page,ending_page;
    size_t max_found, aquired_pages, max_consecutive;
    int p_num;
    max_found = 0; /* variable used for debugging purposes */
    ending_page = 0;
    starting_page = first_page;
    aquired_pages = 0;
    max_consecutive= 0;
    (void) i;
    (void) p_num;

    DEBUG(DB_VM, "Allocating %d pages for %d\n", npages, curproc->pid);
    /*
     * Search the whole coremap for free pages
     * Our priority is to find consecutive pages 
     * If no consecutive pages are found just take
     * the first n free pages
     * 
     * UPDATE: Changed the system to find the n first free pages.
     * will probably change this since I think it adds slowdown(not sure)
     * 
     * 
     * IDEA: Change the system to use an array for allocation istead of next frees !!!!
     */
    for (page = first_page; page < total_pages; page++)
    {
        if (!coremap[page].allocated){
            // /* is set to be deleted*/
            // gc
            // /**/
            aquired_pages += 1;
            max_found += 1;
            max_consecutive = max_consecutive > aquired_pages ? max_consecutive : aquired_pages;
            if (aquired_pages == 1)
                first_free_page = page;
            // if (max_found < 1)
            //     first_free_page = page;
            // if (max_found < npages)
            //     max_found += 1;
        } else {
            aquired_pages = 0;
        }
        if (aquired_pages == npages){
            ending_page = page;
            starting_page = page - (npages - 1);
            KASSERT(starting_page == first_free_page);
            break;
        }
        // if (max_found == npages){
        //     ending_page = page;
        //     starting_page = first_free_page;
        //     break;
        // }
    }

    /* Make sure we have enough pages left to allocate 
     * TODO: Change this for consistancy
     */
    if (aquired_pages < npages)
    {
        /* TODO: fix this when fixing swap*/
        // panic("not enough consecutive memory !!! -- max found : %d -- used pages : %d ", max_consecutive, used_pages);
        spinlock_release(&coremap_lock);
        return 0;
    }

    /* Set start and end of our allocation */
    coremap[starting_page].start = 1;
    coremap[ending_page].end = 1;

    /* start the allocation
     * This is fairly complicated for "all consecutive" allocations
     * Keeping it around to use for non-kernel memory allocations
     */

    // page = starting_page;
    // for (i = 0; i < npages; i++)
    // {
    //     p_num = page;
    //     coremap[page].allocated = 1;
    //     coremap[page].kernel = 1;
    //     if (curthread != NULL && curproc != NULL)
    //         coremap[page].owner = (unsigned int)(curproc->pid);
    //     coremap[page].reference_count += 1;
    //     coremap[page].allocation_size = npages;

    //     /* TODO: Need to fix this timestamp*/
    //     coremap[page].last_access = 1000;

    //      /*
    //       * Fix previous pages that have this page as their next free page 
    //       */
    //      while(true){
    //          p_num = p_num - 1 < (int)first_page ? (int)total_pages - 1 : p_num - 1;
    //          coremap[p_num].next_free = coremap[ending_page].next_free;
    //          if (!coremap[p_num].allocated || coremap[p_num].next_allocated == page)
    //              break;
    //      }

    //     /* 
    //      * Make sure we are not in the end of allocation 
    //      * set the next allocated as the next free 
    //      */
    //     if (!coremap[page].end){
    //         coremap[page].next_allocated = coremap[page].next_free;
    //         coremap[page].next_free = coremap[ending_page].next_free;
    //         page = coremap[page].next_allocated;
    //     } else {
    //         coremap[page].next_allocated = starting_page;
    //     }

    // } 

    /* Naive approach which only works on consecutive allocations */
    for (page = starting_page; page <= ending_page; page++)
    {
        coremap[page].allocated = 1;
        coremap[page].kernel = 1;
        if (curthread != NULL && curproc != NULL)
            coremap[page].owner = (unsigned int)(curproc->pid);
        coremap[page].reference_count += 1;
        coremap[page].allocation_size = npages;
        coremap[page].next_allocated = page + 1 > ending_page ? starting_page : page + 1;
        coremap[page].next_free = coremap[ending_page].next_free;
        /* TODO: Need to fix this timestamp*/
        coremap[page].last_access = 1000;
    }
    

    used_pages += npages;

    paddr_t page_paddr;
    page_paddr = PAGE_TO_PADDR(starting_page);
    
    spinlock_release(&coremap_lock);
    return PADDR_TO_KVADDR(page_paddr);
}


void free_kpages(vaddr_t addr){
    spinlock_acquire(&coremap_lock);
    size_t page_paddr,page,i,next_page;
    int p_num;
    /* Hope this doesnt wrongly align */
    if ((addr & PAGE_FRAME) != addr) {
        panic("free_kpages: address 0x%x is not page-aligned", addr);
    }
    page_paddr = KVADDR_TO_PADDR(addr);
    page = PADDR_TO_PAGE(page_paddr);
    if (!coremap[page].start){
        panic("Tried freeing non-start page");
    }
    DEBUG(DB_VM, "Deallocating %d pages for %d \n", coremap[page].allocation_size, coremap[page].owner);

    used_pages -= coremap[page].allocation_size;
    /* TODO: Check the owner and pid */ 
    next_page = page;
    for (i = 0; i < coremap[page].allocation_size; i++)
    {
        p_num = page;
        if (!coremap[page].allocated)
        {
            panic("Tried freeing non-allocated page : %d -- allocsize : %d", page, coremap[page].allocation_size);
        }

        /* TODO: Make this reference counting based !!! */
        coremap[page].allocated = 0;
        coremap[page].kernel = 0;
        if (curthread != NULL && curproc != NULL)
            coremap[page].owner = 0;
        coremap[page].reference_count -= 1;
        
    
        /* TODO: Need to fix this timestamp*/
        coremap[page].last_access = 1000;

        /* Make sure to fix all of the previous frees */
        while (true)
        {
            p_num = p_num - 1 < (int)first_page ? (int)total_pages - 1 : p_num - 1;
            coremap[p_num].next_free = page;
            if (!coremap[p_num].allocated)
                break;
        }

        next_page = coremap[page].next_allocated;
        coremap[page].start = 0;
        coremap[page].allocation_size = 0;
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
    return used_pages * PAGE_SIZE;
}




vaddr_t get_last_level_pt(vaddr_t vaddr, struct addrspace *as){
    KASSERT(as != NULL);
    unsigned int first_level_index;
    struct page_table *pt,*new_pt;
    pt = as->pt;
    KASSERT(pt != NULL);
    first_level_index = FIRST_LEVEL_MASK(vaddr);
    if (pt->entries[first_level_index].valid == 0){
        pt->entries[first_level_index].valid = 1;
        new_pt = kmalloc(sizeof(struct page_table));
        memset(new_pt, 0, sizeof(struct page_table)); // Initialize the new page table
        KASSERT(new_pt != NULL);
        DEBUG(DB_VM, "Creating new page table 2 - index %x - %p - %x\n",
                                 first_level_index, new_pt, PADDR_TO_PAGE(KVADDR_TO_PADDR((int)new_pt)));
        pt->entries[first_level_index].frame = PADDR_TO_PAGE(KVADDR_TO_PADDR((int)new_pt));
        pt->entries[first_level_index].dirty = 0;
        pt->entries[first_level_index].accessed = 0;
        pt->entries[first_level_index].readable = 1; 
        pt->entries[first_level_index].writable = 0;
        pt->entries[first_level_index].executable = 0; // MIPS does not have hardware execute bits
    }
    

    pt = (struct page_table *)PADDR_TO_KVADDR(PAGE_TO_PADDR(pt->entries[first_level_index].frame));
    DEBUG(DB_VM, "First level looked up -->> %x got %p\n",
          first_level_index, pt);
    unsigned int second_level_index;
    second_level_index = SECOND_LEVEL_MASK(vaddr);
    if (pt->entries[second_level_index].valid == 0){
        pt->entries[second_level_index].valid = 1;
        new_pt = kmalloc(sizeof(struct page_table));
        memset(new_pt, 0, sizeof(struct page_table)); // Initialize the new page table
        KASSERT(new_pt != NULL);
        DEBUG(DB_VM, "Creating new page table 3 - index %x - %p - %x\n",
                                 second_level_index, new_pt, PADDR_TO_PAGE(KVADDR_TO_PADDR((int)new_pt)));
        pt->entries[second_level_index].frame = PADDR_TO_PAGE(KVADDR_TO_PADDR((int)new_pt)); 
        pt->entries[second_level_index].dirty = 0;
        pt->entries[second_level_index].accessed = 0;
        pt->entries[second_level_index].readable = 1; 
        pt->entries[second_level_index].writable = 0;
        pt->entries[second_level_index].executable = 0; // MIPS does not have hardware execute bits
    }
    pt = (struct page_table *)PADDR_TO_KVADDR(PAGE_TO_PADDR(pt->entries[SECOND_LEVEL_MASK(vaddr)].frame));
    DEBUG(DB_VM, "Second level looked up -->> %x got %p\n",
          second_level_index,  pt);
    return (int)pt;
}

/*
 * Allocate a single user page in the coremap.
 */
unsigned int coremap_alloc_userpage(){
    vaddr_t addr;
    unsigned int page;
    addr = alloc_kpages(1); // Allocate one page for user space
    if (addr == 0)
    {
        return 0; // Allocation failed
    }
    page = PADDR_TO_PAGE(KVADDR_TO_PADDR(addr));
    coremap[page].kernel = 0;
    
    return page;
}

void free_page_table(void *page_table, size_t level){
    struct page_table *pt;
    pt = (struct page_table *)page_table;
    if (pt == NULL) {
        return; // Nothing to free
    }
    for (size_t i = 0; i < PAGE_TABLE_SIZE; i++)
    {
        if (pt->entries[i].valid) {
            if (level < PT_LEVELS) {
                // Recursively free the next level
                struct page_table *next_pt = (struct page_table *)PADDR_TO_KVADDR(PAGE_TO_PADDR(pt->entries[i].frame));
                free_page_table(next_pt, level + 1);
            }
            // Free the current page table entry
            kfree((void *)PADDR_TO_KVADDR(PAGE_TO_PADDR(pt->entries[i].frame)));
        }
    }
}

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress){
    if (faultaddress >= USERSPACETOP) {
        DEBUG(DB_VM,"vm_fault: faultaddress 0x%x is above USERSPACETOP 0x%x\n", faultaddress, USERSPACETOP);
        return EFAULT; // Invalid address
    }
    if (faulttype != VM_FAULT_READ && faulttype != VM_FAULT_WRITE && faulttype != VM_FAULT_READONLY) {
        DEBUG(DB_VM,"vm_fault: invalid fault type %d\n", faulttype);
        return EINVAL; // Invalid fault type
    }
    if (curproc == NULL || curproc->p_addrspace == NULL) {
        DEBUG(DB_VM,"vm_fault: no current process or address space\n");
        return EFAULT; // No current process or address space
    }
    if (faulttype == VM_FAULT_READONLY) {
        DEBUG(DB_VM,"vm_fault: read-only fault type is not supported\n");
        return EFAULT; // Read-only fault type is not supported
    }

    /* Check Vm_regions to see if virtaddr is valid and has permissions*/
    struct vm_region *region;
    region = curproc->p_addrspace->regions;
    while (region != NULL) {
        if ((faultaddress >= region->start) && (faultaddress < region->start + region->size)) {
            if ((faulttype == VM_FAULT_READ && !region->readable) ||
                (faulttype == VM_FAULT_WRITE && !(region->writeable || region->temp_write))) {
                DEBUG(DB_VM,"vm_fault: permission denied for faultaddress 0x%x in region [%p, %p)\n",
                        faultaddress, (void *)region->start, (void *)(region->start + region->size));
                return EFAULT; // Permission denied
            }
            break; // Found a valid region
        }
        region = region->next;
    }
    /* If the region is NULL check if it is stack or heap region */
    if (faultaddress >= MAX_USERSTACK && faultaddress < USERSPACETOP) {
        region = curproc->p_addrspace->stack_region;
    } else if (faultaddress >= curproc->p_addrspace->heap_region->start &&
               faultaddress < curproc->p_addrspace->heap_region->start + curproc->p_addrspace->heap_region->size) {
        region = curproc->p_addrspace->heap_region;
    }
    if (region == NULL) {
        DEBUG(DB_VM, "vm_fault: faultaddress 0x%x is not in any valid region\n", faultaddress);
        return EFAULT; // No valid region found for the fault address
    }



    vaddr_t third_level_index, third_level_pt;
    third_level_index = THIRD_LEVEL_MASK(faultaddress);
    third_level_pt = get_last_level_pt(faultaddress, curproc->p_addrspace);
    struct page_table *pt = (struct page_table *)third_level_pt;
    if (pt->entries[third_level_index].valid == 0) {
        // Page fault: allocate a new page
        unsigned int new_page_frame = coremap_alloc_userpage(); // Allocate one page
        if (new_page_frame == 0) {
            return ENOMEM; // Out of memory
        }
        KASSERT(new_page_frame < total_pages); // Ensure the page frame is within bounds
        pt->entries[third_level_index].frame = new_page_frame;
        pt->entries[third_level_index].valid = 1;
        pt->entries[third_level_index].dirty = 1; 
        pt->entries[third_level_index].readable = region->readable;
        pt->entries[third_level_index].writable = region->writeable || region->temp_write;
        /* This doesn't actually matter as MIPS does not have hardware execute bits*/
        pt->entries[third_level_index].executable = region->executable;
    } 
    pt->entries[third_level_index].accessed = 1;
    int frame = pt->entries[third_level_index].frame;
    int valid = pt->entries[third_level_index].valid;
    int dirty = pt->entries[third_level_index].dirty;
    // Updating the TLB entry
    // We need to set the TLBHI and TLBLO entries 
    uint32_t tlbhi = faultaddress & TLBHI_VPAGE;
    tlbhi |= (curproc->p_addrspace->asid << TLBHI_ASID_SHIFT) & TLBHI_PID;
    uint32_t tlblo = (PAGE_TO_PADDR(frame)& TLBLO_PPAGE) |
                     (dirty ? TLBLO_DIRTY : 0) |
                     (valid ? TLBLO_VALID : 0);
    DEBUG(DB_VM, "vm_fault ---> addr : 0x%x, third_level_index : %x, frame : %x, paddr : %x, loo : %x, tlbhi : 0x%x, tlblo : 0x%x\n",
          faultaddress, third_level_index, pt->entries[third_level_index].frame, PAGE_TO_PADDR(pt->entries[third_level_index].frame), (PAGE_TO_PADDR(pt->entries[third_level_index].frame)& TLBLO_PPAGE), tlbhi, tlblo);
    tlb_random(tlbhi, tlblo);
    return 0;
}

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *ts) {
    KASSERT(ts != NULL);
    
    switch (ts->type) {
        case TLB_SHOOTDOWN_ASID:
            if (ts->asid != 0) {
                tlb_invalidate_asid_entries(ts->asid);
            }
            break;
            
        case TLB_SHOOTDOWN_ALL:
            tlb_shootdown_all();
            break;
            
        default:
            panic("vm_tlbshootdown: Unknown shootdown type %d\n", ts->type);
    }
}

/*
 * Invalidate all TLB entries for a specific ASID.
 * This function is called when an address space is destroyed.
 */
void tlb_invalidate_asid_entries(uint32_t asid) { 
    int spl = splhigh();
    for (int i = 0; i < NUM_TLB; i++) {
        uint32_t entryhi, entrylo;
        tlb_read(&entryhi, &entrylo, i);
        if (!(entrylo & TLBLO_VALID)) {
            continue; // Skip invalid entries
        }
        if (((entryhi & TLBHI_PID) >> TLBHI_ASID_SHIFT) == asid) {
            tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
        }
    }
    splx(spl);
}

/* Do a full tlb shootdown */
void tlb_shootdown_all(void) {
    // Invalidate all TLB entries
    int spl = splhigh();
    for (int i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    splx(spl);
}

void shootdown_all_asid(uint8_t asid) {
    struct tlbshootdown ts;
    unsigned int i;
    ts.asid = asid;
    ts.type = TLB_SHOOTDOWN_ASID;
    ts.vaddr = 0; // Not used for ASID shootdown
    vm_tlbshootdown(&ts);
    for (i = 0; i < num_cpus; i++)
    {
        struct cpu *cpu = cpu_get_by_number(i);
        if (cpu == curcpu){
            vm_tlbshootdown(&ts); // Call directly if it's the current CPU
        }
        else if (cpu != NULL) {
            ipi_tlbshootdown(cpu, &ts);
        }
    }
}

void save_tlb_state_to_page_tables() {
    for (int i = 0; i < NUM_TLB; i++) {
        uint32_t entryhi, entrylo;
        tlb_read(&entryhi, &entrylo, i);
        
        if (entrylo & TLBLO_VALID) {
            paddr_t paddr = entrylo & TLBLO_PPAGE;
            
            struct page_table_entry *pte = (struct page_table_entry *)paddr;
            if (pte != NULL) {
                // Copy dirty bit from TLB to page table
                if (entrylo & TLBLO_DIRTY) {
                    pte->dirty = 1;
                }
            }
        }
    }
}



void show_all_tlb_entries(void) {
    int spl;
    uint32_t entryhi, entrylo;
    
    kprintf("=== TLB ENTRIES DUMP ===\n");
    kprintf("Entry | Valid | ASID | Virtual Addr | Physical Addr | Dirty | Global\n");
    kprintf("------|-------|------|--------------|---------------|-------|-------\n");
    
    // Disable interrupts while reading TLB
    spl = splhigh();
    
    for (int i = 0; i < NUM_TLB; i++) {
        tlb_read(&entryhi, &entrylo, i);
        
        // Extract fields
        int valid = (entrylo & TLBLO_VALID) ? 1 : 0;
        int dirty = (entrylo & TLBLO_DIRTY) ? 1 : 0;
        int global = (entrylo & TLBLO_GLOBAL) ? 1 : 0;
        uint32_t asid = (entryhi & TLBHI_PID) >> TLBHI_ASID_SHIFT;
        uint32_t vaddr = entryhi & TLBHI_VPAGE;
        uint32_t paddr = entrylo & TLBLO_PPAGE;
        
        kprintf(" %2d   |   %d   |  %2d  | 0x%08x   | 0x%08x    |   %d   |   %d  \n",
                i, valid, asid, vaddr, paddr, dirty, global);
    }
    
    splx(spl);
    kprintf("========================\n");
}

// Shorter version that only shows valid entries
void show_valid_tlb_entries(void) {
    int spl;
    uint32_t entryhi, entrylo;
    int valid_count = 0;
    
    kprintf("=== VALID TLB ENTRIES ===\n");
    
    spl = splhigh();
    
    for (int i = 0; i < NUM_TLB; i++) {
        tlb_read(&entryhi, &entrylo, i);
        
        if (entrylo & TLBLO_VALID) {
            valid_count++;
            uint32_t asid = (entryhi & TLBHI_PID) >> TLBHI_ASID_SHIFT;
            uint32_t vaddr = entryhi & TLBHI_VPAGE;
            uint32_t paddr = entrylo & TLBLO_PPAGE;
            int dirty = (entrylo & TLBLO_DIRTY) ? 1 : 0;
            
            kprintf("TLB[%2d]: ASID=%2d  0x%08x -> 0x%08x  %s\n", 
                    i, asid, vaddr, paddr, dirty ? "DIRTY" : "CLEAN");
        }
    }
    
    splx(spl);
    kprintf("Total valid entries: %d/%d\n", valid_count, NUM_TLB);
    kprintf("========================\n");
}