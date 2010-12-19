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

#include <limits.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/mman.h>

#include "gr.h"
#include "gpu.h"

gpu_context_t::gpu_context_t(Deps& deps, PmemAllocator& pmemAllocator,
        PmemAllocator& pmemAdspAllocator, const private_module_t* module) :
    deps(deps),
    pmemAllocator(pmemAllocator),
    pmemAdspAllocator(pmemAdspAllocator)
{
    // Zero out the alloc_device_t
    memset(static_cast<alloc_device_t*>(this), 0, sizeof(alloc_device_t));

    // Initialize the procs
    common.tag     = HARDWARE_DEVICE_TAG;
    common.version = 0;
    common.module  = const_cast<hw_module_t*>(&module->base.common);
    common.close   = gralloc_close;
    alloc          = gralloc_alloc;
    free           = gralloc_free;
}

int gpu_context_t::gralloc_alloc_framebuffer_locked(size_t size, int usage,
        buffer_handle_t* pHandle)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(common.module);

    // we don't support allocations with both the FB and PMEM_ADSP flags
    if (usage & GRALLOC_USAGE_PRIVATE_PMEM_ADSP) {
        return -EINVAL;
    }

    // allocate the framebuffer
    if (m->framebuffer == NULL) {
        // initialize the framebuffer, the framebuffer is mapped once
        // and forever.
        int err = deps.mapFrameBufferLocked(m);
        if (err < 0) {
            return err;
        }
    }

    const uint32_t bufferMask = m->bufferMask;
    const uint32_t numBuffers = m->numBuffers;
    const size_t bufferSize = m->finfo.line_length * m->info.yres;
    if (numBuffers == 1) {
        // If we have only one buffer, we never use page-flipping. Instead,
        // we return a regular buffer which will be memcpy'ed to the main
        // screen when post is called.
        int newUsage = (usage & ~GRALLOC_USAGE_HW_FB) | GRALLOC_USAGE_HW_2D;
        return gralloc_alloc_buffer(bufferSize, newUsage, pHandle);
    }

    if (bufferMask >= ((1LU<<numBuffers)-1)) {
        // We ran out of buffers.
        return -ENOMEM;
    }

    // create a "fake" handles for it
    intptr_t vaddr = intptr_t(m->framebuffer->base);
    private_handle_t* hnd = new private_handle_t(dup(m->framebuffer->fd), bufferSize,
                                                 private_handle_t::PRIV_FLAGS_USES_PMEM |
                                                 private_handle_t::PRIV_FLAGS_FRAMEBUFFER);

    // find a free slot
    for (uint32_t i=0 ; i<numBuffers ; i++) {
        if ((bufferMask & (1LU<<i)) == 0) {
            m->bufferMask |= (1LU<<i);
            break;
        }
        vaddr += bufferSize;
    }

    hnd->base = vaddr;
    hnd->offset = vaddr - intptr_t(m->framebuffer->base);
    *pHandle = hnd;

    return 0;
}


int gpu_context_t::gralloc_alloc_framebuffer(size_t size, int usage,
        buffer_handle_t* pHandle)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(common.module);
    pthread_mutex_lock(&m->lock);
    int err = gralloc_alloc_framebuffer_locked(size, usage, pHandle);
    pthread_mutex_unlock(&m->lock);
    return err;
}

int gpu_context_t::alloc_ashmem_buffer(size_t size, unsigned int postfix, void** pBase,
            int* pOffset, int* pFd)
{
    int err = 0;
    int fd = -1;
    void* base = 0;
    int offset = 0;

    char name[ASHMEM_NAME_LEN];
    snprintf(name, ASHMEM_NAME_LEN, "gralloc-buffer-%x", postfix);
    int prot = PROT_READ | PROT_WRITE;
    fd = ashmem_create_region(name, size);
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
            base = mmap(0, size, prot, MAP_SHARED|MAP_POPULATE|MAP_LOCKED, fd, 0);
            if (base == MAP_FAILED) {
                LOGE("alloc mmap(fd=%d, size=%d, prot=%x) failed (%s)",
                     fd, size, prot, strerror(errno));
                close(fd);
                err = -errno;
            } else {
                memset((char*)base + offset, 0, size);
            }
        }
    }
    if(err == 0) {
        *pFd = fd;
        *pBase = base;
        *pOffset = offset;
    }
    return err;
}

int gpu_context_t::gralloc_alloc_buffer(size_t size, int usage, buffer_handle_t* pHandle)
{
    int err = 0;
    int flags = 0;

    int fd = -1;
    void* base = 0; // XXX JMG: This should change to just get an address from
                    // the PmemAllocator rather than getting the base & offset separately
    int offset = 0;
    int lockState = 0;

    size = roundUpToPageSize(size);
#ifndef USE_ASHMEM
    if (usage & GRALLOC_USAGE_HW_TEXTURE) {
        // enable pmem in that case, so our software GL can fallback to
        // the copybit module.
        flags |= private_handle_t::PRIV_FLAGS_USES_PMEM;
    }

    if (usage & GRALLOC_USAGE_HW_2D) {
        flags |= private_handle_t::PRIV_FLAGS_USES_PMEM;
    }
#else
    if (usage & GRALLOC_USAGE_PRIVATE_PMEM){
        flags |= private_handle_t::PRIV_FLAGS_USES_PMEM;
    }
#endif
    if (usage & GRALLOC_USAGE_PRIVATE_PMEM_ADSP) {
        flags |= private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP;
        flags &= ~private_handle_t::PRIV_FLAGS_USES_PMEM;
    }

    private_module_t* m = reinterpret_cast<private_module_t*>(common.module);
    if((flags & private_handle_t::PRIV_FLAGS_USES_PMEM) == 0 &&
       (flags & private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP) == 0) {
       flags |= private_handle_t::PRIV_FLAGS_USES_ASHMEM;
       err = alloc_ashmem_buffer(size, (unsigned int)pHandle, &base, &offset, &fd);
       if(err >= 0)
            lockState |= private_handle_t::LOCK_STATE_MAPPED; 
    }
    else if ((flags & private_handle_t::PRIV_FLAGS_USES_PMEM) != 0 ||
        (flags & private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP) != 0) {

        PmemAllocator* pma = 0;

        if ((flags & private_handle_t::PRIV_FLAGS_USES_PMEM) != 0) {
          if ((flags & private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP) != 0) {
              LOGE("attempting to allocate a gralloc buffer with both the "
                   "USES_PMEM and USES_PMEM_ADSP flags.  Unsetting the "
                   "USES_PMEM_ADSP flag.");
              flags &= ~private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP;
          }
          pma = &pmemAllocator;
        } else { // (flags & private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP) != 0
          pma = &pmemAdspAllocator;
        }

        // PMEM buffers are always mmapped
        lockState |= private_handle_t::LOCK_STATE_MAPPED;

        // Allocate the buffer from pmem
        err = pma->alloc_pmem_buffer(size, usage, &base, &offset, &fd);
        if (err < 0) {
            if (((usage & GRALLOC_USAGE_HW_MASK) == 0) &&
                ((usage & GRALLOC_USAGE_PRIVATE_PMEM_ADSP) == 0)) {
                // the caller didn't request PMEM, so we can try something else
                flags &= ~private_handle_t::PRIV_FLAGS_USES_PMEM;
                err = 0;
                goto try_ashmem;
            } else {
                LOGE("couldn't open pmem (%s)", strerror(errno));
            }
        }
    } else {
try_ashmem:
        fd = deps.ashmem_create_region("gralloc-buffer", size);
        if (fd < 0) {
            LOGE("couldn't create ashmem (%s)", strerror(errno));
            err = -errno;
        }
    }

    if (err == 0) {
        private_handle_t* hnd = new private_handle_t(fd, size, flags);
        hnd->offset = offset;
        hnd->base = int(base)+offset;
        hnd->lockState = lockState;
        *pHandle = hnd;
    }

    LOGE_IF(err, "gralloc failed err=%s", strerror(-err));

    return err;
}

static inline size_t ALIGN(size_t x, size_t align) {
    return (x + align-1) & ~(align-1);
}

int gpu_context_t::alloc_impl(int w, int h, int format, int usage,
        buffer_handle_t* pHandle, int* pStride) {
    if (!pHandle || !pStride)
        return -EINVAL;

    size_t size, alignedw, alignedh;

    alignedw = ALIGN(w, 32);
    alignedh = ALIGN(h, 32);
    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
            size = alignedw * alignedh * 4;
            break;
        case HAL_PIXEL_FORMAT_RGB_888:
            size = alignedw * alignedh * 3;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_RGBA_5551:
        case HAL_PIXEL_FORMAT_RGBA_4444:
            size = alignedw * alignedh * 2;
            break;

        // adreno formats
        case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO:  // NV21
            size  = ALIGN(alignedw*alignedh, 4096);
            size += ALIGN(2 * ALIGN(w/2, 32) * ALIGN(h/2, 32), 4096);
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:   // NV12
            // The chroma plane is subsampled,
            // but the pitch in bytes is unchanged
            // The GPU needs 4K alignment, but the video decoder needs 8K
            alignedw = ALIGN(w, 128);
            size  = ALIGN( alignedw * alignedh, 8192);
            size += ALIGN( alignedw * ALIGN(h/2, 32), 4096);
            break;

        case HAL_PIXEL_FORMAT_YV12:
            if ((w&1) || (h&1)) {
                LOGE("w or h is odd for HAL_PIXEL_FORMAT_YV12");
                return -EINVAL;
            }
            alignedw = ALIGN(w, 16);
            alignedh = h;
            size = alignedw*alignedh +
                    (ALIGN(alignedw/2, 16) * (alignedh/2))*2;
            break;

        default:
            LOGE("unrecognized pixel format: %d", format);
            return -EINVAL;
    }

    if ((ssize_t)size <= 0)
        return -EINVAL;

    int err;
    if (usage & GRALLOC_USAGE_HW_FB) {
        err = gralloc_alloc_framebuffer(size, usage, pHandle);
    } else {
        err = gralloc_alloc_buffer(size, usage, pHandle);
    }

    if (err < 0) {
        return err;
    }

    *pStride = alignedw;
    return 0;
}

int gpu_context_t::free_impl(private_handle_t const* hnd) {
    private_module_t* m = reinterpret_cast<private_module_t*>(common.module);
    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
        // free this buffer
        const size_t bufferSize = m->finfo.line_length * m->info.yres;
        int index = (hnd->base - m->framebuffer->base) / bufferSize;
        m->bufferMask &= ~(1<<index); 
    } else { 
        PmemAllocator* pmem_allocator = 0;
        if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_PMEM) {
            pmem_allocator = &pmemAllocator;
        } else if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP) {
            pmem_allocator = &pmemAdspAllocator;
        }
        if (pmem_allocator) {
            pmem_allocator->free_pmem_buffer(hnd->size, (void*)hnd->base,
                    hnd->offset, hnd->fd);
        }
        deps.terminateBuffer(&m->base, const_cast<private_handle_t*>(hnd));
    }

    deps.close(hnd->fd);
    delete hnd; // XXX JMG: move this to the deps
    return 0;
}

/******************************************************************************
 * Static functions
 *****************************************************************************/

int gpu_context_t::gralloc_alloc(alloc_device_t* dev, int w, int h, int format,
        int usage, buffer_handle_t* pHandle, int* pStride)
{
    if (!dev) {
        return -EINVAL;
    }
    gpu_context_t* gpu = reinterpret_cast<gpu_context_t*>(dev);
    return gpu->alloc_impl(w, h, format, usage, pHandle, pStride);
}

int gpu_context_t::gralloc_free(alloc_device_t* dev,
                                    buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    private_handle_t const* hnd = reinterpret_cast<private_handle_t const*>(handle);
    gpu_context_t* gpu = reinterpret_cast<gpu_context_t*>(dev);
    return gpu->free_impl(hnd);
}

/*****************************************************************************/

int gpu_context_t::gralloc_close(struct hw_device_t *dev)
{
    gpu_context_t* ctx = reinterpret_cast<gpu_context_t*>(dev);
    if (ctx) {
        /* TODO: keep a list of all buffer_handle_t created, and free them
         * all here.
         */
        delete ctx;
    }
    return 0;
}


gpu_context_t::Deps::~Deps() {}
