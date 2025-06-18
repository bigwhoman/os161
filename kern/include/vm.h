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

#ifndef _VM_H_
#define _VM_H_

/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */


#include <machine/vm.h>  
#include <mips/tlb.h>
#include <synch.h>

/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/
#define MAX_ASID 64 // Maximum number of address space identifiers (ASIDs)
#define PT_LEVELS 3 // Number of levels in the page table
#define FIRST_LEVEL_MASK(vaddr) ((vaddr >> 24) & 0xFF) // Mask for first-level index
#define SECOND_LEVEL_MASK(vaddr) ((vaddr >> 16) & 0xFF) // Mask for second-level index
#define THIRD_LEVEL_MASK(vaddr) ((vaddr >> 12) & 0xF) // Mask for third-level index
#define OFFSET_MASK(vaddr) (vaddr & 0xFFF) // Mask for offset within a page

struct bitmap* asid_bitmap; // Create a bitmap for ASIDs

/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(unsigned npages);
void free_kpages(vaddr_t addr);

/* Free page table and mark coremap pages as free */
void free_page_table(void *pt, size_t level);

/* COW a page table recursivly */
void *copy_page_table(void *page_table, size_t level);

/* Shootdown vaddr in all TLBs */
void tlb_shootdown_individual(vaddr_t vaddr, uint8_t asid);

/* Invalidation of vaddr */
void tlb_invalidate_vaddr(const struct tlbshootdown *ts);

/* Shootdown all tlb entries */
void tlb_shootdown_all(void);

/* Invalidate tlb entries for a specific asic */
void tlb_invalidate_asid_entries(uint32_t asid);

/*
 * Return amount of memory (in bytes) used by allocated coremap pages.  If
 * there are ongoing allocations, this value could change after it is returned
 * to the caller. But it should have been correct at some point in time.
 */
unsigned int coremap_used_bytes(void);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown(const struct tlbshootdown *);

void shootdown_all_asid(uint8_t asid);

void show_valid_tlb_entries(void);

void show_all_tlb_entries(void);

void all_tlb_shootdown(void);


#endif /* _VM_H_ */
