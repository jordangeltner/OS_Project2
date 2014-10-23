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

#define KMA_ADD(x) ((int)x+sizeof(kma_page_t*)+sizeof(kma_page_t))
#define KMA_SUB(x) ((int)x-sizeof(kma_page_t*)-sizeof(kma_page_t))
typedef struct resourceHead {
	int base;
	int size;
	struct resourceHead * next;
} resourceEntry;

/************Global Variables*********************************************/
resourceEntry* g_resource_map = NULL;


/************Function Prototypes******************************************/
static bool coalesce(resourceEntry*);

/************External Declaration*****************************************/

/**************Implementation***********************************************/


void* kma_malloc(kma_size_t malloc_size){
	//printf("Malloc:%d line:%d\n",(int)malloc_size,__LINE__); fflush(stdout);
	if (g_resource_map == NULL)
	{
		// if the request is larger than the size of a page we can't allocate it
		if (malloc_size >= (PAGESIZE - sizeof(kma_page_t*))){return NULL;}
		kma_page_t* first = get_page();
		g_resource_map = (resourceEntry*)(first + sizeof(kma_page_t*) + malloc_size);
		g_resource_map->size = PAGESIZE - sizeof(kma_page_t*) - malloc_size;
		g_resource_map->base = first + malloc_size + sizeof(kma_page_t*);
		g_resource_map->next = NULL;
	
		return (void*)(first + sizeof(kma_page_t*));
	}
	resourceEntry* entry = g_resource_map;
	resourceEntry* prev = NULL;
	while(entry!=NULL){
		//found big enough hole
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
		while(coalesce(g_resource_map)){
			coalesced = TRUE;
		}
		if (coalesced){
			return kma_malloc(malloc_size);
		}
		//still can’t place it. create new page
		else{
			// if the request is larger than the size of a page we can't allocate it
			if (malloc_size >= (PAGESIZE - sizeof(kma_page_t*))){return NULL;}
			kma_page_t* newpage = get_page();
			resourceEntry* newentry = (resourceEntry*)(newpage + sizeof(kma_page_t*) + malloc_size);
			newentry->size = PAGESIZE - sizeof(kma_page_t*) - malloc_size;
			newentry->base = newpage + malloc_size + sizeof(kma_page_t*);
			newentry->next = NULL;
			//if there is not enough remaining free space to even store an entry, 
			//you need to delete this entry and link up the free list appropriately
			if(newentry->size<sizeof(resourceEntry)){
				//nothing before it on the list to link up
				if(prev==NULL){
					g_resource_map = newentry->next;
				}
				//link up previous entry to next entry
				else{
					prev->next = newentry->next;
				}
			}
			//link up list correctly
			else if(prev!=NULL){
				prev->next = newentry;
			}
			//set resource_map
			else{
				g_resource_map = NULL;
			}
			return (void*)(newpage + sizeof(kma_page_t*));
		}
	}
	else // updating entry to reflect the remaining free space on the page
	{
		void* ptr = (void*)entry->base;
		entry->size-=malloc_size;
		entry->base+=malloc_size;
		//if there is not enough remaining free space to even store an entry, 
		//you need to delete this entry and link up the free list appropriately
		if(entry->size<sizeof(resourceEntry)){
			//nothing before it on the list to link up
			if(prev==NULL){
				g_resource_map = entry->next;
			}
			//link up previous entry to next entry
			else{
				prev->next = entry->next;
				entry = NULL;
			}
		}
		return ptr;
	}
}

//coalesce forward, adjacent resourceEntries to items on the same physical page
static bool coalesce(resourceEntry* r_map){
	bool result = FALSE;
	resourceEntry* current = r_map;
	//resourceEntry* prev = NULL;
	while (current!=NULL && current->next!=NULL){
		//is this block directly adjacent to the next block? and on the same page?
		if((current->base+current->size)==current->next->base && BASEADDR(current)==BASEADDR(current->next)){
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

#endif // KMA_RM
