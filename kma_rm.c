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
static unsigned int firstbase = 0;
#define PAGEBASE(id) ((void*)(firstbase+PAGESIZE*id))

typedef struct resourceHead {
	int base;
	int size;
	struct resourceHead * next;
} resourceEntry;

/************Global Variables*********************************************/
kma_page_t* g_resource_map = NULL;

/************Function Prototypes******************************************/
static bool coalesce();
static void printResources(char *);
int mspillover = 0;
int fspillover = 0;
int prcount = -999999990;

/************External Declaration*****************************************/

/**************Implementation***********************************************/


void* kma_malloc(kma_size_t malloc_size){
	if(prcount>=44800){printf("Malloc:%d line:%d\n",(int)malloc_size,__LINE__);fflush(stdout);}
	if (g_resource_map == NULL)
	{
		if(prcount>=44800){printf("empty rmap Malloc:%d line:%d\n",(int)malloc_size,__LINE__); fflush(stdout);}
		// if the request is larger than the size of a page we can't allocate it
		if (malloc_size >= (PAGESIZE - sizeof(kma_page_t*))){return NULL;}
		kma_page_t* first = get_page();
		if(first->ptr != BASEADDR(first->ptr)){printf("Bad ptr from get_page line:%d  ptr:%p\tfixed:%p\tba:%p\n",__LINE__,first->ptr,PAGEBASE(first->id),BASEADDR(first->ptr)); fflush(stdout); first->ptr = BASEADDR(first->id);}
		if(firstbase==0){firstbase = (unsigned int)first->ptr;}
		else if(first->ptr !=PAGEBASE(first->id)){first->ptr =PAGEBASE(first->id);}
		// printf("firstbase:%p\n",(void*)firstbase);
// 		printf("pagebase:%p\n",PAGEBASE(0));
		void* allocated = (void*)((unsigned int)first->ptr + sizeof(kma_page_t*));
		//print("first:%p\n",first->ptr);
		g_resource_map = first;
		g_resource_map->ptr =((unsigned int)first->ptr + sizeof(kma_page_t*) + malloc_size);
		resourceEntry* newEntry = g_resource_map->ptr;
		//printf("Resource_map_loc: %d\n", (int)g_resource_map);
		newEntry->size = PAGESIZE - sizeof(kma_page_t*) - malloc_size;
		newEntry->base = (int)g_resource_map->ptr;
		newEntry->next = NULL;
		//print("return %p, ptr:%p  new:%p  size:%d   base:%p  next:%p\n",allocated,g_resource_map->ptr,newEntry,newEntry->size,newEntry->base,newEntry->next);
		//printf("first->ptr: %p first:%p firstbase:%p  first->ptrbase:%p\n", first->ptr, first, BASEADDR(first), BASEADDR(first->ptr));
		//printf("first: %ld firstPage: %ld Base: %d\n", (long)first->ptr, (long)BASEADDR(first->ptr), (int)g_resource_map->base);
		printResources("Empty Resource Map");
		return allocated;
	}
	kma_page_t* map = g_resource_map;
	resourceEntry* entry = g_resource_map;
	//print("entrysize:%d\n",((resourceEntry*)(g_resource_map->ptr))->size);
	//int sizee = entry->size;
	//printf("entry:%p  size:%d  castsize:%d\n",entry, ((resourceEntry*)g_resource_map->ptr)->size, ((resourceEntry*)entry)->size);
	// void* tstr= NULL;
// 	int tstt = 0;
	//printf("msize==psize:%d  esize==psize:%d  void*:%d   int:%d \n",map->size==PAGESIZE, entry->size==PAGESIZE,sizeof(tstr),sizeof(tstt));
	resourceEntry* prev = NULL;
	while(entry!=NULL){
		//print("map:%p  map->ptr:%p   mapsize:%d   entry:%p size:%d  base:%p  next:%p\n",map,map->ptr,map->size,entry,entry->size,(void*)entry->base,entry->next);
		//is this a kma_page_t*
		if(((kma_page_t*)entry)->size==PAGESIZE){
			//print("line:%d  prev:%p  entry:%p   ptr:%p\n",__LINE__,prev,entry,(resourceEntry*)(((kma_page_t*)entry)->ptr));
			prev = entry;
			entry=((kma_page_t*)entry)->ptr;
		}
		//entry is not a kma_page_t*, so check for size of hole
		else if(entry->size<PAGESIZE){
			if (malloc_size < 12){
				mspillover+=12-malloc_size;
				malloc_size = 12;
				
			}
			//found big enough hole
			//print("Searching for hole of size %d, at %p\n", malloc_size, (void*)entry->base);
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
		//printf("Didn't find a hole for %d, coalescing\n", malloc_size);
		while(coalesce()){
			coalesced = TRUE;
		}
		if (coalesced){
			//printf("Coalesced while searching for hole of size %d\n", malloc_size);
			return kma_malloc(malloc_size);
		}
		//still can’t place it. create new page
		else{
			//printf("Can't find a hole even after coalescing, making a new page\n");
			// if the request is larger than the size of a page we can't allocate it
			if (malloc_size >= (PAGESIZE - sizeof(kma_page_t*))){return NULL;}
			kma_page_t* newpage = get_page();
			//need to correct the ptr, in case it returned a bad ptr.
			//printf("Bad ptr from get_page line:%d  ptr:%p\tfixed:%p\tba:%p\n",__LINE__,newpage->ptr,PAGEBASE(newpage->id),BASEADDR(newpage->ptr)); fflush(stdout); 
			if(newpage->ptr != BASEADDR(newpage->ptr)){ newpage->ptr = BASEADDR(newpage->ptr); }
			void* ptr = newpage->ptr + sizeof(kma_page_t*);
			//print("original ptr: %p\tadj pointer: %p\tresourceEntry: %p\n",newpage->ptr,ptr,(void*)((unsigned int)newpage->ptr + sizeof(kma_page_t*) + malloc_size));
			newpage->ptr = (resourceEntry*)((unsigned int)newpage->ptr + sizeof(kma_page_t*) + malloc_size);
			//print("newpage->ptr: %p newpage:%p newpagebase:%p  newpage->ptrbase:%p\n",  newpage->ptr, newpage,BASEADDR(newpage), BASEADDR(newpage->ptr));
			//printf("Made a new page at %p\n", newpage->ptr);
			resourceEntry* newentry = newpage->ptr;
			//resourceEntry* newentry = (resourceEntry*)(newpage + sizeof(kma_page_t*));
			//printf("newpage->ptr: %p newpage:%p newpagebase:%p  newpage->ptrbase:%p  newentry:%p  newentryadj:%p\n",  newpage->ptr, newpage,BASEADDR(newpage), BASEADDR(newpage->ptr), newentry,(kma_page_t*)(newentry-sizeof(kma_page_t*)));
			
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
					prev->next = newpage;
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
						prev->next = newpage;
					}	
				}
				//set resource_map
				else{
					g_resource_map->ptr = newpage;
				}
			}
			//printResources("No hole fit");
			if(prcount>=44800){printf("No fit  Malloc:%d line:%d\n",(int)malloc_size,__LINE__); fflush(stdout);}
			return ptr;
		}
	}
	else // updating entry to reflect the remaining free space on the page
	{
		//printResources("Start of hole-filling");
		//printf("Found a hole: (size: %d, base: %d)\n", entry->size, entry->base);
		void* ptr = (void*)entry->base;
		resourceEntry * next = entry->next;
		int size = entry->size;
		int newsize = entry->size - malloc_size;
		//print("size:%d  next:%p  prev:%p\n",size,next,prev);
		// not enough room for a resource entry
		if (newsize < sizeof(resourceEntry)){
			//printf("IT CAME IN HERE: %d\tSIZE: %d\n", __LINE__, g_resource_map->size);
			//nothing before it on the list to link up
			if(prev==NULL){
				//printf("IT CAME IN HERE: %d\tSIZE: %d\n", __LINE__, g_resource_map->size);
				g_resource_map->ptr = next;
				//printf("%d: size now %d\n", __LINE__, g_resource_map->size);
			}
			//link up previous entry to next entry
			else{
				//printf("IT CAME IN HERE: %d\n", __LINE__);
				//printf("prev: %d\tprev->next:%d\tentry: %d\tentry->next: %d\tsize: %d\n", prev->base, (int)prev->next, (int)entry->base, (int)entry->next, entry->size);
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
				//printf("IT CAME IN HERE: %d\tSIZE: %d\n", __LINE__, g_resource_map->size);
			}
			else{
				if(((kma_page_t*)prev)->size==PAGESIZE){
					((kma_page_t*)prev)->ptr = entry;
				}
				else{
					prev->next = entry;
				}
				//printf("IT CAME IN HERE: %d\n", __LINE__);
			}
		}
		
		char str[80];
		if(prcount>44800){printf("Found a hole at %p\n", ptr); fflush(stdout);}
		sprintf(str, "Found a hole at %p", ptr);
		//printResources(str);
		return ptr;
	}
}

//coalesce forward, adjacent resourceEntries to items on the same physical page
static bool coalesce(){
	bool result = FALSE;
	resourceEntry* current = g_resource_map;
	kma_page_t* recentHead = NULL;
	resourceEntry* prev = NULL;
	while(current!=NULL){
		//is this a kma_page_t*? if so move on
		if(((kma_page_t*)current)->size==PAGESIZE){
			recentHead = current;
			current = ((kma_page_t*)current)->ptr;
		}
		// is the block a fully free page that we should free?
		else if(current->size == (PAGESIZE - sizeof(kma_page_t*))){
			//are we freeing the resource map?
			if(recentHead==g_resource_map){
				g_resource_map = current->next;
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
			if(prcount>44800){printf("About to free a Page!!!!!\n"); fflush(stdout);}
			free_page(recentHead);
			if(prcount>44800){printf("Freed a Page!!!!!\n"); fflush(stdout);}
		}
		//is this block directly adjacent to the next block? and on the same page?
		//printf("Coalesce Attempt: current: %d\tnext: %d\tcurrentPage: %d\tnextPage: %d\n", current->base, current->next->base, (int)BASEADDR(current), (int)BASEADDR(current->next));
		else if(current->next!=NULL &&
				!(((kma_page_t*)(current->next))->size==PAGESIZE) && 
				(current->base+current->size)==current->next->base &&
				BASEADDR(current)==BASEADDR(current->next)){
			//print("Did a coalesce at %p\n", (void*)current->base);
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
	if(size<12){ fspillover+=12-size; size = 12;}
	if(prcount>=44800){printf("Free: ptr:%p  size:%d   line:%d\n",ptr,(int)size,__LINE__); fflush(stdout);}
	resourceEntry* newentry = ptr;
	newentry->base = (int)ptr;
	newentry->size = size;
	kma_page_t* thepage = BASEADDR(ptr);
	resourceEntry* entry = g_resource_map;
	resourceEntry* prev = NULL;
	bool foundpage = FALSE;
	while(entry!=NULL){
		//haven't found page yet?
		if(foundpage == FALSE){
			prev = entry;
			//is this a kma_page_t*?
			if(((kma_page_t*)entry)->size==PAGESIZE){
				//is this the matching page?
				//if(thepage==PAGEBASE(((kma_page_t*)entry)->id)){
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
				printResources("Freed");
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
				printResources("Freed");
				return;
			}
			//not the right place to link, move on
			prev = entry;
			entry = entry->next;
		}
	}
}
		
		


static void printResources(char* mystr){
	if(prcount<44800){prcount++; return;}
	resourceEntry* entry = g_resource_map;
	int counter = 0, size = 0;
	int pagecounter = 0;
	bool quit = FALSE;
	printf("\n----PRINTING ENTRIES----\n");
	int limit = 0;
	int common = 0;
	while(entry != NULL){
		if(((kma_page_t*)entry)->size==PAGESIZE){
			limit = BASEADDR(((kma_page_t*)entry)->ptr) +PAGESIZE;
			//common = PAGEBASE(((kma_page_t*)entry)->id);
			common = BASEADDR(((kma_page_t*)entry)->ptr);
			printf("PAGE: %d\tSIZE: %d\tBASE: %p\tPAGEBASE: %p\tID: %d\tMESSAGE: %s\n", pagecounter, ((kma_page_t*)entry)->size, ((kma_page_t*)entry)->ptr,PAGEBASE(((kma_page_t*)entry)->id),((kma_page_t*)entry)->id, mystr);
			pagecounter++;
			entry=((kma_page_t*)entry)->ptr;
		}
		else{
			printf("NUMBER: %d\tSIZE: %d\tBASE: %p\tBASEADDR: %p\tBASE+SIZE:%p\tNEXT: %p\tMESSAGE: %s\n", counter, entry->size, (void*)entry->base,BASEADDR(entry->base),(void*)(entry->base+entry->size),entry->next, mystr);
			if (entry->size <= 0){quit = TRUE;}
			if(BASEADDR(entry->base)!=common){
				printf("adjacent entries: %p\t%p have different baseaddr: %p\n",(void*)entry->base,common,BASEADDR(entry->base));
				exit(EXIT_FAILURE);
			}
			if((entry->base+entry->size)>limit){
				printf("free space end:%p goes beyond limit of page:%p\n",(void*)(entry->base+entry->size),(void*)limit);
				exit(EXIT_FAILURE);
			}
			size+=entry->size;
			counter++;
			entry=entry->next;
			//printf("l:%d\tr:%d\tresult:%d\n",entry->base+entry->size,limit,(entry->base+entry->size)>limit);
			
		}
	}
	printf("----PRINTED %d ENTRIES FOR %d BYTES OF FREE SPACE----\n\n", counter, size);
	printf("mspillover: %d\tfspillover: %d\n",mspillover,fspillover);
	if (quit){exit(EXIT_FAILURE);}
	return;
}

#endif // KMA_RM
