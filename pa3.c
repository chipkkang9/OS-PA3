
/**********************************************************************
 * Copyright (c) 2020-2023
 *  Sang-Hoon Kim <sanghoonkim@ajou.ac.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTIABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "list_head.h"
#include "vm.h"

/**
 * Ready queue of the system
 */
extern struct list_head processes;

/**
 * Currently running process
 */
extern struct process *current;

/**
 * Page Table Base Register that MMU will walk through for address translation
 */
extern struct pagetable *ptbr;

/**
 * TLB of the system.
 */
extern struct tlb_entry tlb[1UL << (PTES_PER_PAGE_SHIFT * 2)];


/**
 * The number of mappings for each page frame. Can be used to determine how
 * many processes are using the page frames.
 */
extern unsigned int mapcounts[];


/**
 * lookup_tlb(@vpn, @rw, @pfn)
 *
 * DESCRIPTION
 *   Translate @vpn of the current process through TLB. DO NOT make your own
 *   data structure for TLB, but should use the defined @tlb data structure
 *   to translate. If the requested VPN exists in the TLB and it has the same
 *   rw flag, return true with @pfn is set to its PFN. Otherwise, return false.
 *   The framework calls this function when needed, so do not call
 *   this function manually.
 *
 * RETURN
 *   Return true if the translation is cached in the TLB.
 *   Return false otherwise
 */
bool lookup_tlb(unsigned int vpn, unsigned int rw, unsigned int *pfn)
{
	// vpn is Virtual Page Number, r for read, w for write.
	// pfn is Physical Frame Number

	if(rw == ACCESS_WRITE)
		rw = 3;

	for(int i = 0; i < sizeof(tlb) / sizeof(*tlb); i++){
		if(tlb[i].valid == true && tlb[i].vpn == vpn && tlb[i].rw == rw){
			*pfn = tlb[i].pfn;
			return true;
		}
	}
	return false;
}


/**
 * insert_tlb(@vpn, @rw, @pfn)
 *
 * DESCRIPTION
 *   Insert the mapping from @vpn to @pfn for @rw into the TLB. The framework will
 *   call this function when required, so no need to call this function manually.
 *   Note that if there exists an entry for @vpn already, just update it accordingly
 *   rather than removing it or creating a new entry.
 *   Also, in the current simulator, TLB is big enough to cache all the entries of
 *   the current page table, so don't worry about TLB entry eviction. ;-)
 */
void insert_tlb(unsigned int vpn, unsigned int rw, unsigned int pfn)
{
	int index = -1;
	for(int i = 0; i < sizeof(tlb) / sizeof(*tlb); i++){
		if (tlb[i].valid == true && tlb[i].vpn == vpn && tlb[i].rw == rw){
			index = i;
			break;
		}
	}

	if(index != -1){
		tlb[index].rw = rw;
		tlb[index].pfn = pfn;
	}

	else {
		for(int i = 0; i < sizeof(tlb) / sizeof(*tlb); i++){
			if(tlb[i].valid == false){
				tlb[i].valid = true;
				tlb[i].rw = rw;
				tlb[i].vpn = vpn;
				tlb[i].pfn = pfn;
				break;
			}
		}
	}
}


/**
 * alloc_page(@vpn, @rw)
 *
 * DESCRIPTION
 *   Allocate a page frame that is not allocated to any process, and map it
 *   to @vpn. When the system has multiple free pages, this function should
 *   allocate the page frame with the **smallest pfn**.
 *   You may construct the page table of the @current process. When the page
 *   is allocated with ACCESS_WRITE flag, the page may be later accessed for writes.
 *   However, the pages populated with ACCESS_READ should not be accessible with
 *   ACCESS_WRITE accesses.
 *
 * RETURN
 *   Return allocated page frame number.
 *   Return -1 if all page frames are allocated.
 */

unsigned int alloc_page(unsigned int vpn, unsigned int rw)
{
	int pd_index = vpn / NR_PTES_PER_PAGE;
	int pte_index = vpn % NR_PTES_PER_PAGE;

	if(current->pagetable.outer_ptes[pd_index] == NULL){
		current->pagetable.outer_ptes[pd_index] = malloc(sizeof(struct pte_directory));
	}

	current->pagetable.outer_ptes[pd_index]->ptes[pte_index].valid = true;
	current->pagetable.outer_ptes[pd_index]->ptes[pte_index].rw = rw;

	unsigned int min_pfn = 0;
	while(mapcounts[min_pfn] != 0){
		min_pfn++;
		if(min_pfn == NR_PTES_PER_PAGE){
			return -1;
		}
	}

	current->pagetable.outer_ptes[pd_index]->ptes[pte_index].pfn = min_pfn;
	current->pagetable.outer_ptes[pd_index]->ptes[pte_index].private = rw;
	mapcounts[min_pfn]++;

	return min_pfn;
}


/**
 * free_page(@vpn)
 *
 * DESCRIPTION
 *   Deallocate the page from the current processor. Make sure that the fields
 *   for the corresponding PTE (valid, rw, pfn) is set @false or 0.
 *   Also, consider carefully for the case when a page is shared by two processes,
 *   and one process is to free the page.
 */
void free_page(unsigned int vpn)
{
	int pd_index = vpn / NR_PTES_PER_PAGE;
	int pte_index = vpn % NR_PTES_PER_PAGE;

	unsigned int pfn = current->pagetable.outer_ptes[pd_index]->ptes[pte_index].pfn;
	if(current->pagetable.outer_ptes[pd_index] != NULL && current->pagetable.outer_ptes[pd_index]->ptes[pte_index].valid == true){
			current->pagetable.outer_ptes[pd_index]->ptes[pte_index].valid = false;
			current->pagetable.outer_ptes[pd_index]->ptes[pte_index].rw = 0;
			current->pagetable.outer_ptes[pd_index]->ptes[pte_index].pfn = 0;
			mapcounts[pfn]--;

			for(int i = 0; i < sizeof(tlb) / sizeof(*tlb); i++){
				if(tlb[i].valid && tlb[i].vpn == vpn){
					tlb[i].valid = false;
					break;
				}
			}
	}
}


/**
 * handle_page_fault()
 *
 * DESCRIPTION
 *   Handle the page fault for accessing @vpn for @rw. This function is called
 *   by the framework when the __translate() for @vpn fails. This implies;
 *   0. page directory is invalid
 *   1. pte is invalid
 *   2. pte is not rw but @rw is for write
 *   This function should identify the situation, and do the copy-on-write if
 *   necessary.
 *
 * RETURN
 *   @true on successful fault handling
 *   @false otherwise
 */

bool handle_page_fault(unsigned int vpn, unsigned int rw)
{
	int pd_index = vpn / NR_PTES_PER_PAGE;
	int pte_index = vpn % NR_PTES_PER_PAGE;

	// 0. page directory is invalid
	if(current->pagetable.outer_ptes[pd_index] == NULL){
		return false;
	}

	// 1. pte is invalid
	if(current->pagetable.outer_ptes[pd_index]->ptes[pte_index].valid == false){
		return false;
	}

	unsigned int pfn = current->pagetable.outer_ptes[pd_index]->ptes[pte_index].pfn;
	
	// 2. pte is not writable but @rw is for write
	if ((current->pagetable.outer_ptes[pd_index]->ptes[pte_index].rw < 2) && (current->pagetable.outer_ptes[pd_index]->ptes[pte_index].private == 3)) {
		// Copy-on-write
		unsigned int pfn = current->pagetable.outer_ptes[pd_index]->ptes[pte_index].pfn;
		

		if (mapcounts[pfn] == 1) {
			current->pagetable.outer_ptes[pd_index]->ptes[pte_index].valid = true;
			current->pagetable.outer_ptes[pd_index]->ptes[pte_index].rw = 3;
			return true;
		} 
		else if (mapcounts[pfn] > 1 ){
			for (unsigned int i = 0; i < NR_PAGEFRAMES; i++) {
				if (mapcounts[i] == 0) {
					mapcounts[i]++;
					current->pagetable.outer_ptes[pd_index]->ptes[pte_index].valid = true;
					current->pagetable.outer_ptes[pd_index]->ptes[pte_index].rw = 3;
					current->pagetable.outer_ptes[pd_index]->ptes[pte_index].pfn = i;
					break;
				}
			}
			mapcounts[pfn]--;
			return true;
		}
	}

	return false;
}


/**
 * switch_process()
 *
 * DESCRIPTION
 *   If there is a process with @pid in @processes, switch to the process.
 *   The @current process at the moment should be put into the @processes
 *   list, and @current should be replaced to the requested process.
 *   Make sure that the next process is unlinked from the @processes, and
 *   @ptbr is set properly.
 *
 *   If there is no process with @pid in the @processes list, fork a process
 *   from the @current. This implies the forked child process should have
 *   the identical page table entry 'values' to its parent's (i.e., @current)
 *   page table. 
 *   To implement the copy-on-write feature, you should manipulate the rw
 *   bit in PTE and mapcounts for shared pages. You may use pte->private for 
 *   storing some useful information :-)
 */
void switch_process(unsigned int pid)
{
    // Check if the process with @pid exists in the @processes list
	struct process* tmp;
	int flag = 0; // 1 is exist, 0 is not exist
	list_for_each_entry(tmp, &processes, list){
		if(tmp->pid == pid){
			flag = 1;
			break;
		}
	}

	struct process * new_process = (struct process*)malloc(sizeof(struct process));
	// If there is a process with @pid in @processes, switch to the process
	if(flag == 1){
		list_add_tail(&current->list, &processes);
		list_del_init(&tmp->list);
		current = tmp;
		ptbr = &(tmp->pagetable);

		for (unsigned int i = 0; i < (1UL << (PTES_PER_PAGE_SHIFT * 2)); i++) {
			tlb[i].valid = false;
		}

		return;
	}

	// If there is no process with @pid in the @processes list
	// for a process from the @current. Do like fork.
	else{
		new_process->pid = pid;

		for(int i = 0; i < NR_PTES_PER_PAGE; i++){
			if(current->pagetable.outer_ptes[i] != NULL){
				new_process->pagetable.outer_ptes[i] = malloc(sizeof(struct pte_directory));
				for(int j = 0; j < NR_PTES_PER_PAGE; j++){
					if(current->pagetable.outer_ptes[i]->ptes[j].valid == true){
						current->pagetable.outer_ptes[i]->ptes[j].rw = 1;
						new_process->pagetable.outer_ptes[i]->ptes[j] = current->pagetable.outer_ptes[i]->ptes[j];
						new_process->pagetable.outer_ptes[i]->ptes[j].rw = 1;
						mapcounts[current->pagetable.outer_ptes[i]->ptes[j].pfn]++;
					}
				}
			}
		}
	}
	list_add_tail(&current->list, &processes);
	current = new_process;
	ptbr = &(new_process->pagetable);

	for (unsigned int i = 0; i < (1UL << (PTES_PER_PAGE_SHIFT * 2)); i++) {
		tlb[i].valid = false;
	}
}