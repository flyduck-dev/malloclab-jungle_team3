#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

team_t team = {
    "fly-Walker",
    "flyduck-dev🕊",
    "fantasy coding",
    "",
    ""
};

#define ALIGNMENT 8
#define WSIZE 4
#define DSIZE 8
#define INITSIZE          16
#define MINBLOCKSIZE      16  
#define CHUNKSIZE (1 << 12)

#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define MAX(x, y) ((x) > (y) ? (x):(y))
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp))-DSIZE)

#define NEXT_BLKP(bp) ((void *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((void *)(bp) - GET_SIZE(HDRP(bp) - WSIZE))
#define NEXT_FREE_BLKP(bp)(*(void **)(bp))
#define PREV_FREE_BLKP(bp)(*(void **)(bp + WSIZE))

static void* heap_listp;
static void* free_listp;
#define LISTLIMIT 20
static void *segregation_list[LISTLIMIT];

// Prototype
int mm_init(void);
static void *extend_heap(size_t words);
static void *find_fit(size_t size);
static void *coalesce(void *bp);
static void place(void *bp, size_t asize);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);
void remove_from_freelist(void *bp);
void add_free_block(void *bp, size_t size);

// mm_init - initialize the malloc package.

int mm_init(void)
{
    for (int i = 0; i < LISTLIMIT; i++){
        segregation_list[i] = NULL;
    }

    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));
    
    heap_listp += (2 * WSIZE);
    
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

static void *extend_heap(size_t words){
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : (words)*WSIZE;
    if((long) (bp = mem_sbrk(size)) == -1)
        return NULL;
    
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* epilogue header */

    return coalesce(bp);
}

static void *find_fit(size_t size){
    void *bp; 
    /* IMPLICIT */
    // for (bp = free_listp; GET_SIZE(HDRP(bp)) > 0; bp= NEXT_BLKP(bp)){
    //     if (!GET_ALLOC(HDRP(bp))&& (size <= GET_SIZE(HDRP(bp)))){
    //         return bp;
    //     }
    // }
    /* LIFO & SBA */
    // for (bp = free_listp; GET_ALLOC(HDRP(bp)) != 1; bp = NEXT_FREE_BLKP(bp))
    // {
    //     if (size <= GET_SIZE(HDRP(bp)))
    //     {
    //         return bp;
    //     }
    // }
    // return NULL;

    int list = 0;
    size_t searchsize = size;

    while (list < LISTLIMIT){
        if ((list == LISTLIMIT - 1) || (searchsize <= 1) && (segregation_list[list] != NULL)){
            bp = segregation_list[list];
            while((bp != NULL) && (size > GET_SIZE(HDRP(bp)))){
                bp = NEXT_FREE_BLKP(bp);
            }
            if (bp != NULL) return bp;
        }
        searchsize >>= 1;
        list++;
    }
    return NULL;
}

static void place(void *bp, size_t asize){
    size_t csize = GET_SIZE(HDRP(bp));
    remove_from_freelist(bp);
    if ((csize - asize) >= (2*DSIZE)){ 
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
        add_free_block(bp, csize - asize);
    }
    else
    {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0) return NULL;
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1))/DSIZE);
    
    if ((bp = find_fit(asize)) != NULL){
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE))== NULL)return NULL;
    place(bp, asize);
    return bp;
}

void add_free_block(void *bp, size_t size){
    /* LIFO */
    // NEXT_FREE_BLKP(bp) = free_listp;
    // PREV_FREE_BLKP(bp) = NULL;
    // PREV_FREE_BLKP(free_listp) = bp;
    // free_listp = bp;

    int list = 0;
    void *search_ptr;
    void *insert_ptr = NULL;

    //먼저 블록의 크기를 기반으로 리스트 번호(list)를 결정합니다. 
    while((list < LISTLIMIT-1) && (size >1)){
        size >>= 1;
        list++;
    }

    //새로운 블록이 리스트에 추가될 때는 LIFO 방식으로 추가됩니다
    //이전에 추가된 블록은 리스트의 맨 앞에 위치하고,
    //새로운 블록은 이전에 추가된 블록 앞에 위치합니다.
    //새로운 블록의 NEXT_FREE_BLKP 포인터는 이전에 추가된 블록을 가리키고
    //이전에 추가된 블록의 PREV_FREE_BLKP 포인터는 새로운 블록을 가리키게 됩니다.

    search_ptr = segregation_list[list];

    while((search_ptr != NULL) && size > GET_SIZE(HDRP(search_ptr))){
        insert_ptr = search_ptr;
        search_ptr = NEXT_FREE_BLKP(search_ptr);
    }
    if (search_ptr != NULL){
        if(insert_ptr != NULL){
            NEXT_FREE_BLKP(bp) = search_ptr;
            PREV_FREE_BLKP(bp) = insert_ptr;
            PREV_FREE_BLKP(search_ptr) = bp;
            NEXT_FREE_BLKP(insert_ptr) = bp;
        }else{
            NEXT_FREE_BLKP(bp) = search_ptr;
            PREV_FREE_BLKP(bp) = NULL;
            PREV_FREE_BLKP(search_ptr) = bp;
            segregation_list[list] = bp;
        }
    }else{
        if(insert_ptr != NULL){
            NEXT_FREE_BLKP(bp) = NULL;
            PREV_FREE_BLKP(bp) = insert_ptr;
            NEXT_FREE_BLKP(insert_ptr) = bp;
        }else{
            NEXT_FREE_BLKP(bp) = NULL;
            PREV_FREE_BLKP(bp) = NULL;
            segregation_list[list] = bp;
        }
    }
    return;
}

void remove_from_freelist(void *bp){
    int list = 0;
    size_t size = GET_SIZE(HDRP(bp));

    while((list < LISTLIMIT -1) && (size > 1)){
        size >>= 1;
        list++;
    }

    if (NEXT_FREE_BLKP(bp) != NULL){ //현재 free block의 다음에 할당되지 않은 메모리 블록이 있다면
        if(PREV_FREE_BLKP(bp) != NULL){ //현재 free block의 이전에 할당되지 않은 메모리 블록이 있다면
            PREV_FREE_BLKP(NEXT_FREE_BLKP(bp)) = PREV_FREE_BLKP(bp);
            NEXT_FREE_BLKP(PREV_FREE_BLKP(bp)) = NEXT_FREE_BLKP(bp);
        }else{ //현재 free block의 이전에 할당되지 않은 메모리 블록이 없다면
            PREV_FREE_BLKP(NEXT_FREE_BLKP(bp)) = NULL;
            segregation_list[list] = NEXT_FREE_BLKP(bp);
        }
    }else{ //현재 free block의 다음에 할당되지 않은 메모리 블록이 없다면
        if(PREV_FREE_BLKP(bp) != NULL){ //현재 free block의 이전에 할당되지 않은 메모리 블록이 있다면
            NEXT_FREE_BLKP(PREV_FREE_BLKP(bp)) = NULL;
        }else{ //현재 free block의 이전에 할당되지 않은 메모리 블록이 없다면
            segregation_list[list] = NULL;
        }
    }
    return;
}

void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) || PREV_BLKP(bp) == bp;
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if(prev_alloc && next_alloc){
        add_free_block(bp, size);
        return bp;
    }else if(prev_alloc && !next_alloc){
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_from_freelist(NEXT_BLKP(bp)); // 다음 블록 제거
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }else if(!prev_alloc && next_alloc){
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        remove_from_freelist(PREV_BLKP(bp)); // 이전 블록 제거
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }else{
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        remove_from_freelist(PREV_BLKP(bp)); // 이전 블록 제거
        remove_from_freelist(NEXT_BLKP(bp)); // 다음 블록 제거
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    add_free_block(bp, size);
    return bp;
}

void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL){
        return NULL;
    }
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize){
        copySize = size;
    }
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}