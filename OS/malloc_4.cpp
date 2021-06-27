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

MallocMetaData* g_listHead = nullptr;
MallocMetaData* g_listTail = nullptr;
MallocMetaData* g_mmaplistHead = nullptr;
MallocMetaData* g_mmaplistTail = nullptr;

#define ALIGNMENT (8)
#define SIZE_OF_MALLOC(size) ((size)+sizeof(MallocMetaData)+ALIGNMENT)
#define RETURN_TO_USER(curr) (void*)((size_t)(curr)+sizeof(MallocMetaData))
#define P_METADATA(p)  (MallocMetaData*)((size_t)(p)-sizeof(MallocMetaData))
#define MIN_BIN_SIZE (128)
#define IS_BLOCK_SPLITTABLE(block_size ,size) \
        (((block_size)-((size)+sizeof(MallocMetaData))) > SIZE_OF_MALLOC(MIN_BIN_SIZE))
#define KB(x)  ((size_t) (x) << 10)
#define ARR_INDEX(x) ((x) / KB(1))
#define MMAP_THREASHOLD KB(128)
#define MAX_SIZE (pow(10,8))
#define BINS_SIZE (128)

MallocMetaData* bins[BINS_SIZE] = {nullptr};

MallocMetaData* getMatchingBin(size_t size){
    size_t i = ARR_INDEX(size);
    if (size == BINS_SIZE)
        i--;
    for (; i < BINS_SIZE ; ++i) {
        MallocMetaData* curr = bins[i];
        if (curr == nullptr)
            continue;
        MallocMetaData* prev = nullptr;
        while (curr) {
            if (curr->size >= size)
                break;
            prev = curr;
            curr = curr->next_arr;
        }
        if (curr == nullptr)
            continue;
        else if (prev == nullptr){ // list head
            bins[i] = curr->next_arr;
        }
        else{
            prev->next_arr = curr->next_arr;
            curr->next_arr->prev = prev;
        }
        curr->next_arr = curr->prev_arr = nullptr;
        return curr;
    }
    return nullptr;
}

void insertNewBinArray(MallocMetaData* newp) {
    size_t index = ARR_INDEX(newp->size);
    if (index == BINS_SIZE)
        index--;
    if (index >= BINS_SIZE)
        return;
    MallocMetaData* curr = bins[index];
    if (curr == nullptr) {
        bins[index] = newp;
        newp->next_arr = newp->prev_arr = nullptr;
        return;
    }
    MallocMetaData* prev = nullptr;
    while (curr) {
        if (curr->size >= newp->size)
            break;
        prev = curr;
        curr = curr->next_arr;
    }
    if (curr == nullptr) { //end
        if(prev)
            prev->next_arr = newp;
        newp->prev = prev;
    }else{
        if (prev) {
            prev->next_arr = newp;
            newp->next_arr = curr;
            curr->prev_arr = newp;
            newp->prev_arr = prev;
        }
    }
}

void removeBinArray(MallocMetaData* oldp) {
    size_t index = ARR_INDEX(oldp->size);
    if (index == BINS_SIZE)
        index--;
    MallocMetaData* prev = oldp->prev_arr;
    MallocMetaData* next = oldp->next_arr;
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
        newp->alignment = align;
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
        insertNewBinArray(current);
    }
}

void* smalloc(size_t size)
{
    if((size==0)||(size > MAX_SIZE))
    {
        return NULL;
    }
    if (size < MMAP_THREASHOLD){
        MallocMetaData *newp = getMatchingBin(size);
        if (newp){
            splitBin(newp, size);
            return RETURN_TO_USER(newp);
        }
        void *ptr;
        if(g_listTail && g_listTail->is_free)
            {
                ptr = sbrk(size - g_listTail->size);
                if (ptr ==  (void*)(-1))
                {
                    return NULL;
                }
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
            size_t addr = (size_t)ptr+ALIGNMENT+sizeof(MallocMetaData);
            size_t align = addr % ALIGNMENT;
            void* pUser = (void*)(addr - addr%ALIGNMENT);
            newp = (MallocMetaData *)((size_t)pUser-sizeof(MallocMetaData));
            newp->alignment = align;
            newp->is_free = false;
            newp->is_mmap = false;
            newp->size = size;
            newp->prev = g_listTail;
            newp->next = nullptr;
            insertNewBinArray(newp);
            if (g_listHead == nullptr)
            {
                g_listHead = g_listTail = (MallocMetaData*)ptr;
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
        current->alignment = align;
        current->size = size;
        current->is_mmap = true;
        current->next = nullptr;
        current->prev = g_mmaplistTail;
        if (g_mmaplistHead == nullptr)
        {
            g_mmaplistHead = g_mmaplistTail = (MallocMetaData*)ptr;
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
    removeBinArray(p->next);
    removeBinArray(p);
    p->size += p->next->size + sizeof(MallocMetaData) + p->next->alignment;
    p->next = p->next->next;
    if (p->next->next) {
        (p->next->next)->prev = p;
    }
    else if (g_listTail == p->next)
        g_listTail = p;
    insertNewBinArray(p);
}

void combineAdjacent(MallocMetaData* current){
    if(current->next && (current->next)->is_free) {
        combineWithNext(current);
    }
    if (current->prev && (current->prev)->is_free) {
        combineWithNext(current->prev);
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
                g_mmaplistHead->prev = nullptr;
            }
            else //g_mmaplistHead = NULL
                g_mmaplistTail = nullptr;
        }
        else if (metaPtr == g_mmaplistTail){
            g_mmaplistTail = metaPtr->prev;
            metaPtr->prev->next = nullptr;
        }
        else{
            metaPtr->next->prev = metaPtr->prev;
            metaPtr->prev->next = metaPtr->next;
        }
        munmap(metaPtr ,metaPtr->size + sizeof(MallocMetaData));
    }else {
        if (metaPtr->is_free)
            return;
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
        if (newp== nullptr){
            return NULL;
        }
        if (metaPtr->size < size)
            memcpy(newp, oldp, metaPtr->size);
        sfree(oldp);
        return newp;
    }
    if(metaPtr->size >= size){ //a
        splitBin(metaPtr, metaPtr->size);
        return oldp;
    }
    else if(metaPtr->prev && metaPtr->prev->is_free && (metaPtr->size + metaPtr->prev->size + sizeof(MallocMetaData)) > size){ //b
        MallocMetaData* newp = metaPtr->prev;
        combineWithNext(newp);
        splitBin(newp, size);
        memmove(newp ,oldp ,metaPtr->size);
        return RETURN_TO_USER(newp);
    }
    else if(metaPtr->next && metaPtr->next->is_free && (metaPtr->size + metaPtr->next->size + sizeof(MallocMetaData)) > size){ //c
        combineWithNext(metaPtr);
        splitBin(metaPtr, metaPtr->size);
        return oldp;
    }
    else if(metaPtr->prev && metaPtr->next && metaPtr->prev->is_free && metaPtr->next->is_free &&
            (metaPtr->size + metaPtr->prev->size + (2*sizeof(MallocMetaData)) + metaPtr->next->size) > size){ //d
        MallocMetaData* newp = metaPtr->prev;
        combineWithNext(metaPtr);
        combineWithNext(newp);
        splitBin(newp, size);
        memmove(newp ,oldp , metaPtr->size);
        return RETURN_TO_USER(newp);
    }
    else {
        void *newp = smalloc(size);
        if (newp == nullptr) {
            return nullptr;
        }
        memmove(newp ,oldp ,metaPtr->size);
        sfree(oldp);
        return newp;
    }
}

size_t _num_free_blocks(){
    size_t count = 0;
    MallocMetaData* current = g_listHead;
    while (current != nullptr)
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
    while (current != nullptr)
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
    while (current != nullptr)
    {
        count ++;
        current = current->next;
    }
    return count;
}

size_t _num_allocated_bytes(){
    size_t count = 0;
    MallocMetaData* current = g_listHead;
    while (current != nullptr)
    {
        count += current->size;
        current = current->next;
    }
    return count;
}

size_t _num_meta_data_bytes() {
    size_t count = 0;
    MallocMetaData* current = g_listHead;
    size_t metadata_size = sizeof(MallocMetaData);
    while (current != nullptr) {
        count += metadata_size;
        current = current->next;
    }
    return count;
}

size_t _size_meta_data(){
    return sizeof(MallocMetaData);
}