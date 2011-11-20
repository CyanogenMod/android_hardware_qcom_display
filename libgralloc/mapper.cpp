/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2011 Code Aurora Forum. All rights reserved.
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
using android::sp;
/*****************************************************************************/

// Return the type of allocator -
// these are used for mapping/unmapping
static sp<IMemAlloc> getAllocator(int flags)
{
    sp<IMemAlloc> memalloc;
    sp<IAllocController> alloc_ctrl = IAllocController::getInstance(true);
    memalloc = alloc_ctrl->getAllocator(flags);
    return memalloc;
}

static int gralloc_map(gralloc_module_t const* module,
        buffer_handle_t handle,
        void** vaddr)
{
    private_handle_t* hnd = (private_handle_t*)handle;
    void *mappedAddress;
    if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
        size_t size = hnd->size;
        sp<IMemAlloc> memalloc = getAllocator(hnd->flags) ;
        int err = memalloc->map_buffer(&mappedAddress, size,
                hnd->offset, hnd->fd);
        if(err) {
            LOGE("Could not mmap handle %p, fd=%d (%s)",
                    handle, hnd->fd, strerror(errno));
            hnd->base = 0;
            return -errno;
        }

        if (mappedAddress == MAP_FAILED) {
            LOGE("Could not mmap handle %p, fd=%d (%s)",
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
        sp<IMemAlloc> memalloc = getAllocator(hnd->flags) ;
        if(memalloc != NULL)
            err = memalloc->unmap_buffer(base, size, hnd->offset);
        if (err) {
            LOGE("Could not unmap memory at address %p", base);
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

    // if this handle was created in this process, then we keep it as is.
    private_handle_t* hnd = (private_handle_t*)handle;
    if (hnd->pid != getpid()) {
        hnd->base = 0;
        hnd->lockState  = 0;
        hnd->writeOwner = 0;
        // Reset the genlock private fd flag in the handle
        hnd->genlockPrivFd = -1;

        // Check if there is a valid lock attached to the handle.
        if (-1 == hnd->genlockHandle) {
            LOGE("%s: the lock is invalid.", __FUNCTION__);
            return -EINVAL;
        }

        // Attach the genlock handle
        return genlock_attach_lock((native_handle_t *)handle);
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

    LOGE_IF(hnd->lockState & private_handle_t::LOCK_STATE_READ_MASK,
            "[unregister] handle %p still locked (state=%08x)",
            hnd, hnd->lockState);

    // never unmap buffers that were created in this process
    if (hnd->pid != getpid()) {
        if (hnd->lockState & private_handle_t::LOCK_STATE_MAPPED) {
            gralloc_unmap(module, handle);
        }
        hnd->base = 0;
        hnd->lockState  = 0;
        hnd->writeOwner = 0;

        // Release the genlock
        if (-1 != hnd->genlockHandle) {
            return genlock_release_lock((native_handle_t *)handle);
        } else {
            LOGE("%s: there was no genlock attached to this buffer", __FUNCTION__);
            return -EINVAL;
        }
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

    LOGE_IF(hnd->lockState & private_handle_t::LOCK_STATE_READ_MASK,
            "[terminate] handle %p still locked (state=%08x)",
            hnd, hnd->lockState);

    if (hnd->lockState & private_handle_t::LOCK_STATE_MAPPED) {
        // this buffer was mapped, unmap it now
        if (hnd->flags & (private_handle_t::PRIV_FLAGS_USES_PMEM |
                          private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP |
                          private_handle_t::PRIV_FLAGS_USES_ASHMEM |
                          private_handle_t::PRIV_FLAGS_USES_ION)) {
            if (hnd->pid != getpid()) {
                // ... unless it's a "master" pmem buffer, that is a buffer
                // mapped in the process it's been allocated.
                // (see gralloc_alloc_buffer())
                gralloc_unmap(module, hnd);
            }
        } else {
            LOGE("terminateBuffer: unmapping a non pmem/ashmem buffer flags = 0x%x", hnd->flags);
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
    int32_t current_value, new_value;
    int retry;

    do {
        current_value = hnd->lockState;
        new_value = current_value;

        if (current_value & private_handle_t::LOCK_STATE_WRITE) {
            // already locked for write
            LOGE("handle %p already locked for write", handle);
            return -EBUSY;
        } else if (current_value & private_handle_t::LOCK_STATE_READ_MASK) {
            // already locked for read
            if (usage & (GRALLOC_USAGE_SW_WRITE_MASK | GRALLOC_USAGE_HW_RENDER)) {
                LOGE("handle %p already locked for read", handle);
                return -EBUSY;
            } else {
                // this is not an error
                //LOGD("%p already locked for read... count = %d",
                //        handle, (current_value & ~(1<<31)));
            }
        }

        // not currently locked
        if (usage & (GRALLOC_USAGE_SW_WRITE_MASK | GRALLOC_USAGE_HW_RENDER)) {
            // locking for write
            new_value |= private_handle_t::LOCK_STATE_WRITE;
        }
        new_value++;

        retry = android_atomic_cmpxchg(current_value, new_value,
                (volatile int32_t*)&hnd->lockState);
    } while (retry);

    if (new_value & private_handle_t::LOCK_STATE_WRITE) {
        // locking for write, store the tid
        hnd->writeOwner = gettid();
    }

    // if requesting sw write for non-framebuffer handles, flag for
    // flushing at unlock

    if ((usage & GRALLOC_USAGE_SW_WRITE_MASK) &&
            !(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
        hnd->flags |= private_handle_t::PRIV_FLAGS_NEEDS_FLUSH;
    }

    if (usage & (GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK)) {
        if (!(current_value & private_handle_t::LOCK_STATE_MAPPED)) {
            // we need to map for real
            pthread_mutex_t* const lock = &sMapLock;
            pthread_mutex_lock(lock);
            if (!(hnd->lockState & private_handle_t::LOCK_STATE_MAPPED)) {
                err = gralloc_map(module, handle, vaddr);
                if (err == 0) {
                    android_atomic_or(private_handle_t::LOCK_STATE_MAPPED,
                            (volatile int32_t*)&(hnd->lockState));
                }
            }
            pthread_mutex_unlock(lock);
        }
        *vaddr = (void*)hnd->base;
    }

    return err;
}

int gralloc_unlock(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    private_handle_t* hnd = (private_handle_t*)handle;
    int32_t current_value, new_value;

    if (hnd->flags & private_handle_t::PRIV_FLAGS_NEEDS_FLUSH) {
        int err;
        sp<IMemAlloc> memalloc = getAllocator(hnd->flags) ;
        err = memalloc->clean_buffer((void*)hnd->base,
                hnd->size, hnd->offset, hnd->fd);
        LOGE_IF(err < 0, "cannot flush handle %p (offs=%x len=%x, flags = 0x%x) err=%s\n",
                hnd, hnd->offset, hnd->size, hnd->flags, strerror(errno));
        hnd->flags &= ~private_handle_t::PRIV_FLAGS_NEEDS_FLUSH;
    }

    do {
        current_value = hnd->lockState;
        new_value = current_value;

        if (current_value & private_handle_t::LOCK_STATE_WRITE) {
            // locked for write
            if (hnd->writeOwner == gettid()) {
                hnd->writeOwner = 0;
                new_value &= ~private_handle_t::LOCK_STATE_WRITE;
            }
        }

        if ((new_value & private_handle_t::LOCK_STATE_READ_MASK) == 0) {
            LOGE("handle %p not locked", handle);
            return -EINVAL;
        }

        new_value--;

    } while (android_atomic_cmpxchg(current_value, new_value,
                (volatile int32_t*)&hnd->lockState));

    return 0;
}

/*****************************************************************************/

int gralloc_perform(struct gralloc_module_t const* module,
        int operation, ... )
{
    int res = -EINVAL;
    va_list args;
    va_start(args, operation);
#if 0
    switch (operation) {
        case GRALLOC_MODULE_PERFORM_CREATE_HANDLE_FROM_BUFFER:
            {
                int fd = va_arg(args, int);
                size_t size = va_arg(args, size_t);
                size_t offset = va_arg(args, size_t);
                void* base = va_arg(args, void*);

                native_handle_t** handle = va_arg(args, native_handle_t**);
                int memoryFlags = va_arg(args, int);
                private_handle_t* hnd = (private_handle_t*)native_handle_create(
                        private_handle_t::sNumFds, private_handle_t::sNumInts);
                hnd->magic = private_handle_t::sMagic;
                hnd->fd = fd;
                unsigned int contigFlags = GRALLOC_USAGE_PRIVATE_ADSP_HEAP |
                                  GRALLOC_USAGE_PRIVATE_EBI_HEAP |
                                  GRALLOC_USAGE_PRIVATE_SMI_HEAP;

                if (memoryFlags & contigFlags) {
                    // check if the buffer is a pmem buffer
                    pmem_region region;
                    if (ioctl(fd, PMEM_GET_SIZE, &region) < 0)
                        hnd->flags =  private_handle_t::PRIV_FLAGS_USES_ION;
                    else
                        hnd->flags =  private_handle_t::PRIV_FLAGS_USES_PMEM |
                                      private_handle_t::PRIV_FLAGS_DO_NOT_FLUSH;
                } else {
                    if (memoryFlags & GRALLOC_USAGE_PRIVATE_ION)
                        hnd->flags =  private_handle_t::PRIV_FLAGS_USES_ION;
                    else
                        hnd->flags =  private_handle_t::PRIV_FLAGS_USES_ASHMEM;
                }

                hnd->size = size;
                hnd->offset = offset;
                hnd->base = intptr_t(base) + offset;
                hnd->lockState = private_handle_t::LOCK_STATE_MAPPED;
                hnd->gpuaddr = 0;
                *handle = (native_handle_t *)hnd;
                res = 0;
                break;

            }
        case GRALLOC_MODULE_PERFORM_UPDATE_BUFFER_HANDLE:
            {
                native_handle_t* handle = va_arg(args, native_handle_t*);
                int w = va_arg(args, int);
                int h = va_arg(args, int);
                int f = va_arg(args, int);
                private_handle_t* hnd = (private_handle_t*)handle;
                hnd->width = w;
                hnd->height = h;
                if (hnd->format != f) {
                    hnd->format = f;
                }
                break;
            }
        default:
            break;
    }
#endif
    va_end(args);
    return res;
}
