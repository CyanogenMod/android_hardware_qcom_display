/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
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
#include <genlock.h>

#include <linux/android_pmem.h>

#include "gralloc_priv.h"
#include "gr.h"
#include "alloc_controller.h"
#include "memalloc.h"

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
        if(err) {
            ALOGE("Could not mmap handle %p, fd=%d (%s)",
                  handle, hnd->fd, strerror(errno));
            hnd->base = 0;
            return -errno;
        }

        if (mappedAddress == MAP_FAILED) {
            ALOGE("Could not mmap handle %p, fd=%d (%s)",
                  handle, hnd->fd, strerror(errno));
            hnd->base = 0;
            return -errno;
        }
        hnd->base = intptr_t(mappedAddress) + hnd->offset;
        //LOGD("gralloc_map() succeeded fd=%d, off=%d, size=%d, vaddr=%p",
        //        hnd->fd, hnd->offset, hnd->size, mappedAddress);
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
        if(memalloc != NULL)
            err = memalloc->unmap_buffer(base, size, hnd->offset);
        if (err) {
            ALOGE("Could not unmap memory at address %p", base);
        }
    }
    hnd->base = 0;
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
    void *vaddr;
    int err = gralloc_map(module, handle, &vaddr);
    if (err) {
        ALOGE("%s: gralloc_map failed", __FUNCTION__);
        return err;
    }

    // Reset the genlock private fd flag in the handle
    hnd->genlockPrivFd = -1;

    // Check if there is a valid lock attached to the handle.
    if (-1 == hnd->genlockHandle) {
        ALOGE("%s: the lock is invalid.", __FUNCTION__);
        gralloc_unmap(module, handle);
        hnd->base = 0;
        return -EINVAL;
    }

    // Attach the genlock handle
    if (GENLOCK_NO_ERROR != genlock_attach_lock((native_handle_t *)handle)) {
        ALOGE("%s: genlock_attach_lock failed", __FUNCTION__);
        gralloc_unmap(module, handle);
        hnd->base = 0;
        return -EINVAL;
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
    // Release the genlock
    if (-1 != hnd->genlockHandle) {
        return genlock_release_lock((native_handle_t *)handle);
    } else {
        ALOGE("%s: there was no genlock attached to this buffer", __FUNCTION__);
        return -EINVAL;
    }
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

        // Lock the buffer for read/write operation as specified. Write lock
        // has a higher priority over read lock.
        int lockType = 0;
        if (usage & GRALLOC_USAGE_SW_WRITE_MASK) {
            lockType = GENLOCK_WRITE_LOCK;
        } else if (usage & GRALLOC_USAGE_SW_READ_MASK) {
            lockType = GENLOCK_READ_LOCK;
        }

        int timeout = GENLOCK_MAX_TIMEOUT;
        if (GENLOCK_NO_ERROR != genlock_lock_buffer((native_handle_t *)handle,
                                                    (genlock_lock_type)lockType,
                                                    timeout)) {
            ALOGE("%s: genlock_lock_buffer (lockType=0x%x) failed", __FUNCTION__,
                  lockType);
            return -EINVAL;
        } else {
            // Mark this buffer as locked for SW read/write operation.
            hnd->flags |= private_handle_t::PRIV_FLAGS_SW_LOCK;
        }

        if ((usage & GRALLOC_USAGE_SW_WRITE_MASK) &&
            !(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
            // Mark the buffer to be flushed after cpu read/write
            hnd->flags |= private_handle_t::PRIV_FLAGS_NEEDS_FLUSH;
        }
    }
    return err;
}

int gralloc_unlock(gralloc_module_t const* module,
                   buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    private_handle_t* hnd = (private_handle_t*)handle;

    if (hnd->flags & private_handle_t::PRIV_FLAGS_NEEDS_FLUSH) {
        int err;
        IMemAlloc* memalloc = getAllocator(hnd->flags) ;
        err = memalloc->clean_buffer((void*)hnd->base,
                                     hnd->size, hnd->offset, hnd->fd);
        ALOGE_IF(err < 0, "cannot flush handle %p (offs=%x len=%x, flags = 0x%x) err=%s\n",
                 hnd, hnd->offset, hnd->size, hnd->flags, strerror(errno));
        hnd->flags &= ~private_handle_t::PRIV_FLAGS_NEEDS_FLUSH;
    }

    if ((hnd->flags & private_handle_t::PRIV_FLAGS_SW_LOCK)) {
        // Unlock the buffer.
        if (GENLOCK_NO_ERROR != genlock_unlock_buffer((native_handle_t *)handle)) {
            ALOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
            return -EINVAL;
        } else
            hnd->flags &= ~private_handle_t::PRIV_FLAGS_SW_LOCK;
    }
    return 0;
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
        default:
            break;
    }
    va_end(args);
    return res;
}
