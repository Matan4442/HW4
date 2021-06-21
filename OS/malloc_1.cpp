#include <iostream>
#include <math.h>
#include <unistd.h>

void* smalloc(size_t size)
{
    if((size==0)||(size > pow(10,8)))
    {
        return NULL;
    }
    void* sbrk_return = sbrk(size);
    if(sbrk_return == (void*)(-1))
    {
        return NULL;
    }
    return  sbrk_return;
}