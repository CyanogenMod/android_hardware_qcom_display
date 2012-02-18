/*
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.

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


#include <linux/ioctl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <fcntl.h>
#include <cutils/log.h>
#include <errno.h>
#include "gralloc_priv.h"
#include "ionalloc.h"

using gralloc::IonAlloc;

#define ION_DEVICE "/dev/ion"

int IonAlloc::open_device()
{
    if(mIonFd == FD_INIT)
        mIonFd = open(ION_DEVICE, O_RDONLY);

    if(mIonFd < 0 ) {
        LOGE("%s: Failed to open ion device - %s",
                __FUNCTION__, strerror(errno));
        mIonFd = FD_INIT;
        return -errno;
    }
    return 0;
}

void IonAlloc::close_device()
{
    if(mIonFd >= 0)
        close(mIonFd);
    mIonFd = FD_INIT;
}

int IonAlloc::alloc_buffer(alloc_data& data)
{
    int err = 0;
    int ionSyncFd = FD_INIT;
    int iFd = FD_INIT;
    struct ion_handle_data handle_data;
    struct ion_fd_data fd_data;
    struct ion_allocation_data ionAllocData;

    void *base = 0;

    ionAllocData.len = data.size;
    ionAllocData.align = data.align;
    ionAllocData.flags = data.flags;

    err = open_device();
    if (err)
        return err;

    if(data.uncached) {
        // Use the sync FD to alloc and map
        // when we need uncached memory
        ionSyncFd = open(ION_DEVICE, O_RDONLY|O_DSYNC);
        if(ionSyncFd < 0) {
            LOGE("%s: Failed to open ion device - %s",
                    __FUNCTION__, strerror(errno));
            return -errno;
        }
        iFd = ionSyncFd;
    } else {
        iFd = mIonFd;
    }

    if(ioctl(iFd, ION_IOC_ALLOC, &ionAllocData)) {
        err = -errno;
        LOGE("ION_IOC_ALLOC failed with error - %s", strerror(errno));
        if(ionSyncFd >= 0)
            close(ionSyncFd);
        ionSyncFd = FD_INIT;
        return err;
    }

    fd_data.handle = ionAllocData.handle;
    handle_data.handle = ionAllocData.handle;
    if(ioctl(iFd, ION_IOC_MAP, &fd_data)) {
        err = -errno;
        LOGE("%s: ION_IOC_MAP failed with error - %s",
                __FUNCTION__, strerror(errno));
        ioctl(mIonFd, ION_IOC_FREE, &handle_data);
        if(ionSyncFd >= 0)
            close(ionSyncFd);
        ionSyncFd = FD_INIT;
        return err;
    }

    if(!(data.flags & ION_SECURE) &&
       !(data.allocType & private_handle_t::PRIV_FLAGS_NOT_MAPPED)) {

        base = mmap(0, ionAllocData.len, PROT_READ|PROT_WRITE,
                                MAP_SHARED, fd_data.fd, 0);
        if(base == MAP_FAILED) {
            err = -errno;
            LOGE("%s: Failed to map the allocated memory: %s",
                                    __FUNCTION__, strerror(errno));
            ioctl(mIonFd, ION_IOC_FREE, &handle_data);
            ionSyncFd = FD_INIT;
            return err;
        }
        memset(base, 0, ionAllocData.len);
        // Clean cache after memset
        clean_buffer(base, data.size, data.offset, fd_data.fd);
    }

    //Close the uncached FD since we no longer need it;
    if(ionSyncFd >= 0)
        close(ionSyncFd);
    ionSyncFd = FD_INIT;

    data.base = base;
    data.fd = fd_data.fd;
    ioctl(mIonFd, ION_IOC_FREE, &handle_data);
    LOGD("ion: Allocated buffer base:%p size:%d fd:%d",
                            data.base, ionAllocData.len, data.fd);
    return 0;
}


int IonAlloc::free_buffer(void* base, size_t size, int offset, int fd)
{
    LOGD("ion: Freeing buffer base:%p size:%d fd:%d",
            base, size, fd);
    int err = 0;
    err = open_device();
    if (err)
        return err;

    if(base)
        err = unmap_buffer(base, size, offset);
    close(fd);
    return err;
}

int IonAlloc::map_buffer(void **pBase, size_t size, int offset, int fd)
{
    int err = 0;
    void *base = 0;
    // It is a (quirky) requirement of ION to have opened the
    // ion fd in the process that is doing the mapping
    err = open_device();
    if (err)
        return err;

    base = mmap(0, size, PROT_READ| PROT_WRITE,
            MAP_SHARED, fd, 0);
    *pBase = base;
    if(base == MAP_FAILED) {
        err = -errno;
        LOGD("ion: Failed to map memory in the client: %s",
                                strerror(errno));
    } else {
        LOGD("ion: Mapped buffer base:%p size:%d offset:%d fd:%d",
                                base, size, offset, fd);
    }
    return err;
}

int IonAlloc::unmap_buffer(void *base, size_t size, int offset)
{
    LOGD("ion: Unmapping buffer  base:%p size:%d", base, size);
    int err = 0;
    if(munmap(base, size)) {
        err = -errno;
        LOGE("ion: Failed to unmap memory at %p : %s",
                 base, strerror(errno));
    }
    return err;

}
int IonAlloc::clean_buffer(void *base, size_t size, int offset, int fd)
{
    struct ion_flush_data flush_data;
    struct ion_fd_data fd_data;
    struct ion_handle_data handle_data;
    struct ion_handle* handle;
    int err = 0;

    err = open_device();
    if (err)
        return err;

    fd_data.fd = fd;
    if (ioctl(mIonFd, ION_IOC_IMPORT, &fd_data)) {
        err = -errno;
        LOGE("%s: ION_IOC_IMPORT failed with error - %s",
                __FUNCTION__, strerror(errno));
        return err;
    }

    handle_data.handle = fd_data.handle;
    flush_data.handle  = fd_data.handle;
    flush_data.vaddr   = base;
    flush_data.offset  = offset;
    flush_data.length  = size;
    if(ioctl(mIonFd, ION_IOC_CLEAN_INV_CACHES, &flush_data)) {
        err = -errno;
        LOGE("%s: ION_IOC_CLEAN_INV_CACHES failed with error - %s",
                __FUNCTION__, strerror(errno));
        ioctl(mIonFd, ION_IOC_FREE, &handle_data);
        return err;
    }
    ioctl(mIonFd, ION_IOC_FREE, &handle_data);
    return 0;
}

