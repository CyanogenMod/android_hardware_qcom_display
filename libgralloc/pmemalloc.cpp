/*
 * Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <cutils/log.h>
#include <errno.h>
#include <linux/android_pmem.h>
#include "gralloc_priv.h"
#include "pmemalloc.h"
#include "pmem_bestfit_alloc.h"

using namespace gralloc;
using android::sp;

// Common functions between userspace
// and kernel allocators
static int getPmemTotalSize(int fd, size_t* size)
{
    //XXX: 7x27
    int err = 0;
    pmem_region region;
    if (ioctl(fd, PMEM_GET_TOTAL_SIZE, &region)) {
        err = -errno;
    } else {
        *size = region.len;
    }
    return err;
}

static int getOpenFlags(bool uncached)
{
    if(uncached)
        return O_RDWR | O_SYNC;
    else
        return O_RDWR;
}

static int connectPmem(int fd, int master_fd) {
    if (ioctl(fd, PMEM_CONNECT, master_fd))
        return -errno;
    return 0;
}

static int mapSubRegion(int fd, int offset, size_t size) {
    struct pmem_region sub = { offset, size };
    if (ioctl(fd, PMEM_MAP, &sub))
        return -errno;
    return 0;
}

static int unmapSubRegion(int fd, int offset, size_t size) {
    struct pmem_region sub = { offset, size };
    if (ioctl(fd, PMEM_UNMAP, &sub))
        return -errno;
    return 0;
}

static int alignPmem(int fd, size_t size, int align) {
    struct pmem_allocation allocation;
    allocation.size = size;
    allocation.align = align;
    if (ioctl(fd, PMEM_ALLOCATE_ALIGNED, &allocation) < 0)
        return -errno;
    return 0;
}

static int cleanPmem(void *base, size_t size, int offset, int fd) {
    struct pmem_addr pmem_addr;
    pmem_addr.vaddr = (unsigned long) base;
    pmem_addr.offset = offset;
    pmem_addr.length = size;
    if (ioctl(fd, PMEM_CLEAN_INV_CACHES, &pmem_addr))
        return -errno;
    return 0;
}

//-------------- PmemUserspaceAlloc-----------------------//
PmemUserspaceAlloc::PmemUserspaceAlloc()
{
    mPmemDev = DEVICE_PMEM;
    mMasterFd = FD_INIT;
    mAllocator = new SimpleBestFitAllocator();
    pthread_mutex_init(&mLock, NULL);
}

PmemUserspaceAlloc::~PmemUserspaceAlloc()
{
}

int PmemUserspaceAlloc::init_pmem_area_locked()
{
    ALOGV("%s: Opening master pmem FD", __FUNCTION__);
    int err = 0;
    int fd = open(mPmemDev, O_RDWR, 0);
    if (fd >= 0) {
        size_t size = 0;
        err = getPmemTotalSize(fd, &size);
        ALOGV("%s: Total pmem size: %d", __FUNCTION__, size);
        if (err < 0) {
            ALOGE("%s: PMEM_GET_TOTAL_SIZE failed (%d), limp mode", mPmemDev,
                  err);
            size = 8<<20;   // 8 MiB
        }
        mAllocator->setSize(size);

        void* base = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd,
                          0);
        if (base == MAP_FAILED) {
            err = -errno;
            ALOGE("%s: Failed to map pmem master fd: %s", mPmemDev,
                  strerror(errno));
            base = 0;
            close(fd);
            fd = -1;
        } else {
            mMasterFd = fd;
            mMasterBase = base;
        }
    } else {
        err = -errno;
        ALOGE("%s: Failed to open pmem device: %s", mPmemDev,
              strerror(errno));
    }
    return err;
}

int  PmemUserspaceAlloc::init_pmem_area()
{
    pthread_mutex_lock(&mLock);
    int err = mMasterFd;
    if (err == FD_INIT) {
        // first time, try to initialize pmem
        ALOGV("%s: Initializing pmem area", __FUNCTION__);
        err = init_pmem_area_locked();
        if (err) {
            ALOGE("%s: failed to initialize pmem area", mPmemDev);
            mMasterFd = err;
        }
    } else if (err < 0) {
        // pmem couldn't be initialized, never use it
    } else {
        // pmem OK
        err = 0;
    }
    pthread_mutex_unlock(&mLock);
    return err;

}

int PmemUserspaceAlloc::alloc_buffer(alloc_data& data)
{
    int err = init_pmem_area();
    if (err == 0) {
        void* base = mMasterBase;
        size_t size = data.size;
        int offset = mAllocator->allocate(size);
        if (offset < 0) {
            // no more pmem memory
            ALOGE("%s: No more pmem available", mPmemDev);
            err = -ENOMEM;
        } else {
            int openFlags = getOpenFlags(data.uncached);

            // now create the "sub-heap"
            int fd = open(mPmemDev, openFlags, 0);
            err = fd < 0 ? fd : 0;

            // and connect to it
            if (err == 0)
                err = connectPmem(fd, mMasterFd);

            // and make it available to the client process
            if (err == 0)
                err = mapSubRegion(fd, offset, size);

            if (err < 0) {
                ALOGE("%s: Failed to initialize pmem sub-heap: %d", mPmemDev,
                      err);
                close(fd);
                mAllocator->deallocate(offset);
                fd = -1;
            } else {
                ALOGV("%s: Allocated buffer base:%p size:%d offset:%d fd:%d",
                      mPmemDev, base, size, offset, fd);
                memset((char*)base + offset, 0, size);
                //Clean cache before flushing to ensure pmem is properly flushed
                err = clean_buffer((void*)((intptr_t) base + offset), size, offset, fd);
                if (err < 0) {
                    ALOGE("cleanPmem failed: (%s)", strerror(errno));
                }
                cacheflush(intptr_t(base) + offset, intptr_t(base) + offset + size, 0);
                data.base = base;
                data.offset = offset;
                data.fd = fd;
            }
        }
    }
    return err;

}

int PmemUserspaceAlloc::free_buffer(void* base, size_t size, int offset, int fd)
{
    ALOGV("%s: Freeing buffer base:%p size:%d offset:%d fd:%d",
          mPmemDev, base, size, offset, fd);
    int err = 0;
    if (fd >= 0) {
        int err = unmapSubRegion(fd, offset, size);
        ALOGE_IF(err<0, "PMEM_UNMAP failed (%s), fd=%d, sub.offset=%u, "
                 "sub.size=%u", strerror(errno), fd, offset, size);
        if (err == 0) {
            // we can't deallocate the memory in case of UNMAP failure
            // because it would give that process access to someone else's
            // surfaces, which would be a security breach.
            mAllocator->deallocate(offset);
        }
        close(fd);
    }
    return err;
}

int PmemUserspaceAlloc::map_buffer(void **pBase, size_t size, int offset, int fd)
{
    int err = 0;
    size += offset;
    void *base = mmap(0, size, PROT_READ| PROT_WRITE,
                      MAP_SHARED, fd, 0);
    *pBase = base;
    if(base == MAP_FAILED) {
        err = -errno;
        ALOGE("%s: Failed to map buffer size:%d offset:%d fd:%d Error: %s",
              mPmemDev, size, offset, fd, strerror(errno));
    } else {
        ALOGV("%s: Mapped buffer base:%p size:%d offset:%d fd:%d",
              mPmemDev, base, size, offset, fd);
    }
    return err;

}

int PmemUserspaceAlloc::unmap_buffer(void *base, size_t size, int offset)
{
    int err = 0;
    //pmem hack
    base = (void*)(intptr_t(base) - offset);
    size += offset;
    ALOGV("%s: Unmapping buffer base:%p size:%d offset:%d",
          mPmemDev , base, size, offset);
    if (munmap(base, size) < 0) {
        err = -errno;
        ALOGE("%s: Failed to unmap memory at %p :%s",
              mPmemDev, base, strerror(errno));

    }

    return err;
}

int PmemUserspaceAlloc::clean_buffer(void *base, size_t size, int offset, int fd)
{
    return cleanPmem(base, size, offset, fd);
}


//-------------- PmemKernelAlloc-----------------------//

PmemKernelAlloc::PmemKernelAlloc(const char* pmemdev) :
    mPmemDev(pmemdev)
{
}

PmemKernelAlloc::~PmemKernelAlloc()
{
}

int PmemKernelAlloc::alloc_buffer(alloc_data& data)
{
    int err, offset = 0;
    int openFlags = getOpenFlags(data.uncached);
    int size = data.size;

    int fd = open(mPmemDev, openFlags, 0);
    if (fd < 0) {
        err = -errno;
        ALOGE("%s: Error opening %s", __FUNCTION__, mPmemDev);
        return err;
    }

    if (data.align == 8192) {
        // Tile format buffers need physical alignment to 8K
        // Default page size does not need this ioctl
        err = alignPmem(fd, size, 8192);
        if (err < 0) {
            ALOGE("alignPmem failed");
        }
    }
    void* base = mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        err = -errno;
        ALOGE("%s: failed to map pmem fd: %s", mPmemDev,
              strerror(errno));
        close(fd);
        return err;
    }
    memset(base, 0, size);
    clean_buffer((void*)((intptr_t) base + offset), size, offset, fd);
    data.base = base;
    data.offset = 0;
    data.fd = fd;
    ALOGV("%s: Allocated buffer base:%p size:%d fd:%d",
          mPmemDev, base, size, fd);
    return 0;

}

int PmemKernelAlloc::free_buffer(void* base, size_t size, int offset, int fd)
{
    ALOGV("%s: Freeing buffer base:%p size:%d fd:%d",
          mPmemDev, base, size, fd);

    int err =  unmap_buffer(base, size, offset);
    close(fd);
    return err;
}

int PmemKernelAlloc::map_buffer(void **pBase, size_t size, int offset, int fd)
{
    int err = 0;
    void *base = mmap(0, size, PROT_READ| PROT_WRITE,
                      MAP_SHARED, fd, 0);
    *pBase = base;
    if(base == MAP_FAILED) {
        err = -errno;
        ALOGE("%s: Failed to map memory in the client: %s",
              mPmemDev, strerror(errno));
    } else {
        ALOGV("%s: Mapped buffer base:%p size:%d, fd:%d",
              mPmemDev, base, size, fd);
    }
    return err;

}

int PmemKernelAlloc::unmap_buffer(void *base, size_t size, int offset)
{
    int err = 0;
    if (munmap(base, size)) {
        err = -errno;
        ALOGW("%s: Error unmapping memory at %p: %s",
              mPmemDev, base, strerror(err));
    }
    return err;

}
int PmemKernelAlloc::clean_buffer(void *base, size_t size, int offset, int fd)
{
    return cleanPmem(base, size, offset, fd);
}

