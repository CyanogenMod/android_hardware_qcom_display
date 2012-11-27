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

#ifndef GRALLOC_PMEMALLOC_H
#define GRALLOC_PMEMALLOC_H

#include <linux/ion.h>
#include <utils/RefBase.h>
#include "memalloc.h"

namespace gralloc {
class PmemUserspaceAlloc : public IMemAlloc  {

    public:
    class Allocator: public android::RefBase {
        public:
        virtual ~Allocator() {};
        virtual ssize_t setSize(size_t size) = 0;
        virtual size_t  size() const = 0;
        virtual ssize_t allocate(size_t size, uint32_t flags = 0) = 0;
        virtual ssize_t deallocate(size_t offset) = 0;
    };

    virtual int alloc_buffer(alloc_data& data);

    virtual int free_buffer(void *base, size_t size,
                            int offset, int fd);

    virtual int map_buffer(void **pBase, size_t size,
                           int offset, int fd);

    virtual int unmap_buffer(void *base, size_t size,
                             int offset);

    virtual int clean_buffer(void*base, size_t size,
                             int offset, int fd);

    PmemUserspaceAlloc();

    ~PmemUserspaceAlloc();

    private:
    int mMasterFd;
    void* mMasterBase;
    const char* mPmemDev;
    android::sp<Allocator> mAllocator;
    pthread_mutex_t mLock;
    int init_pmem_area();
    int init_pmem_area_locked();

};

class PmemKernelAlloc : public IMemAlloc  {

    public:
    virtual int alloc_buffer(alloc_data& data);

    virtual int free_buffer(void *base, size_t size,
                            int offset, int fd);

    virtual int map_buffer(void **pBase, size_t size,
                           int offset, int fd);

    virtual int unmap_buffer(void *base, size_t size,
                             int offset);

    virtual int clean_buffer(void*base, size_t size,
                             int offset, int fd);

    PmemKernelAlloc(const char* device);

    ~PmemKernelAlloc();
    private:
    const char* mPmemDev;


};

}
#endif /* GRALLOC_PMEMALLOC_H */
