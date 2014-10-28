/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the buddy algorithm
 *    Author: Stefan Birrer
 *    Copyright: 2004 Northwestern University
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    Revision 1.2  2009/10/31 21:28:52  jot836
 *    This is the current version of KMA project 3.
 *    It includes:
 *    - the most up-to-date handout (F'09)
 *    - updated skeleton including
 *        file-driven test harness,
 *        trace generator script,
 *        support for evaluating efficiency of algorithm (wasted memory),
 *        gnuplot support for plotting allocation and waste,
 *        set of traces for all students to use (including a makefile and README of the settings),
 *    - different version of the testsuite for use on the submission site, including:
 *        scoreboard Python scripts, which posts the top 5 scores on the course webpage
 *
 *    Revision 1.1  2005/10/24 16:07:09  sbirrer
 *    - skeleton
 *
 *    Revision 1.2  2004/11/05 15:45:56  sbirrer
 *    - added size as a parameter to kma_free
 *
 *    Revision 1.1  2004/11/03 23:04:03  sbirrer
 *    - initial version for the kernel memory allocator project
 *
 ***************************************************************************/
#ifdef KMA_BUD
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/************Private include**********************************************/
#include "kma_page.h"
#include "kma.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */
#define MAX_ORDER 5
#define BLOCKSIZE 255
#define HEAD_PTRS (void*)(my_page->ptr + sizeof(kma_page_t*) + 4)
#define BITFIELD (int*)(my_page->ptr + sizeof(kma_page_t*))
#define LINE printf("LINE: %d\n", __LINE__)

typedef struct freeEntry {
	struct freeEntry * next;
	struct freeEntry * previous;
} freeEntry;

typedef struct headers {
	struct freeEntry * zero;
	struct freeEntry * one;
	struct freeEntry * two;
	struct freeEntry * three;
	struct freeEntry * four;
	struct freeEntry * five;
} headers;
/************Global Variables*********************************************/
static kma_page_t* my_page = NULL;
/************Function Prototypes******************************************/
int get_order(int);
void * get_matching_block(int);
freeEntry * split_and_get(int);
int map_num(int);
void mark_allocated(freeEntry *, int);
void mark_free(freeEntry *, int);
void find_and_combine(freeEntry *, int);
freeEntry* get_entry_by_level(headers *, int);
void set_entry_by_level(headers *, int, freeEntry*);
/************External Declaration*****************************************/

/**************Implementation***********************************************/

/*
General Notes
PAGESIZE = 8192
sizeof(kma_page_t*) = 4
need to make number of blocks fit evenly into 8188-number of blocks
min block = 255B, then there are 32 blocks (8160) accessible memory
4B bitmap (1 bit for each block)
24B for free list pointers 
8192 - 4 - 24 - 4 = 8160
order 0 block = 255, 1 = 510, 2 = 1020, 3 = 2040, 4 = 4080, 5 = 8160
each free block has a next and previous pointer in it
allocated blocks have no header
every page has first 4 bytes as kma_page_t*, bytes 4-8 as bitfield, bytes 8-32 as head_ptrs, and 32-8192 as 32 allocatable blocks of size 255
*/

void* kma_malloc(kma_size_t malloc_size){
	printf("Mallocing %d\n", malloc_size);
	if (malloc_size > 8160){
		return NULL;
	}
	
	//head_ptrs is the array of pointers to the 6 free pointers (each free block has ptr to next and ptr to prev) 
	if (my_page == NULL){
		my_page = get_page();

		//always put the bit_field at page’s baseaddr + 4
		int * bit_field = BITFIELD;
		//all of the blocks are initially free
		*bit_field = 0;

		//make an array of head pointers (takes 24 bytes)
		headers * h = (headers*)HEAD_PTRS;
		h->zero = NULL;
		h->one = NULL;
		h->two = NULL;
		h->three = NULL;
		h->four = NULL;
		h->five = (freeEntry*)((int)h + 24);
		h->five->next = NULL;
		h->five->previous = NULL;
	}
	//get the desired order
	int order = get_order(malloc_size);
	printf("Order: %d\n", order);
	if (order == -1){
		return NULL;
	}
	return get_matching_block(order);
}

//finds the closest order that has block sizes > malloc_size
int get_order(int malloc_size){
	int i;
	for (i = 0; i < 6; i++){
		if (map_num(i) > malloc_size){
			return i;
		}
	}
	return -1;
}

//searches through the free head ptrs for a block of the matching order
//recursively splits blocks of larger order until at least one exists of matching
//returns NULL if no larger-order blocks can be split
void * get_matching_block(int order){
	if (order > 5 || order < 0){
		return NULL;
	}
	// its offset by the kma pointer and the bitfield
	headers* h = (headers *)HEAD_PTRS;
	freeEntry* entry = get_entry_by_level(h, order);

	//if we find an appropriate block, update head_ptrs, bitmap, then return
	if (entry != NULL){
		printf("Got an entry at %p on %p\n", (void *)entry, BASEADDR(entry));
		mark_allocated(entry, order);
	}
	//didn’t find an entry so we have to split until we find one of the right order
	else{
		entry = split_and_get(order);
	}
	return (void*)entry;
}

//there isn’t an entry in the head_ptrs for order, so look in higher-order blocks
//split if found and set level to order and continue searching
//if match found and order == level, return
freeEntry * split_and_get(int order){
	headers* h = (headers*)HEAD_PTRS;
	int level = order + 1;
	freeEntry* entry = get_entry_by_level(h, level);
	while (level <= MAX_ORDER && level >= 0){
		entry = get_entry_by_level(h, level);
		printf("Checking %d\n", level);
		//found one of a higher order, split it and return one
		//we already know there isn’t one at a lower level
		if (entry != NULL){
			if (level == order){
				printf("Found an entry at the desired level: %d\n", order);
				mark_allocated(entry, order);
				return entry;
			}
			printf("SPLITTING: %d\n", level);
			freeEntry* b = (freeEntry*)((int)entry + (int)pow(2, level - 1));
			printf("Weird calculation worked\n");
			if (entry->next != NULL){
				LINE;
				set_entry_by_level(h, level, entry->next);
				entry->next->previous = NULL;
			}
			printf("Set entry by level\n");
			
			//link in the two entries into the lower level
			entry->next = b;
			entry->previous = NULL;
			b->previous = entry;
			b->next = NULL;
			set_entry_by_level(h, level-1, entry);
			level--;
			printf("linked entries to lower level\n");
		}
		else{
			printf("Leaving %d\n", level);
			level++;
		}
	}
	return NULL;
}

//maps from orders to sizes and sizes to orders
//([0, 1, 2 … 5], [255, 510, 1020 … 8160])
int map_num(int num){
	if (num < 6 && num >= 0){
		return 255 * pow(2, num);
	}
	else if ((num % 255) < 6 && (num % 266) >= 0){
		return num % 266;
	}
	else{
		printf("Got a bad argument to map_num: %d\n", num);
		exit(EXIT_FAILURE);
	}
}

//marks in the bitfield that the block is allocated
//also updates the list header if the used block was the header
void mark_allocated(freeEntry * entry, int order){
	headers * h = (headers*)HEAD_PTRS;
	//entry’s next becomes the head_ptr of the list
	set_entry_by_level(h, order, entry->next);
	if (entry->next != NULL){
		entry->next->previous = NULL;
	}

	//update the bitmap to show that the space at entry is allocated
	int * bit_field = BITFIELD;
	int entry_size = pow(2,order);
	int i, relative, index;

	//entry’s addr - base addr - 32 should give the bitfield-relative addressing (% 255)
	relative = (int)entry - (int)BASEADDR(entry) - 32;
	index = relative / 255;
	for (i = 0; i < entry_size; i++){
		//update the bit at each position the entry covers (based on order)
		*bit_field |= 1 << (index+i);
	}
	return;
}

//updates the bitfield and the linked list headers that the block is free
void mark_free(freeEntry* entry, int order){
	int * bitfield = BITFIELD;
	headers * h = (headers*)HEAD_PTRS;
	freeEntry * current = get_entry_by_level(h, order);
	if (current == NULL){
		entry->next = NULL;
		entry->previous = NULL;
	}
	else{
		entry->next = current;
		current->previous = entry;
	}
	set_entry_by_level(h, order, entry);
	int entry_size = pow(2,order);
	int i, relative, index;

	//entry’s addr - base addr - 32 should give the bitfield-relative addressing (% 255)
	relative = (int)entry - (int)BASEADDR(entry) - 32;
	index = relative / 255;
	for (i = 0; i < entry_size; i++){
		//update the bit at each position the entry covers (based on order)
		*bitfield &= 0 << (index+i);
	}
	return;
}

void kma_free(void* ptr, kma_size_t size){
	printf("Freeing %d\n", size);
	// have to free the entire block, not a fraction
	int* bitfield = BITFIELD;
	if (size < 255){
		size = 255;
	}
	freeEntry * entry = (freeEntry*)ptr;
	int order = get_order(size);
	
	//update bitfield and linked lists
	mark_free(entry, order);

	//see if the buddy of entry is free. if so, combine them
	//if combined, look for the new combo’s boddy
	find_and_combine(entry, order);
	if (*bitfield == 0){
		free_page(my_page->ptr);
	}
	return;
}


//searches for the buddy of entry and combines them if found
//if combines, then it looks for the new combo’s buddy
void find_and_combine(freeEntry *entry, int order){
	int * bitfield = BITFIELD;
	headers* h = (headers*)HEAD_PTRS;
	int size = pow(2, order);
	int relative, index, i;
	bool free = TRUE;
	
	relative = (int)entry - (int)BASEADDR(entry) - 32;
	index = relative / 255;
	int b_index = index & size;
	for (i = 0; i < size; i++){
		//if any spot in buddy is not free, we can’t combine
		if((*bitfield & (1 << (b_index+i))) > 0){
			free = FALSE;
		}
	}
	if (free){
		//relink their old neighbor’s pointers
		freeEntry* buddy = (freeEntry*)((int)entry & (size*255));
		//save the entry with the lower addr, so swap addrs
		if ((int)buddy < (int)entry){
			int budint = (int)buddy;
			int entint = (int)entint;
			entint = entint ^ budint;
			budint = entint ^ budint;
			entint = entint ^ budint;
			buddy = (freeEntry*)budint;
			entry = (freeEntry*)entint;
		}

		if (buddy->next && buddy->previous){
			buddy->next->previous = buddy->previous;
			buddy->previous->next = buddy->next->previous;
	}
	else if (buddy->previous){
		buddy->previous->next = NULL;
	}
	if (get_entry_by_level(h, order) == buddy){
		set_entry_by_level(h, order, buddy->next);
	}
		//link them into the higher order of the head_ptrs
		freeEntry* higher = get_entry_by_level(h, order+1);
		if (higher != NULL){
			entry->next = higher;
			higher->previous = entry;
		}
		set_entry_by_level(h, order+1, entry);;
		//try to join the combined entry to its new buddy
		find_and_combine(entry, order+1);
	}
	return;
}

freeEntry* get_entry_by_level(headers * h, int order){
	freeEntry* entry = NULL;
	switch (order){
		case 0:
		entry = h->zero;
		break;
		case 1:
		entry = h->one;
		break;
		case 2:
		entry = h->two;
		break;
		case 3:
		entry = h->three;
		break;
		case 4:
		entry = h->four;
		break;
		case 5:
		entry = h->five;
		break;
	}
	return entry;
}

void set_entry_by_level(headers * h, int order, freeEntry * entry){
	printf("Setting entry %d with memory at %p\n", order, (void*)entry);
	switch (order){
		case 0:
		h->zero = entry;
		break;
		case 1:
		h->one = entry;
		break;
		case 2:
		h->two = entry;
		break;
		case 3:
		h->three = entry;
		break;
		case 4:
		h->four = entry;
		break;
		case 5:
		h->five = entry;
		break;
	}
	return;
}


#endif // KMA_BUD
