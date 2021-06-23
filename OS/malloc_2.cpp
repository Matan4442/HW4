//
// Created by Matan Tennenhaus on 6/18/2021.
//

#include <iostream>
#include <math.h>
#include <unistd.h>
#include <cstring>

typedef struct MallocMetaData {
    size_t size;
    bool  is_free;
    MallocMetaData* next;
    MallocMetaData* prev;
} MallocMetaData;

MallocMetaData* g_listHead = nullptr;
MallocMetaData* g_listTail = nullptr;

#define SIZE_OF_MALLOC(size) ((size)+sizeof(MallocMetaData))
#define RETURN_TO_USER(curr) (void*)((size_t)(curr)+sizeof(MallocMetaData))
#define P_METADATA(p)  (MallocMetaData*)((size_t)(p)-sizeof(MallocMetaData))
#define MAX_SIZE (pow(10,8))


void* smalloc(size_t size)
{
    if((size==0)||(size > MAX_SIZE))
    {
        return NULL;
    }
    MallocMetaData* current = g_listHead;
    while (current != nullptr)
    {
        if (current->is_free && current->size >= size)
        {
            current->is_free = false;
            return RETURN_TO_USER(current);
        }
        current = current->next;
    }
    void* ptr = sbrk(SIZE_OF_MALLOC(size));
    if (ptr ==  (void*)(-1))
    {
        return NULL;
    }
    MallocMetaData* new_metadata =  (MallocMetaData*)ptr;
    new_metadata->is_free = false;
    new_metadata->size = size;
    new_metadata->prev = g_listTail;
    new_metadata->next = nullptr;
    if (g_listHead == nullptr)
    {
        g_listHead = g_listTail =  (MallocMetaData*)ptr;
    }
    else
    {
        g_listTail->next = new_metadata;
        g_listTail = g_listTail->next;
    }
    return RETURN_TO_USER(ptr);

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

void sfree(void* p)
{
    if (p == nullptr){
        return;
    }
    MallocMetaData* metaPtr = P_METADATA(p);
    metaPtr->is_free = true;
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
    if(metaPtr->size >= size){
        return oldp;
    }
    void* newp = smalloc(size);
    if (newp == nullptr) {
        return nullptr;
    }
    memcpy(newp ,oldp ,metaPtr->size);
    sfree(oldp);
    return newp;
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