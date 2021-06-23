#ifndef HW4_MALLOC_2_H
#define HW4_MALLOC_2_H


#include <iostream>
#include <math.h>
#include <unistd.h>
#include <cstring>

void* smalloc(size_t size);

void* scalloc(size_t num, size_t size);

void sfree(void* p);

void* srealloc(void* oldp, size_t size);

size_t _num_free_blocks();

size_t _num_free_bytes();

size_t _num_allocated_blocks();

size_t _num_allocated_bytes();

size_t _num_meta_data_bytes();

size_t _size_meta_data();

#endif //HW4_MALLOC_2_H