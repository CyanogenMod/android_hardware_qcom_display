/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GRALLOC_QSD8K_PMEMALLOC_H
#define GRALLOC_QSD8K_PMEMALLOC_H

#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>


/**
 * An interface to the PMEM allocators.
 */
class PmemAllocator {

 public:

    virtual ~PmemAllocator();

    // Only valid after init_pmem_area() has completed successfully.
    virtual void* get_base_address() = 0;

    virtual int alloc_pmem_buffer(size_t size, int usage, void** pBase,
            int* pOffset, int* pFd) = 0;
    virtual int free_pmem_buffer(size_t size, void* base, int offset, int fd) = 0;
};


/**
 * A PMEM allocator that allocates the entire pmem memory from the kernel and
 * then uses a user-space allocator to suballocate from that.  This requires
 * that the PMEM device driver have kernel allocation disabled.
 */
class PmemUserspaceAllocator: public PmemAllocator {

 public:

    class Deps {
     public:

        class Allocator {
         public:
            virtual ~Allocator();
            virtual ssize_t setSize(size_t size) = 0;
            virtual size_t  size() const = 0;
            virtual ssize_t allocate(size_t size, uint32_t flags = 0) = 0;
            virtual ssize_t deallocate(size_t offset) = 0;
        };

        virtual ~Deps();

        // pmem
        virtual size_t getPmemTotalSize(int fd, size_t* size) = 0;
        virtual int connectPmem(int fd, int master_fd) = 0;
        virtual int mapPmem(int fd, int offset, size_t size) = 0;
        virtual int unmapPmem(int fd, int offset, size_t size) = 0;

        // C99
        virtual int getErrno() = 0;

        // POSIX
        virtual void* mmap(void* start, size_t length, int prot, int flags, int fd,
                off_t offset) = 0;
        virtual int open(const char* pathname, int flags, int mode) = 0;
        virtual int close(int fd) = 0;
    };

    PmemUserspaceAllocator(Deps& deps, Deps::Allocator& allocator, const char* pmemdev);
    virtual ~PmemUserspaceAllocator();

    // Only valid after init_pmem_area() has completed successfully.
    virtual void* get_base_address();

    virtual int init_pmem_area_locked();
    virtual int init_pmem_area();
    virtual int alloc_pmem_buffer(size_t size, int usage, void** pBase,
            int* pOffset, int* pFd);
    virtual int free_pmem_buffer(size_t size, void* base, int offset, int fd);

#ifndef ANDROID_OS
    // DO NOT USE: For testing purposes only.
    void set_master_values(int fd, void* base) {
        master_fd = fd;
        master_base = base;
    }
#endif // ANDROID_OS

 private:

    enum {
        MASTER_FD_INIT = -1,
    };

    Deps& deps;
    Deps::Allocator& allocator;

    pthread_mutex_t lock;
    const char* pmemdev;
    int master_fd;
    void* master_base;
};


/**
 * A PMEM allocator that allocates each individual allocation from the kernel
 * (using the kernel's allocator).  This requires the kernel driver for the
 * particular PMEM device being allocated from to support kernel allocation.
 */
class PmemKernelAllocator: public PmemAllocator {

 public:

    class Deps {
     public:

        virtual ~Deps();

        // C99
        virtual int getErrno() = 0;

        // POSIX
        virtual void* mmap(void* start, size_t length, int prot, int flags, int fd,
                off_t offset) = 0;
        virtual int munmap(void* start, size_t length) = 0;
        virtual int open(const char* pathname, int flags, int mode) = 0;
        virtual int close(int fd) = 0;
    };

    PmemKernelAllocator(Deps& deps, const char* pmemdev);
    virtual ~PmemKernelAllocator();

    // Only valid after init_pmem_area() has completed successfully.
    virtual void* get_base_address();

    virtual int alloc_pmem_buffer(size_t size, int usage, void** pBase,
            int* pOffset, int* pFd);
    virtual int free_pmem_buffer(size_t size, void* base, int offset, int fd);

 private:

    Deps& deps;

    const char* pmemdev;
};

#endif  // GRALLOC_QSD8K_PMEMALLOC_H
