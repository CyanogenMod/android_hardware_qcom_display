/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
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
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/ashmem.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/ashmem.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <linux/android_pmem.h>

#include "gralloc_priv.h"
#include "gr.h"
#include "alloc_controller.h"
#include "memalloc.h"
#include <qdMetaData.h>

using namespace gralloc;
/*****************************************************************************/

// Return the type of allocator -
// these are used for mapping/unmapping
static IMemAlloc* getAllocator(int flags)
{
    IMemAlloc* memalloc;
    IAllocController* alloc_ctrl = IAllocController::getInstance();
    memalloc = alloc_ctrl->getAllocator(flags);
    return memalloc;
}

static int gralloc_map(gralloc_module_t const* module,
                       buffer_handle_t handle,
                       void** vaddr)
{
    private_handle_t* hnd = (private_handle_t*)handle;
    void *mappedAddress;
    if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) &&
        !(hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER)) {
        size_t size = hnd->size;
        IMemAlloc* memalloc = getAllocator(hnd->flags) ;
        int err = memalloc->map_buffer(&mappedAddress, size,
                                       hnd->offset, hnd->fd);
        if(err || mappedAddress == MAP_FAILED) {
            ALOGE("Could not mmap handle %p, fd=%d (%s)",
                  handle, hnd->fd, strerror(errno));
            hnd->base = 0;
            return -errno;
        }

        hnd->base = intptr_t(mappedAddress) + hnd->offset;
        //LOGD("gralloc_map() succeeded fd=%d, off=%d, size=%d, vaddr=%p",
        //        hnd->fd, hnd->offset, hnd->size, mappedAddress);
        mappedAddress = MAP_FAILED;
        size = ROUND_UP_PAGESIZE(sizeof(MetaData_t));
        err = memalloc->map_buffer(&mappedAddress, size,
                                       hnd->offset_metadata, hnd->fd_metadata);
        if(err || mappedAddress == MAP_FAILED) {
            ALOGE("Could not mmap handle %p, fd=%d (%s)",
                  handle, hnd->fd_metadata, strerror(errno));
            hnd->base_metadata = 0;
            return -errno;
        }
        hnd->base_metadata = intptr_t(mappedAddress) + hnd->offset_metadata;
    }
    *vaddr = (void*)hnd->base;
    return 0;
}

static int gralloc_unmap(gralloc_module_t const* module,
                         buffer_handle_t handle)
{
    private_handle_t* hnd = (private_handle_t*)handle;
    if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
        int err = -EINVAL;
        void* base = (void*)hnd->base;
        size_t size = hnd->size;
        IMemAlloc* memalloc = getAllocator(hnd->flags) ;
        if(memalloc != NULL) {
            err = memalloc->unmap_buffer(base, size, hnd->offset);
            if (err) {
                ALOGE("Could not unmap memory at address %p", base);
            }
            base = (void*)hnd->base_metadata;
            size = ROUND_UP_PAGESIZE(sizeof(MetaData_t));
            err = memalloc->unmap_buffer(base, size, hnd->offset_metadata);
            if (err) {
                ALOGE("Could not unmap memory at address %p", base);
            }
        }
    }
    /* need to initialize the pointer to NULL otherwise unmapping for that
     * buffer happens twice which leads to crash */
    hnd->base = 0;
    hnd->base_metadata = 0;
    return 0;
}

/*****************************************************************************/

static pthread_mutex_t sMapLock = PTHREAD_MUTEX_INITIALIZER;

/*****************************************************************************/

int gralloc_register_buffer(gralloc_module_t const* module,
                            buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    // In this implementation, we don't need to do anything here

    /* NOTE: we need to initialize the buffer as not mapped/not locked
     * because it shouldn't when this function is called the first time
     * in a new process. Ideally these flags shouldn't be part of the
     * handle, but instead maintained in the kernel or at least
     * out-of-line
     */

    private_handle_t* hnd = (private_handle_t*)handle;
    hnd->base = 0;
    hnd->base_metadata = 0;
    void *vaddr;
    int err = gralloc_map(module, handle, &vaddr);
    if (err) {
        ALOGE("%s: gralloc_map failed", __FUNCTION__);
        return err;
    }

    return 0;
}

int gralloc_unregister_buffer(gralloc_module_t const* module,
                              buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    /*
     * If the buffer has been mapped during a lock operation, it's time
     * to un-map it. It's an error to be here with a locked buffer.
     * NOTE: the framebuffer is handled differently and is never unmapped.
     */

    private_handle_t* hnd = (private_handle_t*)handle;

    if (hnd->base != 0) {
        gralloc_unmap(module, handle);
    }
    hnd->base = 0;
    hnd->base_metadata = 0;
    return 0;
}

int terminateBuffer(gralloc_module_t const* module,
                    private_handle_t* hnd)
{
    /*
     * If the buffer has been mapped during a lock operation, it's time
     * to un-map it. It's an error to be here with a locked buffer.
     */

    if (hnd->base != 0) {
        // this buffer was mapped, unmap it now
        if (hnd->flags & (private_handle_t::PRIV_FLAGS_USES_PMEM |
                          private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP |
                          private_handle_t::PRIV_FLAGS_USES_ASHMEM |
                          private_handle_t::PRIV_FLAGS_USES_ION)) {
                gralloc_unmap(module, hnd);
        } else {
            ALOGE("terminateBuffer: unmapping a non pmem/ashmem buffer flags = 0x%x",
                  hnd->flags);
            gralloc_unmap(module, hnd);
        }
    }

    return 0;
}

int gralloc_lock(gralloc_module_t const* module,
                 buffer_handle_t handle, int usage,
                 int l, int t, int w, int h,
                 void** vaddr)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    int err = 0;
    private_handle_t* hnd = (private_handle_t*)handle;
    if (usage & (GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK)) {
        if (hnd->base == 0) {
            // we need to map for real
            pthread_mutex_t* const lock = &sMapLock;
            pthread_mutex_lock(lock);
            err = gralloc_map(module, handle, vaddr);
            pthread_mutex_unlock(lock);
        }
        *vaddr = (void*)hnd->base;
        if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_ION and
                    not useUncached(usage)) {
            bool nonCPUWriters = usage & (
                        GRALLOC_USAGE_HW_RENDER |
                        GRALLOC_USAGE_HW_FB |
                        GRALLOC_USAGE_HW_VIDEO_ENCODER |
                        GRALLOC_USAGE_HW_CAMERA_WRITE);

            //Invalidate if CPU reads in software and there are non-CPU
            //writers. No need to do this for the metadata buffer as it is
            //only read/written in software.
            //Corner case: If we reach here with a READ_RARELY, then there must
            //be a WRITE_OFTEN that caused caching to be used.
            if ((usage & GRALLOC_USAGE_SW_READ_MASK) and nonCPUWriters) {
                IMemAlloc* memalloc = getAllocator(hnd->flags) ;
                err = memalloc->clean_buffer((void*)hnd->base,
                        hnd->size, hnd->offset, hnd->fd,
                        CACHE_INVALIDATE);
            }
            //Mark the buffer to be flushed after CPU write.
            //Corner case: If we reach here with a WRITE_RARELY, then there
            //must be a READ_OFTEN that caused caching to be used.
            if (usage & GRALLOC_USAGE_SW_WRITE_MASK) {
                hnd->flags |= private_handle_t::PRIV_FLAGS_NEEDS_FLUSH;
            }
        }
    }

    if(useUncached(usage))
        hnd->flags |= private_handle_t::PRIV_FLAGS_DO_NOT_FLUSH;

    return err;
}

int gralloc_unlock(gralloc_module_t const* module,
                   buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;
    int err = 0;
    private_handle_t* hnd = (private_handle_t*)handle;

    IMemAlloc* memalloc = getAllocator(hnd->flags);
    if (hnd->flags & private_handle_t::PRIV_FLAGS_NEEDS_FLUSH) {
        err = memalloc->clean_buffer((void*)hnd->base,
                hnd->size, hnd->offset, hnd->fd,
                CACHE_CLEAN);
        hnd->flags &= ~private_handle_t::PRIV_FLAGS_NEEDS_FLUSH;
    }

    if(hnd->flags & private_handle_t::PRIV_FLAGS_DO_NOT_FLUSH)
            hnd->flags &= ~private_handle_t::PRIV_FLAGS_DO_NOT_FLUSH;

    return err;
}

/*****************************************************************************/

int gralloc_perform(struct gralloc_module_t const* module,
                    int operation, ... )
{
    int res = -EINVAL;
    va_list args;
    va_start(args, operation);
    switch (operation) {
        case GRALLOC_MODULE_PERFORM_CREATE_HANDLE_FROM_BUFFER:
            {
                int fd = va_arg(args, int);
                size_t size = va_arg(args, size_t);
                size_t offset = va_arg(args, size_t);
                void* base = va_arg(args, void*);
                int width = va_arg(args, int);
                int height = va_arg(args, int);
                int format = va_arg(args, int);

                native_handle_t** handle = va_arg(args, native_handle_t**);
                int memoryFlags = va_arg(args, int);
                private_handle_t* hnd = (private_handle_t*)native_handle_create(
                    private_handle_t::sNumFds, private_handle_t::sNumInts);
                hnd->magic = private_handle_t::sMagic;
                hnd->fd = fd;
                hnd->flags =  private_handle_t::PRIV_FLAGS_USES_ION;
                hnd->size = size;
                hnd->offset = offset;
                hnd->base = intptr_t(base) + offset;
                hnd->gpuaddr = 0;
                hnd->width = width;
                hnd->height = height;
                hnd->format = format;
                *handle = (native_handle_t *)hnd;
                res = 0;
                break;

            }
        case GRALLOC_MODULE_PERFORM_GET_STRIDE:
            {
                int width   = va_arg(args, int);
                int format  = va_arg(args, int);
                int *stride = va_arg(args, int *);
                int alignedw = 0, alignedh = 0;
                AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(width,
                                     0, format, alignedw, alignedh);
                *stride = alignedw;
                res = 0;
            } break;
        case GRALLOC_MODULE_PERFORM_GET_CUSTOM_STRIDE_AND_HEIGHT_FROM_HANDLE:
            {
                private_handle_t* hnd =  va_arg(args, private_handle_t*);
                int *stride = va_arg(args, int *);
                int *height = va_arg(args, int *);
                if (private_handle_t::validate(hnd)) {
                    return res;
                }
                MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
                if(metadata && metadata->operation & UPDATE_BUFFER_GEOMETRY) {
                    *stride = metadata->bufferDim.sliceWidth;
                    *height = metadata->bufferDim.sliceHeight;
                } else {
                    *stride = hnd->width;
                    *height = hnd->height;
                }
                res = 0;
            } break;
        default:
            break;
    }
    va_end(args);
    return res;
}
