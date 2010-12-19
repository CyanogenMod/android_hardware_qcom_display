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

//#define LOG_NDEBUG 0

#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

#include <cutils/log.h>
#include <cutils/ashmem.h>

#include "gralloc_priv.h"
#include "pmemalloc.h"


#define BEGIN_FUNC LOGV("%s begin", __PRETTY_FUNCTION__)
#define END_FUNC LOGV("%s end", __PRETTY_FUNCTION__)


static int get_open_flags(int usage) {
    int openFlags = O_RDWR | O_SYNC;
    uint32_t uread = usage & GRALLOC_USAGE_SW_READ_MASK;
    uint32_t uwrite = usage & GRALLOC_USAGE_SW_WRITE_MASK;
    if (uread == GRALLOC_USAGE_SW_READ_OFTEN ||
        uwrite == GRALLOC_USAGE_SW_WRITE_OFTEN) {
        openFlags &= ~O_SYNC;
    }
    return openFlags;
}

PmemAllocator::~PmemAllocator()
{
    BEGIN_FUNC;
    END_FUNC;
}


PmemUserspaceAllocator::PmemUserspaceAllocator(Deps& deps, Deps::Allocator& allocator, const char* pmemdev):
    deps(deps),
    allocator(allocator),
    pmemdev(pmemdev),
    master_fd(MASTER_FD_INIT)
{
    BEGIN_FUNC;
    pthread_mutex_init(&lock, NULL);
    END_FUNC;
}


PmemUserspaceAllocator::~PmemUserspaceAllocator()
{
    BEGIN_FUNC;
    END_FUNC;
}


void* PmemUserspaceAllocator::get_base_address() {
    BEGIN_FUNC;
    END_FUNC;
    return master_base;
}


int PmemUserspaceAllocator::init_pmem_area_locked()
{
    BEGIN_FUNC;
    int err = 0;
    int fd = deps.open(pmemdev, O_RDWR, 0);
    if (fd >= 0) {
        size_t size = 0;
        err = deps.getPmemTotalSize(fd, &size);
        if (err < 0) {
            LOGE("%s: PMEM_GET_TOTAL_SIZE failed (%d), limp mode", pmemdev,
                    err);
            size = 8<<20;   // 8 MiB
        }
        allocator.setSize(size);

        void* base = deps.mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd,
                0);
        if (base == MAP_FAILED) {
            LOGE("%s: failed to map pmem master fd: %s", pmemdev,
                    strerror(deps.getErrno()));
            err = -deps.getErrno();
            base = 0;
            deps.close(fd);
            fd = -1;
        } else {
            master_fd = fd;
            master_base = base;
        }
    } else {
        LOGE("%s: failed to open pmem device: %s", pmemdev,
                strerror(deps.getErrno()));
        err = -deps.getErrno();
    }
    END_FUNC;
    return err;
}


int PmemUserspaceAllocator::init_pmem_area()
{
    BEGIN_FUNC;
    pthread_mutex_lock(&lock);
    int err = master_fd;
    if (err == MASTER_FD_INIT) {
        // first time, try to initialize pmem
        err = init_pmem_area_locked();
        if (err) {
            LOGE("%s: failed to initialize pmem area", pmemdev);
            master_fd = err;
        }
    } else if (err < 0) {
        // pmem couldn't be initialized, never use it
    } else {
        // pmem OK
        err = 0;
    }
    pthread_mutex_unlock(&lock);
    END_FUNC;
    return err;
}


int PmemUserspaceAllocator::alloc_pmem_buffer(size_t size, int usage,
        void** pBase, int* pOffset, int* pFd)
{
    BEGIN_FUNC;
    int err = init_pmem_area();
    if (err == 0) {
        void* base = master_base;
        int offset = allocator.allocate(size);
        if (offset < 0) {
            // no more pmem memory
            LOGE("%s: no more pmem available", pmemdev);
            err = -ENOMEM;
        } else {
            int openFlags = get_open_flags(usage);

            //LOGD("%s: allocating pmem at offset 0x%p", pmemdev, offset);

            // now create the "sub-heap"
            int fd = deps.open(pmemdev, openFlags, 0);
            err = fd < 0 ? fd : 0;

            // and connect to it
            if (err == 0)
                err = deps.connectPmem(fd, master_fd);

            // and make it available to the client process
            if (err == 0)
                err = deps.mapPmem(fd, offset, size);

            if (err < 0) {
                LOGE("%s: failed to initialize pmem sub-heap: %d", pmemdev,
                        err);
                err = -deps.getErrno();
                deps.close(fd);
                allocator.deallocate(offset);
                fd = -1;
            } else {
                LOGV("%s: mapped fd %d at offset %d, size %d", pmemdev, fd, offset, size);
                memset((char*)base + offset, 0, size);
                //cacheflush(intptr_t(base) + offset, intptr_t(base) + offset + size, 0);
                *pBase = base;
                *pOffset = offset;
                *pFd = fd;
            }
            //LOGD_IF(!err, "%s: allocating pmem size=%d, offset=%d", pmemdev, size, offset);
        }
    }
    END_FUNC;
    return err;
}


int PmemUserspaceAllocator::free_pmem_buffer(size_t size, void* base, int offset, int fd)
{
    BEGIN_FUNC;
    int err = 0;
    if (fd >= 0) {
        int err = deps.unmapPmem(fd, offset, size);
        LOGE_IF(err<0, "PMEM_UNMAP failed (%s), fd=%d, sub.offset=%u, "
                "sub.size=%u", strerror(deps.getErrno()), fd, offset, size);
        if (err == 0) {
            // we can't deallocate the memory in case of UNMAP failure
            // because it would give that process access to someone else's
            // surfaces, which would be a security breach.
            allocator.deallocate(offset);
        }
    }
    END_FUNC;
    return err;
}

PmemUserspaceAllocator::Deps::Allocator::~Allocator()
{
    BEGIN_FUNC;
    END_FUNC;
}

PmemUserspaceAllocator::Deps::~Deps()
{
    BEGIN_FUNC;
    END_FUNC;
}

PmemKernelAllocator::PmemKernelAllocator(Deps& deps, const char* pmemdev):
    deps(deps),
    pmemdev(pmemdev)
{
    BEGIN_FUNC;
    END_FUNC;
}


PmemKernelAllocator::~PmemKernelAllocator()
{
    BEGIN_FUNC;
    END_FUNC;
}


void* PmemKernelAllocator::get_base_address() {
    BEGIN_FUNC;
    END_FUNC;
    return 0;
}


static unsigned clp2(unsigned x) {
    x = x - 1;
    x = x | (x >> 1);
    x = x | (x >> 2);
    x = x | (x >> 4);
    x = x | (x >> 8);
    x = x | (x >>16);
    return x + 1;
}


int PmemKernelAllocator::alloc_pmem_buffer(size_t size, int usage,
        void** pBase,int* pOffset, int* pFd)
{
    BEGIN_FUNC;

    *pBase = 0;
    *pOffset = 0;
    *pFd = -1;

    int err;
    int openFlags = get_open_flags(usage);
    int fd = deps.open(pmemdev, openFlags, 0);
    if (fd < 0) {
        err = -deps.getErrno();
        END_FUNC;
        return err;
    }

    // The size should already be page aligned, now round it up to a power of 2.
    size = clp2(size);

    void* base = deps.mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        LOGE("%s: failed to map pmem fd: %s", pmemdev,
             strerror(deps.getErrno()));
        err = -deps.getErrno();
        deps.close(fd);
        END_FUNC;
        return err;
    }

    memset(base, 0, size);

    *pBase = base;
    *pOffset = 0;
    *pFd = fd;

    END_FUNC;
    return 0;
}


int PmemKernelAllocator::free_pmem_buffer(size_t size, void* base, int offset, int fd)
{
    BEGIN_FUNC;
    // The size should already be page aligned, now round it up to a power of 2
    // like we did when allocating.
    size = clp2(size);

    int err = deps.munmap(base, size);
    if (err < 0) {
        err = deps.getErrno();
        LOGW("%s: error unmapping pmem fd: %s", pmemdev, strerror(err));
        return -err;
    }
    END_FUNC;
    return 0;
}

PmemKernelAllocator::Deps::~Deps()
{
    BEGIN_FUNC;
    END_FUNC;
}
