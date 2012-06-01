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
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <cutils/log.h>
#include <linux/ashmem.h>
#include <cutils/ashmem.h>
#include <errno.h>
#include "ashmemalloc.h"

using gralloc::AshmemAlloc;
int AshmemAlloc::alloc_buffer(alloc_data& data)
{
    int err = 0;
    int fd = -1;
    void* base = 0;
    int offset = 0;
    char name[ASHMEM_NAME_LEN];
    snprintf(name, ASHMEM_NAME_LEN, "gralloc-buffer-%x", data.pHandle);
    int prot = PROT_READ | PROT_WRITE;
    fd = ashmem_create_region(name, data.size);
    if (fd < 0) {
        LOGE("couldn't create ashmem (%s)", strerror(errno));
        err = -errno;
    } else {
        if (ashmem_set_prot_region(fd, prot) < 0) {
            LOGE("ashmem_set_prot_region(fd=%d, prot=%x) failed (%s)",
                 fd, prot, strerror(errno));
            close(fd);
            err = -errno;
        } else {
            base = mmap(0, data.size, prot, MAP_SHARED|MAP_POPULATE|MAP_LOCKED, fd, 0);
            if (base == MAP_FAILED) {
                LOGE("alloc mmap(fd=%d, size=%d, prot=%x) failed (%s)",
                     fd, data.size, prot, strerror(errno));
                close(fd);
                err = -errno;
            } else {
                memset((char*)base + offset, 0, data.size);
            }
        }
    }
    if(err == 0) {
        data.fd = fd;
        data.base = base;
        data.offset = offset;
        clean_buffer(base, data.size, offset, fd);
        LOGV("ashmem: Allocated buffer base:%p size:%d fd:%d",
                                base, data.size, fd);

    }
    return err;

}

int AshmemAlloc::free_buffer(void* base, size_t size, int offset, int fd)
{
    LOGV("ashmem: Freeing buffer base:%p size:%d fd:%d",
                            base, size, fd);
    int err = 0;

    if(!base) {
        LOGE("Invalid free");
        return -EINVAL;
    }
    err = unmap_buffer(base, size, offset);
    close(fd);
    return err;
}

int AshmemAlloc::map_buffer(void **pBase, size_t size, int offset, int fd)
{
    int err = 0;
    void *base = 0;

    base = mmap(0, size, PROT_READ| PROT_WRITE,
            MAP_SHARED|MAP_POPULATE, fd, 0);
    *pBase = base;
    if(base == MAP_FAILED) {
        LOGE("ashmem: Failed to map memory in the client: %s",
                                strerror(errno));
        err = -errno;
    } else {
        LOGV("ashmem: Mapped buffer base:%p size:%d fd:%d",
                 base, size, fd);
    }
    return err;
}

int AshmemAlloc::unmap_buffer(void *base, size_t size, int offset)
{
    LOGV("ashmem: Unmapping buffer base: %p size: %d", base, size);
    int err = munmap(base, size);
    if(err) {
        LOGE("ashmem: Failed to unmap memory at %p: %s",
                                base, strerror(errno));
    }
    return err;

}
int AshmemAlloc::clean_buffer(void *base, size_t size, int offset, int fd)
{
    int err = 0;
    if (ioctl(fd, ASHMEM_CACHE_FLUSH_RANGE, NULL)) {
        LOGE("ashmem: ASHMEM_CACHE_FLUSH_RANGE failed fd = %d", fd);
    }

    return err;
}

