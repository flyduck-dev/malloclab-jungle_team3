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

// CONSTANTS
#define ALIGNMENT         8         // memory alignment factor
#define WSIZE             4         // Size in bytes of a single word 
#define DSIZE             8         // Size in bytes of a double word
#define INITSIZE          16        // Initial size of free list before first free block added
#define MINBLOCKSIZE      16        /* Minmum size for a free block, includes 4 bytes for header/footer
                                       and space within the payload for two pointers to the prev and next
                                       free blocks */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p)        (*(size_t *)(p))
#define PUT(p, val)   (*(size_t *)(p) = (val))
#define GET_SIZE(p)  (GET(p) & ~0x1)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp)     ((void *)(bp) - WSIZE)
#define FTRP(bp)     ((void *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((void *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((void *)(bp) - GET_SIZE(HDRP(bp) - WSIZE))
#define NEXT_FREE_BLKP(bp)(*(void **)(bp))
#define PREV_FREE_BLKP(bp)(*(void **)(bp + WSIZE))

// PROTOTYPES
static void *extend_heap(size_t words);
static void *find_fit(size_t size);
static void *coalesce(void *bp);
static void place(void *bp, size_t asize);
static void remove_from_list(void *bp);

static char *heap_listp = 0;  /* Points to the start of the heap */
static char *free_listp = 0;  /* Poitns to the frist free block */

int mm_init(void)
{
    // (32 bytes total)
    if ((heap_listp = mem_sbrk(INITSIZE+MINBLOCKSIZE)) == (void *)-1){
        return -1;
    }

    PUT(heap_listp, PACK(MINBLOCKSIZE, 1)); //PROLOGUE_HEADER
    PUT(heap_listp + (1*WSIZE), PACK(MINBLOCKSIZE, 0)); //FREE_HEADER

    PUT(heap_listp + (2*WSIZE), PACK(0, 0)); //NEXT_FREE_BLKP
    PUT(heap_listp + (3*WSIZE), PACK(0, 0)); //PREV_FREE_BLKP

    PUT(heap_listp + (4*WSIZE), PACK(MINBLOCKSIZE, 0)); //FREE_FOOTER
    PUT(heap_listp + (5*WSIZE), PACK(0, 1)); //에필로그 헤더 유지
    free_listp = heap_listp +WSIZE;
    return 0;
}

void *mm_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    size_t asize; /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;
    
    asize = MAX(ALIGN(size) + DSIZE, MINBLOCKSIZE);

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, MINBLOCKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {
        return NULL;
    }
    place(bp, asize);
    return bp;
}

void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    coalesce(bp);
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
    //copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    copySize = GET_SIZE(HDRP(oldptr));
    //copySize = GET_SIZE(HDRP(oldptr))-2 *WSIZE - SIZE_T_SIZE;
    if (size < copySize){
        copySize = size;
    }
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1){
        return NULL;
    }

    PUT(HDRP(bp), PACK(size, 0));         /* Free block header */
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

    return coalesce(bp);
}

static void *find_fit(size_t size)
{
  void *bp;

  for (bp = free_listp; GET_ALLOC(HDRP(bp)) == 0; bp = NEXT_FREE_BLKP(bp)) {
    if (!GET_ALLOC(HDRP(bp)) && (size <= GET_SIZE(HDRP(bp))))
      return bp; 
  }
  return NULL; 
}


//* remove_freeblock - Removes the given free block pointed to by bp from the free list.
static void remove_from_list(void *removed)
{
        //제거할 때 연결리스트 작업...
    if(removed == heap_listp) return;
    //removed->llink->rlink = removed->rlink;
    if (PREV_FREE_BLKP(removed)){
        NEXT_FREE_BLKP(PREV_FREE_BLKP(removed)) = NEXT_FREE_BLKP(removed);
    } else {
        free_listp = NEXT_FREE_BLKP(removed);
    }
    //removed->rlink->llink = removed->llink;
    if (NEXT_FREE_BLKP(removed)){
        PREV_FREE_BLKP(NEXT_FREE_BLKP(removed)) = PREV_FREE_BLKP(removed);
    }
}


static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))) || PREV_BLKP(bp) == bp;
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    // if (prev_alloc && next_alloc) {  // case 1
    //     return bp;
    // } //이거 넣으면 오류 뜸
    if (prev_alloc && !next_alloc) {  // case 2
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_from_list(NEXT_BLKP(bp)); // 다음 블록 제거
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {  // case 3
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        remove_from_list(PREV_BLKP(bp)); // 이전 블록 제거
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else if (!prev_alloc && !next_alloc) { // case 4
    size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        remove_from_list(PREV_BLKP(bp)); // 이전 블록 제거
        remove_from_list(NEXT_BLKP(bp)); // 다음 블록 제거
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    // "coalesced block"을 "free list"의 맨 앞에 삽입
    NEXT_FREE_BLKP(bp) = free_listp; //새로운 노드의 NEXT_FREE_BLKP 필드를 현재 free list의 첫 번째 노드의 주소로 설정
    PREV_FREE_BLKP(free_listp) = bp; //현재 free list의 첫 번째 노드의 PREV_FREE 필드를 새로운 노드의 주소로 설정
    PREV_FREE_BLKP(bp) = NULL; //최상단
    free_listp = bp; //전역 변수 free_listp를 새로운 노드의 주소로 설정하여, free list의 첫 번째 노드를 업데이트
    return bp;
}

static void place(void *bp, size_t asize) {
    // Gets the total size of the free block 
    size_t csize = GET_SIZE(HDRP(bp));

    // Case 1: Splitting is performed 
    if ((csize - asize) >= (MINBLOCKSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        remove_from_list(bp);
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
        coalesce(bp);
    } 
    else { // Case 2: Splitting not possible. Use the full free block 
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        remove_from_list(bp);
    }
}
