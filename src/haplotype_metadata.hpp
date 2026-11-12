#pragma once
#include "platform_policy.hpp"
#ifndef FINDHAP_LIBRARY_METADATA
#define FINDHAP_LIBRARY_METADATA 0
#endif
#ifndef FINDHAP_METADATA_CHUNK
#define FINDHAP_METADATA_CHUNK 1
#endif
#if FINDHAP_LIBRARY_METADATA
#include <cstddef>
#include <limits>
#include <new>
#include <sys/mman.h>
#include <cstdlib>
#ifndef FINDHAP_METADATA_CALLOC
#define FINDHAP_METADATA_CALLOC 1
#endif
namespace findhap_haplotype_metadata {
#if FINDHAP_METADATA_CALLOC
// Match the native owner's one-pointer representation. Large libc calloc
// allocations retain demand-zero semantics without enlarging captured state.
class Array {
    int* data_=nullptr;
public:
    Array()=default;
    Array(const Array&)=delete;
    Array& operator=(const Array&)=delete;
    ~Array(){std::free(data_);}
    void allocate(size_t count){
        if(count>std::numeric_limits<size_t>::max()/sizeof(int))throw std::bad_array_new_length();
        std::free(data_);data_=nullptr;
        if(count){data_=static_cast<int*>(std::calloc(count,sizeof(int)));if(!data_)throw std::bad_alloc();}
    }
    int* get()const{return data_;}
    int& operator[](size_t at)const{return data_[at];}
};
#else
// Keep the original contiguous addresses and segment strides, but leave
// unpublished pages demand-zero. A new stage gets a fresh explicit lifetime.
class Array {
    int* data_ = nullptr;
    size_t bytes_ = 0;
    static __attribute__((malloc,alloc_size(1),noinline)) int* fresh_bytes(size_t bytes) {
        void* address=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(address==MAP_FAILED)throw std::bad_alloc();
        return static_cast<int*>(address);
    }
public:
    Array()=default;
    Array(const Array&)=delete;
    Array& operator=(const Array&)=delete;
    ~Array(){if(data_)munmap(data_,bytes_);}
    void allocate(size_t count) {
        if(count>std::numeric_limits<size_t>::max()/sizeof(int))throw std::bad_array_new_length();
        if(data_)munmap(data_,bytes_);
        data_=nullptr;bytes_=0;
        if(count){bytes_=count*sizeof(int);data_=fresh_bytes(bytes_);}
    }
    int* get()const{return data_;}
    int& operator[](size_t at)const{return data_[at];}
};
#endif
}
#endif
