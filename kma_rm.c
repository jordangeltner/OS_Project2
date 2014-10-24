/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the resource map algorithm
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
#ifdef KMA_RM
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>

/************Private include**********************************************/
#include "kma_page.h"
#include "kma.h"
#include "stdio.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

typedef struct resourceHead {
	int base;
	int size;
	struct resourceHead * next;
} resourceEntry;

/************Global Variables*********************************************/
resourceEntry* g_resource_map = NULL;


/************Function Prototypes******************************************/
static bool coalesce();
static void printResources(char *);

/************External Declaration*****************************************/

/**************Implementation***********************************************/


void* kma_malloc(kma_size_t malloc_size){
	printf("Malloc:%d line:%d\n",(int)malloc_size,__LINE__); fflush(stdout);
	if (g_resource_map == NULL)
	{
		// if the request is larger than the size of a page we can't allocate it
		if (malloc_size >= (PAGESIZE - sizeof(kma_page_t*))){return NULL;}
		kma_page_t* first = get_page();
		
		g_resource_map = (resourceEntry*)((unsigned int)first->ptr + sizeof(kma_page_t*) + malloc_size);
		printf("Resource_map_loc: %d\n", (int)g_resource_map);
		g_resource_map->size = PAGESIZE - sizeof(kma_page_t*) - malloc_size;
		g_resource_map->base = (unsigned int)first->ptr + malloc_size + sizeof(kma_page_t*);
		g_resource_map->next = NULL;
		
		printf("first: %ld firstPage: %ld Base: %d\n", (long)first->ptr, (long)BASEADDR(first->ptr), (int)g_resource_map->base);
		printResources("Empty Resource Map");
		return (void*)((unsigned int)first->ptr + sizeof(kma_page_t*));
	}
	resourceEntry* entry = g_resource_map;
	printf("head of resource map: %d\n", entry->base);

	resourceEntry* prev = NULL;
	while(entry!=NULL){
		//found big enough hole
		printf("Searching for hole of size %d, at %d\n", malloc_size, entry->base);
		if(malloc_size <= entry->size){
			break;
		}
		prev = entry;
		entry=entry->next;
	}
	//no hole big enough
	if(entry==NULL)
	{
		bool coalesced = FALSE;
		//attempt to merge holes, if any were merged, coalesced==TRUE
		printf("Didn't find a hole for %d, coalescing\n", malloc_size);
		while(coalesce()){
			coalesced = TRUE;
		}
		if (coalesced){
			printf("Coalesced while searching for hole of size %d\n", malloc_size);
			return kma_malloc(malloc_size);
		}
		//still can’t place it. create new page
		else{
			printf("Can't find a hole even after coalescing, making a new page\n");
			// if the request is larger than the size of a page we can't allocate it
			if (malloc_size >= (PAGESIZE - sizeof(kma_page_t*))){return NULL;}
			kma_page_t* newpage = get_page();
			printf("Made a new page at %p\n", newpage->ptr);
			resourceEntry* newentry = (resourceEntry*)((unsigned int)newpage->ptr + sizeof(kma_page_t*) + malloc_size);
			newentry->size = PAGESIZE - sizeof(kma_page_t*) - malloc_size;
			newentry->base = (unsigned int)newpage->ptr + malloc_size + sizeof(kma_page_t*);
			newentry->next = NULL;
			//if there is not enough remaining free space to even store an entry, 
			//you need to delete this entry and link up the free list appropriately
			if(newentry->size<sizeof(resourceEntry)){
				//link up previous entry to next entry
				prev->next = newentry->next;
			}
			//link up list correctly
			else if(prev!=NULL){
				prev->next = newentry;
			}
			//set resource_map
			else{
				g_resource_map = NULL;
			}
			printResources("No hole fit");
			return (void*)((unsigned int)newpage->ptr + sizeof(kma_page_t*));
		}
	}
	else // updating entry to reflect the remaining free space on the page
	{
		printResources("Start of hole-filling");
		printf("Found a hole: (size: %d, base: %d)\n", entry->size, entry->base);
		void* ptr = (void*)entry->base;
		entry->size-=malloc_size;
		entry->base+=malloc_size;
		if (prev==NULL){
			g_resource_map = entry = (resourceEntry*)entry->base;
		}
		else{
			entry = (resourceEntry*)entry->base;
		}
		//if there is not enough remaining free space to even store an entry, 
		//you need to delete this entry and link up the free list appropriately
		if(entry->size < sizeof(resourceEntry)){
			printf("IT CAME IN HERE: %d\n", __LINE__);
			//nothing before it on the list to link up
			if(prev==NULL){
				g_resource_map = entry->next;
			}
			//link up previous entry to next entry
			else{
				prev->next = entry->next;
				//entry = NULL;
			}
		}
		char str[80];
		sprintf(str, "Found a hole at %d", (int)ptr);
		printResources(str);
		return ptr;
	}
}

//coalesce forward, adjacent resourceEntries to items on the same physical page
static bool coalesce(){
	bool result = FALSE;
	resourceEntry* current = g_resource_map;
	while (current!=NULL && current->next!=NULL){
		//is this block directly adjacent to the next block? and on the same page?
		if((current->base+current->size)==current->next->base && BASEADDR(current)==BASEADDR(current->next)){
			printf("Did a coalesce at %d\n", current->base);
			result = TRUE;
			current->size+=current->next->size;
			current->next=current->next->next;
		}
		current = current->next;
	}
	return result;
}


void
kma_free(void* ptr, kma_size_t size)
{
	printf("Free: ptr:%d  size:%d   line:%d\n",(int)ptr,(int)size,__LINE__); fflush(stdout);
	resourceEntry* newentry = ptr;
	newentry->base = (int)ptr;
	newentry->size = size;
	if (g_resource_map == NULL){
		newentry->next = NULL;
		g_resource_map = newentry;
		return;
	}
	resourceEntry* entry = g_resource_map;
	resourceEntry* prev = NULL;
	while(entry!=NULL){
		//found the place in the free list?
		if(entry->base > newentry->base && BASEADDR(entry->base)==BASEADDR(newentry->base)){
			if(prev==NULL){
				newentry->next = entry;
				g_resource_map = newentry;
				return;
			}
			else{
				prev->next = newentry;
				newentry->next = entry;
				return;
			}
		}
		prev = entry;
		entry = entry->next;
	}
	//if we got here, the newentry must be placed at the end of the free list
	prev->next = newentry;
	newentry->next = NULL;
	return;
}
static void printResources(char* mystr){
	resourceEntry* entry = g_resource_map;
	int counter = 0;
	printf("\n----PRINTING ENTRIES----\n");
	while(entry != NULL){
		printf("NUMBER: %d\tSIZE: %d\tBASE: %d\tNEXT: %d\tMESSAGE: %s\n", counter, entry->size, entry->base, (unsigned int)entry->next, mystr);
		entry=entry->next;
		counter++;
	}
	printf("----PRINTED %d ENTRIES----\n\n", counter);
	return;
}

#endif // KMA_RM
