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
// static unsigned int firstbase = 0;
// #define PAGEBASE(id) ((void*)(firstbase+PAGESIZE*id))

typedef struct resourceHead {
	int base;
	int size;
	struct resourceHead * next;
} resourceEntry;

/************Global Variables*********************************************/
kma_page_t* g_resource_map = NULL;

/************Function Prototypes******************************************/
static bool coalesce();
//static void printResources(char *);

/************External Declaration*****************************************/

/**************Implementation***********************************************/


void* kma_malloc(kma_size_t malloc_size){
	if (g_resource_map == NULL)
	{
		// if the request is larger than the size of a page we can't allocate it
		if (malloc_size >= (PAGESIZE - sizeof(kma_page_t*))){return NULL;}
		kma_page_t* first = get_page();
		// safely corrects the ptr, in case get_page does not return the actual base
		if(first->ptr != BASEADDR(first->ptr)){ first->ptr = BASEADDR(first->id);}
		void* allocated = (void*)((unsigned int)first->ptr + sizeof(kma_page_t*));
		g_resource_map = first;
		g_resource_map->ptr =(void*)((unsigned int)first->ptr + sizeof(kma_page_t*) + malloc_size);
		resourceEntry* newEntry = g_resource_map->ptr;
		newEntry->size = PAGESIZE - sizeof(kma_page_t*) - malloc_size;
		newEntry->base = (int)g_resource_map->ptr;
		newEntry->next = NULL;
		return allocated;
	}
	resourceEntry* entry = (resourceEntry*)g_resource_map;
	resourceEntry* prev = NULL;
	while(entry!=NULL){
		//is this a kma_page_t*
		if(((kma_page_t*)entry)->size==PAGESIZE){
			prev = entry;
			entry=((kma_page_t*)entry)->ptr;
		}
		//entry is not a kma_page_t*, so check for size of hole
		else if(entry->size<PAGESIZE){
			if (malloc_size < 12){
				malloc_size = 12;
			}
			//found big enough hole
			if(malloc_size <= (entry->size-12)){
				break;
			}
			prev = entry;
			entry=entry->next;
		}
		else{
			printf("hard else  size:%d\n",entry->size);
			exit(EXIT_FAILURE);
			entry = NULL;
		}
	}
	//no hole big enough
	if(entry==NULL)
	{
		bool coalesced = FALSE;
		//attempt to merge holes, if any were merged, coalesced==TRUE
		while(coalesce()){
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
			//need to correct the ptr, in case it returned a bad ptr.
			if(newpage->ptr != BASEADDR(newpage->ptr)){ newpage->ptr = BASEADDR(newpage->ptr); }
			void* ptr = newpage->ptr + sizeof(kma_page_t*);
			newpage->ptr = (resourceEntry*)((unsigned int)newpage->ptr + sizeof(kma_page_t*) + malloc_size);
			resourceEntry* newentry = newpage->ptr;
			//if there is not enough remaining free space to even store an entry, 
			//you need to delete this entry and link up the free list appropriately
			if((PAGESIZE - sizeof(kma_page_t*) - malloc_size)<sizeof(resourceEntry)){
				//link up previous entry to next entry
				//if prev is a kma_page_t* access appropriately
				if(((kma_page_t*)prev)->size==PAGESIZE){
					((kma_page_t*)prev)->ptr = newpage;
					newpage->ptr = NULL;
				}
				else{
					prev->next = (resourceEntry*)newpage;
					newpage->ptr = NULL;
				}
			}
			else{
				newentry->size = PAGESIZE - sizeof(kma_page_t*) - malloc_size;
				newentry->base = (unsigned int)newpage->ptr;
				newentry->next = NULL;
				//link up list correctly
				if(prev!=NULL){
					if(((kma_page_t*)prev)->size==PAGESIZE){
						((kma_page_t*)prev)->ptr = newpage;
					}
					else{
						prev->next = (resourceEntry*)newpage;
					}	
				}
				//set resource_map
				else{
					g_resource_map->ptr = newpage;
				}
			}
			return ptr;
		}
	}
	else // updating entry to reflect the remaining free space on the page
	{
		void* ptr = (void*)entry->base;
		resourceEntry * next = entry->next;
		int size = entry->size;
		int newsize = entry->size - malloc_size;
		// not enough room for a resource entry
		if (newsize < sizeof(resourceEntry)){
			//nothing before it on the list to link up
			if(prev==NULL){
				g_resource_map->ptr = next;
			}
			//link up previous entry to next entry
			else{
				//previous was a kma_page_t* so access appropriately
				if(((kma_page_t*)prev)->size==PAGESIZE){
					((kma_page_t*)prev)->ptr = next;
				}
				else{
					prev->next = next;
				}
				entry = NULL;
			}
		}
		else{
			// we have enough room, so update the values
			entry = (resourceEntry*)(entry->base + malloc_size);
			entry->base = (int)entry;
			entry->size = size - malloc_size;
			entry->next = next;
			if (prev==NULL){
				g_resource_map->ptr = entry;
			}
			else{
				if(((kma_page_t*)prev)->size==PAGESIZE){
					((kma_page_t*)prev)->ptr = entry;
				}
				else{
					prev->next = entry;
				}
			}
		}
		return ptr;
	}
}

//coalesce forward, adjacent resourceEntries to items on the same physical page
static bool coalesce(){
	bool result = FALSE;
	resourceEntry* current = (resourceEntry*)g_resource_map;
	kma_page_t* recentHead = NULL;
	resourceEntry* prev = NULL;
	while(current!=NULL){
		//is this a kma_page_t*? if so move on
		if(((kma_page_t*)current)->size==PAGESIZE){
			recentHead = (kma_page_t*)current;
			current = ((kma_page_t*)current)->ptr;
		}
		// is the block a fully free page that we should free?
		else if(current->size == (PAGESIZE - sizeof(kma_page_t*))){
			//are we freeing the resource map?
			if(recentHead==g_resource_map){
				g_resource_map = (kma_page_t*)current->next;
			}
			else{
				if(prev!=NULL){
					if(((kma_page_t*)prev)->size==PAGESIZE){
						((kma_page_t*)prev)->ptr = current->next;
					}
					else{
						prev->next = current->next;
					}
				}
			}
			current = current->next;
			free_page(recentHead);
		}
		//is this block directly adjacent to the next block? and on the same page?
		else if(current->next!=NULL &&
				!(((kma_page_t*)(current->next))->size==PAGESIZE) && 
				(current->base+current->size)==current->next->base &&
				BASEADDR(current)==BASEADDR(current->next)){
			result = TRUE;
			prev = current;
			current->size+=current->next->size;
			current->next=current->next->next;
			current = current->next;
		}
		else{
			prev = current;
			current = current->next;
		}
	}
	return result;
}

void
kma_free(void* ptr, kma_size_t size)
{
	while(coalesce()){};
	if(size<12){size = 12;}
	resourceEntry* newentry = ptr;
	newentry->base = (int)ptr;
	newentry->size = size;
	kma_page_t* thepage = BASEADDR(ptr);
	resourceEntry* entry = (resourceEntry*)g_resource_map;
	resourceEntry* prev = NULL;
	bool foundpage = FALSE;
	while(entry!=NULL){
		//haven't found page yet?
		if(foundpage == FALSE){
			prev = entry;
			//is this a kma_page_t*?
			if(((kma_page_t*)entry)->size==PAGESIZE){
				//is this the matching page?
				if(thepage==BASEADDR(((kma_page_t*)entry)->ptr)){
					//we found the right page, so link appropriately from here
					foundpage = TRUE;
				}
				entry = ((kma_page_t*)entry)->ptr;
			}
			//this is a resourceEntry
			else{
				entry = entry->next;
			}
		}
		//Found the correct page
		if(foundpage==TRUE) {
			//found the next kma_page_t*, so link prev to newentry to entry
			if(((kma_page_t*)entry)->size==PAGESIZE){
				//prev page cannot be null, so is it a page?
				if(((kma_page_t*)prev)->size==PAGESIZE){
					((kma_page_t*)prev)->ptr = newentry;
				}
				//prev is an entry
				else{
					prev->next = newentry;
				}
				newentry->next = entry;
				while(coalesce()){};
				return;
			}
			//is this entry's base larger than ptr? if so we link here
			else if(entry->base>newentry->base){
				if(((kma_page_t*)prev)->size==PAGESIZE){
					((kma_page_t*)prev)->ptr = newentry;
				}
				//prev is an entry
				else{
					prev->next = newentry;
				}
				newentry->next = entry;
				while(coalesce()){};
				return;
			}
			//not the right place to link, move on
			prev = entry;
			entry = entry->next;
		}
	}
}
		
		


// static void printResources(char* mystr){
// 	resourceEntry* entry = (resourceEntry*)g_resource_map;
// 	int counter = 0, size = 0;
// 	int pagecounter = 0;
// 	bool quit = FALSE;
// 	printf("\n----PRINTING ENTRIES----\n");
// 	int limit = 0;
// 	int common = 0;
// 	while(entry != NULL){
// 		if(((kma_page_t*)entry)->size==PAGESIZE){
// 			limit = (int)BASEADDR(((kma_page_t*)entry)->ptr) +PAGESIZE;
// 			common = (int)BASEADDR(((kma_page_t*)entry)->ptr);
// 			printf("PAGE: %d\tSIZE: %d\tBASE: %p\tID: %d\tMESSAGE: %s\n", pagecounter, ((kma_page_t*)entry)->size, ((kma_page_t*)entry)->ptr,((kma_page_t*)entry)->id, mystr);
// 			pagecounter++;
// 			entry=((kma_page_t*)entry)->ptr;
// 		}
// 		else{
// 			printf("NUMBER: %d\tSIZE: %d\tBASE: %p\tBASEADDR: %p\tBASE+SIZE:%p\tNEXT: %p\tMESSAGE: %s\n", counter, entry->size, (void*)entry->base,BASEADDR(entry->base),(void*)(entry->base+entry->size),entry->next, mystr);
// 			if (entry->size <= 0){quit = TRUE;}
// 			if((int)BASEADDR(entry->base)!=common){
// 				printf("adjacent entries: %p\t%p have different baseaddr: %p\n",(void*)entry->base,(void*)common,BASEADDR(entry->base));
// 				exit(EXIT_FAILURE);
// 			}
// 			if((entry->base+entry->size)>limit){
// 				printf("free space end:%p goes beyond limit of page:%p\n",(void*)(entry->base+entry->size),(void*)limit);
// 				exit(EXIT_FAILURE);
// 			}
// 			size+=entry->size;
// 			counter++;
// 			entry=entry->next;			
// 		}
// 	}
// 	printf("----PRINTED %d ENTRIES FOR %d BYTES OF FREE SPACE----\n\n", counter, size);
// 	if (quit){exit(EXIT_FAILURE);}
// 	return;
// }

#endif // KMA_RM
