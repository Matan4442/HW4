//
// Created by Matan Tennenhaus on 6/18/2021.
//
#include <iostream>
#include <cmath>
#include <unistd.h>
#include <cstring>
#include <sys/mman.h>

typedef struct MallocMetaData {
    size_t size;
    bool  is_free;
    MallocMetaData* next;
    MallocMetaData* prev;
    bool is_mmap;
    MallocMetaData* prev_arr;
    MallocMetaData* next_arr;
    size_t alignment;
} MallocMetaData;

MallocMetaData* g_listHead = NULL;
MallocMetaData* g_listTail = NULL;
MallocMetaData* g_mmaplistHead = NULL;
MallocMetaData* g_mmaplistTail = NULL;

#define ALIGNMENT (8)
#define SIZE_OF_MALLOC(size) ((size)+sizeof(MallocMetaData)+ALIGNMENT)
#define RETURN_TO_USER(curr) (void*)((size_t)(curr)+sizeof(MallocMetaData))
#define P_METADATA(p)  (MallocMetaData*)((size_t)(p)-sizeof(MallocMetaData))
#define MIN_BIN_SIZE (128)
#define IS_BLOCK_SPLITTABLE(block_size ,size) \
        ((block_size) > ((size)+sizeof(MallocMetaData) + SIZE_OF_MALLOC(MIN_BIN_SIZE)))
#define KB(x)  ((size_t) (x) << 10)
#define ARR_INDEX(x) ((x) / KB(1))
#define MMAP_THREASHOLD KB(128)
#define MAX_SIZE (pow(10,8))

#define BINS_SIZE (128)
MallocMetaData* bins[BINS_SIZE] = {NULL};

MallocMetaData* getMatchingBin(size_t size){
    size_t i = ARR_INDEX(size);
    if (size == BINS_SIZE)
        i--;
    for (; i < BINS_SIZE ; ++i) {
        MallocMetaData* curr = bins[i];
        if (curr == NULL)
            continue;
        MallocMetaData* prev = NULL;
        while (curr) {
            if (curr->size >= size)
                break;
            prev = curr;
            curr = curr->next_arr;
        }
        if (curr == NULL)
            continue;
        else if (prev == NULL){ // list head
            bins[i] = curr->next_arr;
        }
        else{
            prev->next_arr = curr->next_arr;
            if (curr->next_arr)
                (curr->next_arr)->prev = prev;
        }
        curr->next_arr = curr->prev_arr = NULL;
        return curr;
    }
    return NULL;
}

void insertNewBinArray(MallocMetaData* newp) {
    size_t index = ARR_INDEX(newp->size);
    if (index == BINS_SIZE)
        index--;
    if (index >= BINS_SIZE)
        return;
    MallocMetaData* curr = bins[index];
    if (curr == NULL) {
        bins[index] = newp;
        newp->next_arr = newp->prev_arr = NULL;
        return;
    }
    MallocMetaData* prev = NULL;
    while (curr) {
        if (curr->size >= newp->size)
            break;
        prev = curr;
        curr = curr->next_arr;
    }
    if (curr == NULL) { //end
        if(prev)
            prev->next_arr = newp;
        newp->prev_arr = prev;
    }else{
        curr->prev_arr = newp;
        newp->next_arr = curr;
        if (prev) {
            prev->next_arr = newp;
            newp->prev_arr = prev;
        }else{ // enter as first
            newp->prev_arr = NULL;
            bins[index] = newp;
        }
    }
}

void removeBinArray(MallocMetaData* oldp) {
    size_t index = ARR_INDEX(oldp->size);
    if (index == BINS_SIZE)
        index--;
    MallocMetaData* prev = oldp->prev_arr;
    MallocMetaData* next = oldp->next_arr;
    oldp->next_arr = oldp->prev_arr = NULL;
    if (!prev && bins[index] != oldp)
        return;
    if (next)
        next->prev_arr = prev;
    if (prev)
        prev->next_arr = next;
    else //oldp is head
        bins[index] = next;
}

void splitBin(MallocMetaData* current, size_t size){
    if (IS_BLOCK_SPLITTABLE(current->size,size)) {
        removeBinArray(current);
        current->is_free = false;
        MallocMetaData *ptr = (MallocMetaData *) ((size_t) current + size + sizeof(MallocMetaData));
        size_t addr = (size_t)ptr+ALIGNMENT+sizeof(MallocMetaData);
        size_t align = addr % ALIGNMENT;
        void* pUser = (void*)(addr - align);
        MallocMetaData *newp = (MallocMetaData *)((size_t)pUser-sizeof(MallocMetaData));
        newp->alignment = align == 0 ? 8 : align;
        newp->is_free = true;
        newp->is_mmap = false;
        newp->size = current->size - sizeof(MallocMetaData) - size;
        newp->prev = current;
        newp->next = current->next;
        current->size = size;
        if (current->next)
            (current->next)->prev = newp;
        else
            g_listTail = newp;
        current->next = newp;
        insertNewBinArray(newp);
    }
}

void* smalloc(size_t size)
{
    if((size==0)||(size > MAX_SIZE))
    {
        return NULL;
    }
    if (size <= MMAP_THREASHOLD){
        MallocMetaData *newp = getMatchingBin(size);
        if (newp){
            newp->is_free = false;
            splitBin(newp, size);
            return RETURN_TO_USER(newp);
        }
        void *ptr;
        if(g_listTail != NULL && g_listTail->is_free){
            ptr = sbrk(size - g_listTail->size);
            if (ptr ==  (void*)(-1))
            {
                return NULL;
            }
            removeBinArray(g_listTail);
            g_listTail->size = size;
            g_listTail->is_free = false;
            return RETURN_TO_USER(g_listTail);
        }
        else{
            ptr = sbrk(SIZE_OF_MALLOC(size));
            if (ptr ==  (void*)(-1))
            {
                return NULL;
            }
            void* addr = (void*)((size_t)ptr+ALIGNMENT+sizeof(MallocMetaData));
            size_t align = (size_t)addr % ALIGNMENT;
            void* pUser = (void*)((size_t)addr - (size_t)addr%ALIGNMENT);
            newp = (MallocMetaData *)((size_t)pUser-sizeof(MallocMetaData));
            newp->alignment = align == 0 ? 8 : align;
            newp->is_free = false;
            newp->is_mmap = false;
            newp->size = size;
            newp->prev = g_listTail;
            newp->next = NULL;
            if (g_listHead == NULL)
            {
                g_listHead = g_listTail =  (MallocMetaData*)newp;
            }
            else
            {
                g_listTail->next = newp;
                g_listTail = g_listTail->next;
            }
            return pUser;
        }
    }
    else{
        void* ptr = mmap(NULL ,(SIZE_OF_MALLOC(size)), (PROT_READ | PROT_WRITE) , (MAP_ANONYMOUS | MAP_PRIVATE), -1, 0) ;
        if(ptr == MAP_FAILED){
            return NULL;
        }
        size_t addr = (size_t)ptr+ALIGNMENT+sizeof(MallocMetaData);
        size_t align = addr % ALIGNMENT;
        void* pUser = (void*)(addr - addr%ALIGNMENT);
        MallocMetaData * current = (MallocMetaData *)((size_t)pUser-sizeof(MallocMetaData));
        current->alignment = align == 0 ? 8 : align;
        current->size = size;
        current->is_mmap = true;
        current->next = NULL;
        current->prev = g_mmaplistTail;
        if (g_mmaplistHead == NULL)
        {
            g_mmaplistHead = g_mmaplistTail = (MallocMetaData*)current;
        }
        else
        {
            g_mmaplistTail->next = current;
            g_mmaplistTail = g_mmaplistTail->next;
        }
        return pUser;
    }
}

void* scalloc(size_t num, size_t size)
{
    void* ptr = smalloc(num*size);
    if (ptr == NULL)
    {
        return NULL;
    }
    memset(ptr,0 ,num*size);
    return ptr;
}

void combineWithNext(MallocMetaData* p){
    p->size += p->next->size + sizeof(MallocMetaData);
    p->next = p->next->next;
    if (p->next) {
        (p->next)->prev = p;
    }
    else if (g_listTail == p->next)
        g_listTail = p;
}

void combineAdjacent(MallocMetaData* current){
    if(current->next && (current->next)->is_free) {
        removeBinArray(current->next);
        removeBinArray(current);
        combineWithNext(current);
        insertNewBinArray(current);
    }
    if (current->prev && (current->prev)->is_free) {
        removeBinArray(current->prev);
        removeBinArray(current);
        combineWithNext(current->prev);
        insertNewBinArray(current->prev);
    }
}

void sfree(void* p)
{
    if (p == NULL){
        return;
    }
    MallocMetaData* metaPtr = P_METADATA(p);
    if (metaPtr->is_mmap){
        if (metaPtr == g_mmaplistHead){
            g_mmaplistHead = metaPtr->next;
            if (g_mmaplistHead){
                g_mmaplistHead->prev = NULL;
            }
            else //g_mmaplistHead = NULL
                g_mmaplistTail = NULL;
        }
        else if (metaPtr == g_mmaplistTail){
            g_mmaplistTail = metaPtr->prev;
            metaPtr->prev->next = NULL;
        }
        else{
            metaPtr->next->prev = metaPtr->prev;
            metaPtr->prev->next = metaPtr->next;
        }
        munmap(metaPtr ,metaPtr->size + sizeof(MallocMetaData));
    }else {
        if (metaPtr->is_free)
            return;
        insertNewBinArray(metaPtr);
        combineAdjacent(metaPtr);
        metaPtr->is_free = true;
    }
}

void* srealloc(void* oldp, size_t size)
{
    if (oldp == NULL){
        return smalloc(size);
    }
    if((size==0)||(size > MAX_SIZE))
    {
        return NULL;
    }
    MallocMetaData* metaPtr = P_METADATA(oldp);
    if(metaPtr->is_mmap){
        if(metaPtr->size == size){
            return oldp;
        }
        void* newp = smalloc(size);
        if (newp == NULL){
            return NULL;
        }
        if (metaPtr->size < size)
            memmove(newp, oldp, metaPtr->size);
        sfree(oldp);
        return newp;
    }
    if(metaPtr->size >= size){ //a
        splitBin(metaPtr, metaPtr->size);
        return oldp;
    }
    else if(metaPtr->prev && metaPtr->prev->is_free && (metaPtr->size + metaPtr->prev->size + sizeof(MallocMetaData)) > size){ //b
        MallocMetaData* newp = metaPtr->prev;
        removeBinArray(newp);
        combineWithNext(newp);
        newp->is_free = false;
        splitBin(newp, size);
        memmove(RETURN_TO_USER(newp) ,oldp ,metaPtr->size);
        return RETURN_TO_USER(newp);
    }
    else if(metaPtr->next && metaPtr->next->is_free && (metaPtr->size + metaPtr->next->size + sizeof(MallocMetaData)) > size){ //c
        removeBinArray(metaPtr->next);
        combineWithNext(metaPtr);
        metaPtr->next->is_free = false;
        splitBin(metaPtr, size);
        return oldp;
    }
    else if(metaPtr->prev && metaPtr->next && metaPtr->prev->is_free && metaPtr->next->is_free &&
            (metaPtr->size + metaPtr->prev->size + (2*sizeof(MallocMetaData)) + metaPtr->next->size) > size){ //d
        MallocMetaData* newp = metaPtr->prev;
        removeBinArray(newp);
        removeBinArray(metaPtr->next);
        combineWithNext(metaPtr);
        combineWithNext(newp);
        newp->is_free = false;
        metaPtr->next->is_free = false;
        splitBin(newp, size);
        memmove(RETURN_TO_USER(newp) ,oldp , metaPtr->size);
        return RETURN_TO_USER(newp);
    }
    else { //metaPtr->size < size
        if(g_listTail == metaPtr) {
            void* ptr = sbrk(size - g_listTail->size);
            if (ptr == (void *) (-1)) {
                return NULL;
            }
            g_listTail->size = size;
            g_listTail->is_free = false;
            return RETURN_TO_USER(g_listTail);
        }
        metaPtr->is_free = true;
        void *newp = smalloc(size);
        if (newp == NULL) {
            return NULL;
        }
        memmove(newp ,oldp ,metaPtr->size);
        if (newp != oldp)
            sfree(oldp);
        return newp;
    }
}

size_t _num_free_blocks(){
    size_t count = 0;
    MallocMetaData* current = g_listHead;
    while (current != NULL)
    {
        count += current->is_free;
        current = current->next;
    }

    return count;
}

size_t _num_free_bytes()
{
    size_t count = 0;
    MallocMetaData* current = g_listHead;
    while (current != NULL)
    {
        if (current->is_free) {
            count += current->size;
        }
        current = current->next;
    }
    return count;
}

size_t _num_allocated_blocks(){
    size_t count = 0;
    MallocMetaData* current = g_listHead;
    while (current != NULL)
    {
        count ++;
        current = current->next;
    }
    current = g_mmaplistHead;
    while (current != NULL){
        count ++;
        current = current->next;
    }
    return count;
}

size_t _num_allocated_bytes(){
    size_t count = 0;
    MallocMetaData* current = g_listHead;
    while (current != NULL)
    {
        count += current->size;
        current = current->next;
    }
    current = g_mmaplistHead;
    while (current != NULL){
        count += current->size;
        current = current->next;
    }
    return count;
}

size_t _num_meta_data_bytes() {
    size_t count = 0;
    MallocMetaData* current = g_listHead;
    size_t metadata_size = sizeof(MallocMetaData);
    while (current != NULL) {
        count += metadata_size;
        current = current->next;
    }
    current = g_mmaplistHead;
    while (current != NULL){
        count += metadata_size;
        current = current->next;
    }
    return count;
}

size_t _size_meta_data(){
    return sizeof(MallocMetaData);
}