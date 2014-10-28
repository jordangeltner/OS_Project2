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
#define HEAD_PTRS (void*)(my_page->ptr + sizeof(kma_page_t*) + sizeof(int*))
#define BITFIELD (int*)(my_page->ptr + sizeof(kma_page_t*))
#define LINE printf("LINE: %d\n", __LINE__)
#define DEBUG 0

typedef struct freeEntry {
	struct freeEntry * next;
	struct freeEntry * previous;
} freeEntry;

typedef struct headers {
	struct freeEntry* arr[6];
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
void print_headers(bool);
float power(float, int);
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
	if (malloc_size > (PAGESIZE - sizeof(kma_page_t*) - sizeof(int*) - sizeof(headers))){
		return NULL;
	}
	
	printf("Mallocing %d\n", malloc_size);
	
	//head_ptrs is the array of pointers to the 6 free pointers (each free block has ptr to next and ptr to prev) 
	if (my_page == NULL){
		my_page = get_page();

		//always put the bit_field at page’s baseaddr + 4
		int * bit_field = BITFIELD;
		//all of the blocks are initially free
		*bit_field = 0;

		//make an array of head pointers (takes 24 bytes)
		headers * h = (headers*)HEAD_PTRS;
		freeEntry* five = (freeEntry*)((int)h + 24);
		five->next = NULL;
		five->previous = NULL;
		int i;
		for (i = 0; i < 6; i++){
			h->arr[i] = NULL;
		}
		h->arr[5] = five;
	}
	//get the desired order
	int order = get_order(malloc_size);
	if (DEBUG > 1){printf("Order: %d\n", order);}
	if (order == -1){
		return NULL;
	}
	return get_matching_block(order);
}

//finds the closest order that has block sizes > malloc_size
int get_order(int malloc_size){
	int i, num;
	for (i = 0; i < 6; i++){
		num = map_num(i);
		if (num == -1){return -1;}
		if (num > malloc_size){
			return i;
		}
	}
	return -1;
}

//searches through the free head ptrs for a block of the matching order
//recursively splits blocks of larger order until at least one exists of matching
//returns NULL if no larger-order blocks can be split
void * get_matching_block(int order){
	if (order > MAX_ORDER || order < 0){
		return NULL;
	}
	// its offset by the kma pointer and the bitfield
	headers* h = (headers *)HEAD_PTRS;
	freeEntry* entry = h->arr[order];

	//if we find an appropriate block, update head_ptrs, bitmap, then return
	if (entry != NULL){
		if (DEBUG > 0) {printf("Found entry %p at the desired level: %d\n", (void*)entry, order);}
		mark_allocated(entry, order);
	}
	//didn’t find an entry so we have to split until we find one of the right order
	else{
		if (DEBUG >= 1){printf("Didn't find an entry at level %d, searching higher levels to split.\n", order);}
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
	freeEntry* entry = h->arr[level];
	while (level <= MAX_ORDER && level >= 0){
		entry = h->arr[level];
		print_headers(FALSE);
		//found one of a higher order, split it and return one
		//we already know there isn’t one at a lower level
		if (entry != NULL){
			if (level == order){
				if(DEBUG > 0){printf("Found entry %p at the desired level: %d\n", (void*)entry, order);}
				mark_allocated(entry, order);
				return entry;
			}
			if (DEBUG > 0){printf("SPLITTING: %d\n", level);}
			//make the current entry down a level
			h->arr[level] = entry->next;
			if (entry->next != NULL){
				entry->next->previous = NULL;
			}
			h->arr[level-1] = entry;
			if (DEBUG > 0){printf("Set an entry in level %d to the addr at %p\n", (level-1), (void*)entry);}
			//make a buddy that starts at half its original order's addr
			freeEntry* buddy = (freeEntry*)((int)entry + (int)(BLOCKSIZE*power(2, level-1)));
			//link the buddy into level-1
			entry->next = buddy;
			buddy->previous = entry;
			buddy->next = NULL;
			if (DEBUG > 0){printf("Set an entry in level %d to the addr at %p\n", (level-1), (void*)buddy);}
			level--;
		}
		else{
			level++;
		}
	}
	return NULL;
}

//maps from orders to sizes and sizes to orders
//([0, 1, 2 … 5], [255, 510, 1020 … 8160])
int map_num(int num){
	if (num < 6 && num >= 0){
		return BLOCKSIZE * power(2, num);
	}
	else if ((num % BLOCKSIZE) < 6 && (num % BLOCKSIZE) >= 0){
		return num % BLOCKSIZE;
	}
	else{
		printf("Got a bad argument to map_num: %d\n", num);
		return -1;
	}
}

//marks in the bitfield that the block is allocated
//also updates the list header if the used block was the header
void mark_allocated(freeEntry * entry, int order){
	headers * h = (headers*)HEAD_PTRS;
	//entry’s next becomes the head_ptr of the list
	h->arr[order] = entry->next;
	if (entry->next != NULL){
		entry->next->previous = NULL;
	}

	//update the bitmap to show that the space at entry is allocated
	int * bit_field = BITFIELD;
	int entry_size = power(2,order);
	int i, relative, index;

	//entry’s addr - base addr - 32 should give the bitfield-relative addressing (% 255)
	relative = (int)entry - (int)BASEADDR(entry) - 32;
	index = relative / BLOCKSIZE;
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
	freeEntry * current = h->arr[order];
	if (current == NULL){
		entry->next = NULL;
		entry->previous = NULL;
	}
	else{
		entry->next = current;
		current->previous = entry;
	}
	h->arr[order] = entry;
	int entry_size = power(2,order);
	int i, relative, index;

	//entry’s addr - base addr - 32 should give the bitfield-relative addressing (% 255)
	relative = (int)entry - (int)BASEADDR(entry) - 32;
	index = relative / BLOCKSIZE;
	for (i = 0; i < entry_size; i++){
		//update the bit at each position the entry covers (based on order)
		*bitfield &= 0 << (index+i);
	}
	return;
}

void kma_free(void* ptr, kma_size_t size){
	printf("Freeing %d\n", size);
	print_headers(TRUE);
	// have to free the entire block, not a fraction
	int* bitfield = BITFIELD;
	if (size < BLOCKSIZE){
		size = BLOCKSIZE;
	}
	//printf("Got a bitfield at %p\n", (void*) bitfield);
	freeEntry * entry = (freeEntry*)ptr;
	int order = get_order(size);
	printf("order: %d\n", order);
	
	//print_headers(TRUE);
	//update bitfield and linked lists
	mark_free(entry, order);
	//printf("Marked entry at %p as free\n", (void*) entry);

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
	if (order == MAX_ORDER){return;}
	int * bitfield = BITFIELD;
	headers* h = (headers*)HEAD_PTRS;
	int length = power(2, order);
	int relative, index, i;
	bool free = TRUE;
	
	//look in bitfield for buddy. if its allocated, return
	//buddy should be at base XOR length. a block of length 4 at addr 8's buddy
	relative = (int)entry - (int)BASEADDR(entry) - sizeof(kma_page_t*) - sizeof(int*) - sizeof(headers);
	index = relative / BLOCKSIZE;
	int b_index = index ^ length;
	int bit;
	printf("ORDER: %d\tentry index at %p, buddy index at %p. length: %d\n", order, (void*)index, (void*)b_index, length);
	//is at 100 ^ 1000 = 1100 = 12. so then check bits 12 through 12 + 4 to be free.
	for (i = b_index; i < (b_index + length); i++){
		bit = (int)(*bitfield & (1 << i));
		if (bit > 0){
			free = FALSE;
		}
	}
	//if all <length> bits are free, the buddy is free
	//if its free, combine them
	if (free){
		freeEntry* buddy = (freeEntry*)((int)entry ^ (length*BLOCKSIZE));
		//save the entry with the lower addr, so swap addrs
		if ((int)buddy < (int)entry){
			int budint = (int)buddy;
			int entint = (int)entry;
			entint = entint ^ budint;
			budint = entint ^ budint;
			entint = entint ^ budint;
			buddy = (freeEntry*)budint;
			entry = (freeEntry*)entint;
		}
		//relink their old neighbor’s pointers
		if (buddy->next && buddy->previous){
			buddy->next->previous = buddy->previous;
			buddy->previous->next = buddy->next->previous;
		}
		else if (buddy->previous){
			buddy->previous->next = NULL;
		}
		if (h->arr[order] == buddy){
			h->arr[order] = buddy->next;
		}
		else if (h->arr[order] == entry){
			h->arr[order] = entry->next;
		}
		//link them into the higher order of the head_ptrs
		freeEntry* higher = h->arr[order+1];
		if (higher != NULL){
			entry->next = higher;
			higher->previous = entry;
		}
		//add it to the head of the list for that size
		h->arr[order+1] = entry;
		//try to join the combined entry to its new buddy
		find_and_combine(entry, order+1);
	}
	return;
}

void print_headers(bool p){
	if (p == FALSE){return;}
	int i;
	headers* h = (headers *)HEAD_PTRS;
	freeEntry* entry = NULL;
	for(i = 0; i < 6; i++){
		entry = h->arr[i];
		while (entry != NULL){
			printf("Entry in level %d at address %p\n", i, (void*)entry);
			entry = entry->next;
		}
	}
}
// http://www.geeksforgeeks.org/write-a-c-program-to-calculate-powxn/
float power(float x, int y)
{
    float temp;
    if( y == 0)
       return 1;
    temp = power(x, y/2);       
    if (y%2 == 0)
        return temp*temp;
    else
    {
        if(y > 0)
            return x*temp*temp;
        else
            return (temp*temp)/x;
    }
}

#endif // KMA_BUD
