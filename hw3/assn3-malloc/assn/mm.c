/*
 * This implementation replicates the implicit list implementation
 * provided in the textbook
 * "Computer Systems - A Programmer's Perspective"
 * Blocks are never coalesced or reused.
 * Realloc is implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Basilisk",
    /* First member's full name */
    "Divya Dadlani",
    /* First member's email address */
    "divya.dadlani@mail.utoronto.ca",
    /* Second member's full name (leave blank if none) */
    "Geetika Saksena",
    /* Second member's email address (leave blank if none) */
    "geetika.saksena@mail.utoronto.ca"
};

/*************************************************************************
 * Basic Constants and Macros
 * You are not required to use these macros but may find them helpful.
*************************************************************************/
#define WSIZE       sizeof(void *)            /* word size (8 bytes on 64-bit) */
#define DSIZE       (2 * WSIZE)            /* doubleword size (16 bytes on 64-bit) */
#define CHUNKSIZE   (1<<13)      /* initial heap size (bytes) */

#define MAX(x,y) ((x) > (y)?(x) :(y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)          (*(uintptr_t *)(p))
#define PUT(p,val)      (*(uintptr_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)     (GET(p) & ~(DSIZE - 1))
#define GET_ALLOC(p)    (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)        ((char *)(bp) - WSIZE)
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))


/* Given a free block pointer, compute address of next and previous blocks
 * of that fit in the free list
 */
#define NEXT_FREE_BLKP(fbp) (*(void **) fbp)

/** Number of positions in the free list.
 * 	The last position (14) will be used for any blocks larger
 * 	than our largest fit (i.e. 2^13 words)
 */
#define FREE_LIST_SIZE 15

/**
 * The largest fit in free_list[13], i.e. the largest planned segregated fit.
 */
#define LARGEST_FIT_SIZE 8192

/**
 * Pointer to the heap
 */
void* heap_listp = NULL;

// Number of words currently in the heap
int heap_length = CHUNKSIZE;


// Segregated free list
// each element of the free_list contains 2^i double-word sized blocks
// last element contains all blocks larger than 8192 words
void *free_lists[FREE_LIST_SIZE];
unsigned int num_free_blocks[FREE_LIST_SIZE];

int get_free_list_index(unsigned int num_words);

int mm_check(void);

void *remove_from_free_list(void *bp);

void add_to_free_list(void *bp);

void print_free_list(int free_list_index);
/**********************************************************
 * mm_init
 * Initialize the heap, including "allocation" of the
 * prologue and epilogue. At initialization, we create
 * one heap block of size INITIAL_HEAP_SIZE, which is
 * the block size held in the second last element of
 * free_lists.
 **********************************************************/
 int mm_init(void)
 {

	if ((heap_listp = mem_sbrk((heap_length+4)*WSIZE)) == (void *) -1)
		return -1;
	// printf("heap_listp: %p", heap_listp);
	//  ___________________________________________
	// |0x1|S|__________FREE BLOCK___________|S|0x1|
	//  W   W <------heap_length*WSIZE------> W   W

	// alignment & prologue
	PUT(heap_listp, 1);
	// header containing size of block
	PUT(heap_listp + WSIZE, PACK((heap_length * WSIZE + DSIZE), 0));
	// footer containing size of block
	PUT(heap_listp + (heap_length * WSIZE + DSIZE), PACK((heap_length * WSIZE + DSIZE),0));
	// alignment & epilogue
	PUT(heap_listp + ((heap_length * WSIZE + DSIZE) + WSIZE), 1);

	// heap_listp starts at payload of first (huge) block
	// heap_listp  is now a big free block
	heap_listp+=DSIZE;
	// include two words for header and footer
	heap_length+=2;
	int i;
	for (i = 0; i < FREE_LIST_SIZE; i++) {
		free_lists[i] = 0;
	}

	//printf("Heap initialized at %p with size %u\n", heap_listp, heap_length);
	add_to_free_list(heap_listp);
	return 0;
 }

/**********************************************************
 * coalesce
 * Covers the 4 cases discussed in the text:
 * - both neighbours are allocated
 * - the next block is available for coalescing
 * - the previous block is available for coalescing
 * - both neighbours are available for coalescing
 **********************************************************/
void *coalesce(void *bp)
{
	//printf("About to coalesce\n");
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // MACRO for PREV_BLKP does not work for this case
    if (bp == heap_listp) {
    	prev_alloc = 1;
    }
    // //printf("Coalesce: size1: %u\n",(unsigned int) size);
    void *coalesced_bp = NULL;

    if (prev_alloc && next_alloc) {       /* Case 1 */
    	coalesced_bp = bp;
    }

    else if (prev_alloc && !next_alloc) { /* Case 2 */
		//account for size 16 external fragmentation which isn't
		//in the free list
		assert (GET_SIZE(HDRP(NEXT_BLKP(bp))) != DSIZE);
		assert(remove_from_free_list(NEXT_BLKP(bp)));

        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        coalesced_bp = bp;
    }

    else if (!prev_alloc && next_alloc){
		//account for size 16 external fragmentation which isn't
		//in the free list
		assert(GET_SIZE(HDRP(PREV_BLKP(bp))) != DSIZE);
		void * if_free = remove_from_free_list(PREV_BLKP(bp));
		assert(if_free);

    	  /* Case 3 */
         size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		 PUT(FTRP(bp), PACK(size, 0));
		 PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		 coalesced_bp = PREV_BLKP(bp);
    }

	else { /* Case 4 */
		//account for size 16 external fragmentation which isn't
		//in the free list
		int size_prev = GET_SIZE(HDRP(PREV_BLKP(bp)));
		int size_next = GET_SIZE(FTRP(NEXT_BLKP(bp)));
		assert(size_prev != DSIZE);
		assert(remove_from_free_list(PREV_BLKP(bp)));
		assert(size_next != DSIZE);
		assert(remove_from_free_list(NEXT_BLKP(bp)));

		size += size_prev + size_next;
        // //printf("Coalesce: size_p: %u\n",(unsigned int) size_prev);
        // printf("Coalesce: size_n: %u\n",(unsigned int) size_next);
		PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size,0));
		coalesced_bp = PREV_BLKP(bp);
	}

	 //printf("Coalesced into size: %u, addr: %p\n",(unsigned int) size, (int*)coalesced_bp);
	return coalesced_bp;
}

/**********************************************************
 * extend_heap
 * Extend the heap by "words" words, maintaining alignment
 * requirements of course. Free the former epilogue block
 * and reallocate its new header
 **********************************************************/
void *extend_heap(size_t words)
{
     //printf("Entered extend heap\n");

    char *bp;
    // size in bytes
    size_t size;

    /* Allocate an even number of words to maintain alignments */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;


    /* Find the size of the last block in the heap (if free) */
    void* p = heap_listp + (heap_length * WSIZE - DSIZE);
    if (GET_ALLOC(p) == 0) {
//    	mm_check();
//    	print_free_list();
    	size -= GET_SIZE(p);
        //printf("Size of p = %lu, Size in bytes to sbrk: %lu\n",GET_SIZE(p),size);
    }

    if ( (bp = mem_sbrk(size)) == (void *)-1 )
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));                // free block header
    PUT(FTRP(bp), PACK(size, 0));                // free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));        // new epilogue header
    heap_length += (size >> 3);
     //printf("Exiting extend heap, new heap_length: %d\n", heap_length);
    /* Coalesce if the previous block was free */
    return coalesce(bp);

}


/**********************************************************
 * find_fit
 * Traverse the heap searching for a block to fit asize
 * Return NULL if no free blocks can handle that size
 * Assumed that asize is aligned
 **********************************************************/
void * find_fit(size_t asize)
{
	 //printf("Entered find fit.\n");
	// Divide by WSIZE (which is 8 bytes)
	unsigned int num_words_payload = (asize >> 3) - 2;
	int free_list_index = get_free_list_index(num_words_payload);
	// If this is -1, asize was 0.
	assert(free_list_index >= 0);

	// look in the largest block range to find a fit
	void *best_fit_blkp = NULL;
	void *cur_blkp;
	int best_fit = 999999999, cur_fit;

	while (free_list_index < FREE_LIST_SIZE) {
//		printf("In find fit\n");
		cur_blkp = free_lists[free_list_index];
		while (cur_blkp) {
			cur_fit = GET_SIZE(HDRP(cur_blkp)) - asize;
			if (cur_fit == 0) {
				// found the exact fit
				best_fit_blkp = cur_blkp;
				best_fit = 0;
				break;
			} else if (cur_fit > 0) {
				if ((best_fit_blkp == NULL) || (cur_fit < best_fit)) {
					// we don't have a previous best fit or this fit is better than our previous best fit
					best_fit_blkp = cur_blkp;
					best_fit = cur_fit;
				}
			}
			cur_blkp = NEXT_FREE_BLKP(cur_blkp);
		}
		// found a good fit, don't need to look in higher free list indices
		if (best_fit_blkp != NULL)
			break;

		free_list_index++;
	}
	// found a good fit. If not, will extend heap in malloc
	if (best_fit_blkp != NULL) {
		 //printf("Found best_fit blkp, leaving find fit.\n");
		remove_from_free_list(best_fit_blkp);
		return best_fit_blkp;
	}
	 //printf("Found nothing, leaving find fit.\n");
	return NULL;
}

/**********************************************************
 * place
 * Mark the block as allocated
 **********************************************************/
void place(void* bp, size_t asize)
{
//  /* Get the current block size */
//  size_t bsize = GET_SIZE(HDRP(bp));

  PUT(HDRP(bp), PACK(asize, 1));
  PUT(FTRP(bp), PACK(asize, 1));
  // remove next free block pointer
  PUT(bp, 0);
}

/**********************************************************
 * mm_free
 * Free the block and coalesce with neighbouring blocks
 **********************************************************/
void mm_free(void *bp)
{
	 //printf("Freeing block %p of size %lu\n",bp, GET_SIZE(HDRP(bp)));
    if(bp == NULL){
      return;
    }
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size,0));
    PUT(FTRP(bp), PACK(size,0));

    //printf("Old bp: %p\n",bp);
    void *new_bp = coalesce(bp);
    //printf("New bp: %p\n",new_bp);
    // size may be different after coalescing
    add_to_free_list(new_bp);
     //printf("Exiting free\n");
//    if(!mm_check())
//    	 printf("ERROR IN HEAP after free bp = %p size = %lu\n ", (int*) bp, GET_SIZE(HDRP(bp)));
    //}
}


/**********************************************************
 * mm_malloc
 * Allocate a block of size bytes.
 * The type of search is determined by find_fit
 * The decision of splitting the block, or not is determined
 *   in place(..)
 * If no block satisfies the request, the heap is extended
 **********************************************************/
void *mm_malloc(size_t size)
{
    size_t asize; /* adjusted block size in bytes */
    size_t extendsize; /* amount to extend heap if no fit */
    char * bp;

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1))/ DSIZE);

	bp = find_fit(asize);
	if (bp == NULL) {
		/* No fit found. Get more memory and place the block */
		extendsize = MAX(asize, CHUNKSIZE);
		if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
			return NULL;
	}
	int split_size = GET_SIZE(HDRP(bp)) - asize;

	assert(split_size >= 0);
	assert(!(split_size % DSIZE));

	if (split_size == DSIZE) {
	// cannot have a free block of size DSIZE, so add it to the allocated block
		split_size = 0;
		asize += DSIZE;
	}
	else if (split_size > 0) {
		void *split_bp = bp + asize;
		// set header and footer of free split block
		PUT(HDRP(split_bp), PACK(split_size, 0));
		PUT(FTRP(split_bp), PACK(split_size, 0));
		// insert into head of linked list at partition_array_pos
		add_to_free_list(split_bp);
	}

	// set header and footer of malloc'ed block
	place(bp, asize);
    return bp;
}

/**********************************************************
 * mm_realloc
 * Implemented simply in terms of mm_malloc and mm_free
 *********************************************************/
void *mm_realloc(void *ptr, size_t size)
{
//	print_free_list(0);
//	printf("Realloc size = %lu\n",size);
    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0){
      mm_free(ptr);
      return NULL;
    }
    /* If oldptr is NULL, then this is just malloc. */
    if (ptr == NULL) {
      return (mm_malloc(size));
    }

    void *oldptr = ptr;
    void *newptr;
    size_t old_size, new_size;

    new_size = DSIZE * ((size + (DSIZE) + (DSIZE-1))/ DSIZE);
    old_size = GET_SIZE(HDRP(oldptr));

    if (new_size == old_size) {
    	return oldptr;
    } else if (new_size < old_size) {
    	// can't store a DSIZE sized block so do nothing
    	if ((old_size - new_size) < DSIZE)
    		return oldptr;

    	// trim the block
    	place(oldptr, new_size);
    	void *new_free_ptr = oldptr + new_size;

    	PUT(HDRP(new_free_ptr), PACK ((old_size - new_size), 0));
    	PUT(FTRP(new_free_ptr), PACK ((old_size - new_size), 0));

    	add_to_free_list(new_free_ptr);
    	return oldptr;

    }

    void *next_blkptr = NEXT_BLKP(oldptr);
    	// need to malloc more. Coalesce with next block if free
	if (!GET_ALLOC(HDRP(next_blkptr))) {
		size_t combined_size = old_size + GET_SIZE(HDRP(NEXT_BLKP(oldptr)));
		if (combined_size >= new_size) {
			remove_from_free_list(next_blkptr);
			place(oldptr, combined_size);
			assert(GET_SIZE(HDRP(oldptr)) == combined_size);
			return oldptr;
		}
	}

//	printf("Mallocing\n");

	newptr = mm_malloc(size);
	if (newptr == NULL) {
		// printf("Exiting realloc out of mem\n");
		return NULL;
	}

	/* Copy the old data. */
	if (size < old_size)
		old_size = size;
	memcpy(newptr, oldptr, old_size);
	mm_free(oldptr);
	return newptr;


}

/**********************************************************
 * mm_check
 * Check the consistency of the memory heap
 * Return nonzero if the heap is consistent.
 *********************************************************/
int mm_check(void){

	int num_blk = 0;
	void * cur_blkp = heap_listp;

	// check if all free blocks are in the free list array
	while ((cur_blkp != NULL) && (*(int *)(HDRP(cur_blkp)) != 1)) {

		size_t size = GET_SIZE(HDRP(cur_blkp));
		size_t alloc = GET_ALLOC(HDRP(cur_blkp));
		if (size == 0)
			break;
		 printf("Block %d: Size = %u, Allocated = %u, Address = %p\n",num_blk, (unsigned int) size, (unsigned int) alloc, cur_blkp);
		num_blk++;
		void *ftr = cur_blkp + size - DSIZE;
		if (GET_SIZE(ftr) != size) {
			return 0;
		}
		if (alloc) {
			// add these checks later

		}
		else {
			assert(size > DSIZE);
				//payload of the free block
				unsigned int payload_size = (size - DSIZE) >> 3;
				int free_list_index = get_free_list_index(payload_size);
				void *head = free_lists[free_list_index];
				// int lc2 = 0;
				while (head != NULL) {
					if (head == cur_blkp)
						break;
					// address of next free block in linked list should
					// be located at head
					head = NEXT_FREE_BLKP(head);
				}

				// head was not found in the linked list at free_lists[i]
				if (head == NULL) {
					return 0;
				}
			}

		cur_blkp = NEXT_BLKP(cur_blkp);
	}
	// all blocks correctly accounted for
	return 1;
}

void print_free_list(int free_list_index) {
	int i;
	unsigned int fsize, falloc;
//	if (free_list_index > -1) {
//		printf("free_list[%d]: ", free_list_index);
//		void *cur = (void *) free_lists[free_list_index];
//		while ((cur != NULL) && (GET(cur+WSIZE) != 1)) {
//			//			printf("In print free list\n");
//			fsize = GET_SIZE(HDRP(cur));
//			// confirm that it is not allocated
//			falloc = GET_ALLOC(HDRP(cur));
//			PUT(cur + WSIZE, 1);
//			void *next = NEXT_FREE_BLKP(cur);
//			printf("[addr = %p, size = %u, next_addr = %p]", cur, fsize, next);
//			if (falloc)
//				printf(", ERROR: ALLOCATED!!");
//			printf("-->");
//			cur = NEXT_FREE_BLKP(cur);
//		}
//		printf("\n");
//	}
	for (i = 0; i < FREE_LIST_SIZE; i++) {
		printf("free_list[%d]: ",i);
		void *cur = (void *)free_lists[i];
		while(cur != NULL) {
//			printf("In print free list\n");
			fsize = GET_SIZE(HDRP(cur));
			// confirm that it is not allocated
			falloc = GET_ALLOC(HDRP(cur));
			void *next = NEXT_FREE_BLKP(cur);
			printf("[addr = %p, size = %u, next_addr = %p]", cur, fsize, next);
			if(falloc)
				printf(", ERROR: ALLOCATED!!");
			printf("-->");
			cur = NEXT_FREE_BLKP(cur);
		}
		printf("\n");
	}
}

int get_free_list_index(unsigned int num_words) {
	//If the size is greater than our largest fit, return the last index
	if(num_words > LARGEST_FIT_SIZE)
		return FREE_LIST_SIZE - 1;

	unsigned int num_set_bits =__builtin_popcount(num_words);

	if (num_set_bits > 1) {
		// not a power of 2. Return one position up
		return (sizeof(int) << 3) - __builtin_clz(num_words);
	} else if (num_set_bits == 1) {
		// is a power of 2. Return this position
		return (sizeof(int) << 3) - __builtin_clz(num_words)-1;
	} else {
		return -1;
	}
}

void *remove_from_free_list(void *bp) {
	unsigned int size = GET_SIZE(HDRP(bp));
	size_t payload_words = (size-DSIZE) >> 3;
	int free_list_index = get_free_list_index(payload_words);
	void *prev = NULL;
	void *cur = free_lists[free_list_index] ;
	while((cur != NULL) && (cur != bp)) {
		prev = cur;
		cur = NEXT_FREE_BLKP(cur);
	}

	if (cur == NULL) {
		return NULL;
	}

	if (prev != NULL) {
		//somewhere later in the list
		*(void **)prev = NEXT_FREE_BLKP(cur);
	} else {
		//remove from head
		free_lists[free_list_index] = NEXT_FREE_BLKP(cur);
	}

	return cur;
}

void add_to_free_list(void *bp) {
	assert(!GET_ALLOC(HDRP(bp)));
	size_t size = GET_SIZE(HDRP(bp));
	size_t payload_words = (size -DSIZE) >> 3;
	int index = get_free_list_index(payload_words);
	assert(free_lists[index]!=bp);
	PUT(bp, (uintptr_t) free_lists[index]);
	free_lists[index] = bp;
}
