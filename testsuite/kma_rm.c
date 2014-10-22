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
	bool free;
} resourceEntry;

/************Global Variables*********************************************/
static resourceEntry* g_resource_map = NULL;


/************Function Prototypes******************************************/
resourceEntry* makeBKPage(size_t);
resourceEntry* makeDataPage(resourceEntry*, size_t);
resourceEntry* makeFreeEntry(resourceEntry*,resourceEntry*,size_t);
resourceEntry* createResourceEntry(resourceEntry*);

/************External Declaration*****************************************/

/**************Implementation***********************************************/

resourceEntry * makeBKPage(size_t malloc_size)
{
	kma_page_t * page = get_page();
	// check malloc size not too large (if it is free the page and return null)
	if (malloc_size > page->size - sizeof(kma_page_t*)){
		free(page);
		return NULL;
	}
	resourceEntry * bkpg = (resourceEntry*)(page + sizeof(kma_page_t *));
	bkpg->base = (int)page;
	bkpg->free = FALSE;
	// account for size of page ptr struct and resourceEntry
	bkpg->size = page->size - sizeof(kma_page_t*) - sizeof(resourceEntry*); 
	return bkpg;
}

resourceEntry* makeDataPage(resourceEntry* bkpg, size_t malloc_size){
	resourceEntry* datapg = createResourceEntry(bkpg);
	bkpg->size -= sizeof(resourceEntry*);
	kma_page_t * dpage = get_page();
	datapg->base = (int)dpage;
	datapg->size = dpage->size;
	datapg->free=TRUE;
	bkpg->next = datapg;
	makeFreeEntry(bkpg,datapg,malloc_size);
	return datapg;
}

resourceEntry* makeFreeEntry(resourceEntry* bkpg, resourceEntry* datapg, size_t malloc_size){
	resourceEntry* freeEntry = createResourceEntry(bkpg);
	datapg->next = freeEntry;
	datapg->free = FALSE;
	freeEntry->base = datapg->base+malloc_size;
	freeEntry->size = datapg->size-malloc_size-sizeof(kma_page_t*);
	freeEntry->free = TRUE;
	freeEntry->next = NULL;
	datapg->size = malloc_size;
	bkpg->size -= sizeof(resourceEntry*);
	return freeEntry;
}

resourceEntry* createResourceEntry(resourceEntry * bkpg)
{
	//bkpg is the book keeping entryResource
	//its size represents how much space is left in the book keeping page
	resourceEntry* me = bkpg;
	resourceEntry* result = (resourceEntry*)me->base;
	while (me != NULL){
		result = (resourceEntry*)me->base;
		me = me->next;
	}
	return result + sizeof(resourceEntry);
}

void* kma_malloc(kma_size_t malloc_size){
	if (g_resource_map == NULL)
	{
		resourceEntry* bkpg = makeBKPage(malloc_size);
		resourceEntry* datapg = makeDataPage(bkpg,malloc_size);
		g_resource_map = bkpg;
		return (void*)((int)datapg->base+(int)sizeof(kma_page_t*));
	}

	//search for matching(free==TRUE && malloc_size <= entry->size) resourceEntry
	resourceEntry* entry = g_resource_map;
	resourceEntry* prev = NULL;
	while(entry!=NULL){
		//found big enough hole
		if(entry->free==TRUE && malloc_size <= entry->size){
			break;
		}
		prev = entry;
		entry=entry->next;
	}

	//didn’t find a match, switch on status of book keeping and data pages
	//no hole big enough
	if(entry==NULL)
	{
		bool coalesced = FALSE;
		//attempt to merge holes, if any were merged, coalesced==TRUE
// 		while(coalesce(g_resource_map)){
// 			coalesced = TRUE;
// 		}
		if (coalesced){
			return kma_malloc(malloc_size);
		}
		//still can’t place it. create new data page
		else{
			//get the entry that manages the book keeping page
			resourceEntry* bkHead = ((resourceEntry *)BASEADDR(prev))+ sizeof(kma_page_t *); 
			//we can use this book keeping page again
			if(bkHead->size >= (2*sizeof(resourceEntry))){
				//make our new resource entry and allocate to the new datapage
				resourceEntry * datapage = makeDataPage(bkHead,malloc_size);
				return (void*)((int)datapage->base+sizeof(kma_page_t*));
			}
			else //we need to make a new book keeping page
			{
				//make a new resourceEntry to manage this page
				resourceEntry* bkpage = makeBKPage(malloc_size);
				resourceEntry * datapage = makeDataPage(bkpage,malloc_size);
				return (void*)((int)datapage->base+(int)sizeof(kma_page_t*));
			}
		}
	}
	else // updating entry to reflect requested size; split off new free entry
	{
		resourceEntry* bkHead = ((resourceEntry *)BASEADDR(entry))+ sizeof(kma_page_t *); 
		//room on bkpage for new resourceEntry
		if(bkHead->size >= sizeof(resourceEntry)){
			//make our new resource entry and allocate to the new datapage
			makeFreeEntry(bkHead, entry, malloc_size);
			return (void*)((int)entry->base+sizeof(kma_page_t*));
		}
		//bkpage full, need new bkpage
		else{
			resourceEntry* bkpage = makeBKPage(malloc_size);
			makeFreeEntry(bkpage,entry,malloc_size);
			return (void*)((int)entry->base+sizeof(kma_page_t*));
		}
	}
}
void
kma_free(void* ptr, kma_size_t size)
{
  ;
}

#endif // KMA_RM
