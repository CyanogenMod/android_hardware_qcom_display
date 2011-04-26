/*
 * Copyright (C) 2008 The Android Open Source Project
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

// we need this for now because pmem cannot mmap at an offset
#define PMEM_HACK   1

/* desktop Linux needs a little help with gettid() */
#if defined(ARCH_X86) && !defined(HAVE_ANDROID_OS)
#define __KERNEL__
# include <linux/unistd.h>
pid_t gettid() { return syscall(__NR_gettid);}
#undef __KERNEL__
#endif

/*****************************************************************************/

static int gralloc_map(gralloc_module_t const* module,
        buffer_handle_t handle,
        void** vaddr)
{
    private_handle_t* hnd = (private_handle_t*)handle;
    void *mappedAddress;
    if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
        size_t size = hnd->size;
#if PMEM_HACK
        size += hnd->offset;
#endif
        if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_ASHMEM) {
            mappedAddress = mmap(0, size,
                PROT_READ|PROT_WRITE, MAP_SHARED | MAP_POPULATE, hnd->fd, 0);
        } else {
            mappedAddress = mmap(0, size,
                PROT_READ|PROT_WRITE, MAP_SHARED, hnd->fd, 0);
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
        void* base = (void*)hnd->base;
        size_t size = hnd->size;
#if PMEM_HACK
        base = (void*)(intptr_t(base) - hnd->offset);
        size += hnd->offset;
#endif
        //LOGD("unmapping from %p, size=%d", base, size);
        if (munmap(base, size) < 0) {
            LOGE("Could not unmap %s", strerror(errno));
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
        if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_PMEM ||
            hnd->flags & private_handle_t::PRIV_FLAGS_USES_ASHMEM) {
            if (hnd->pid != getpid()) {
                // ... unless it's a "master" pmem buffer, that is a buffer
                // mapped in the process it's been allocated.
                // (see gralloc_alloc_buffer())
                gralloc_unmap(module, hnd);
            }
        } else {
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
        if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_PMEM) {
            struct pmem_addr pmem_addr;
            pmem_addr.vaddr = hnd->base;
            pmem_addr.offset = hnd->offset;
            pmem_addr.length = hnd->size;
            err = ioctl( hnd->fd, PMEM_CLEAN_CACHES,  &pmem_addr);
        } else if ((hnd->flags & private_handle_t::PRIV_FLAGS_USES_ASHMEM)) {
            unsigned long addr = hnd->base + hnd->offset;
            err = ioctl(hnd->fd, ASHMEM_CACHE_FLUSH_RANGE, NULL);
        }         

        LOGE_IF(err < 0, "cannot flush handle %p (offs=%x len=%x)\n",
                hnd, hnd->offset, hnd->size);
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

    switch (operation) {
        case GRALLOC_MODULE_PERFORM_CREATE_HANDLE_FROM_BUFFER: {
            int fd = va_arg(args, int);
            size_t size = va_arg(args, size_t);
            size_t offset = va_arg(args, size_t);
            void* base = va_arg(args, void*);

            native_handle_t** handle = va_arg(args, native_handle_t**);
            int memoryFlags = va_arg(args, int);
            if (memoryFlags == GRALLOC_USAGE_PRIVATE_PMEM) {
                // validate that it's indeed a pmem buffer
                pmem_region region;
                if (ioctl(fd, PMEM_GET_SIZE, &region) < 0) {
                    break;
                }
            }
            private_handle_t* hnd = (private_handle_t*)native_handle_create(
                    private_handle_t::sNumFds, private_handle_t::sNumInts);
            hnd->magic = private_handle_t::sMagic;
            hnd->fd = fd;
            hnd->flags = (memoryFlags == GRALLOC_USAGE_PRIVATE_PMEM) ?
                         private_handle_t::PRIV_FLAGS_USES_PMEM :
                         private_handle_t::PRIV_FLAGS_USES_ASHMEM;
            hnd->size = size;
            hnd->offset = offset;
            hnd->base = intptr_t(base) + offset;
            hnd->lockState = private_handle_t::LOCK_STATE_MAPPED;
            hnd->gpuaddr = 0;
            *handle = (native_handle_t *)hnd;
            res = 0;
            break;
        }
        case GRALLOC_MODULE_PERFORM_DECIDE_PUSH_BUFFER_HANDLING: {
            int format = va_arg(args, int);
            int width = va_arg(args, int);
            int height = va_arg(args, int);
            char *compositionUsed = va_arg(args, char*);
            int hasBlitEngine = va_arg(args, int);
            int *needConversion = va_arg(args, int*);
            int *useBufferDirectly = va_arg(args, int*);
            size_t *size = va_arg(args, size_t*);
            *size = calculateBufferSize(width, height, format);
            int conversion = 0;
            int direct = 0;
            res = decideBufferHandlingMechanism(format, compositionUsed, hasBlitEngine,
                                                needConversion, useBufferDirectly);
	    break;
	}
	default:
	    break;
    }

    va_end(args);
    return res;
}

int decideBufferHandlingMechanism(int format, const char *compositionUsed, int hasBlitEngine,
                                  int *needConversion, int *useBufferDirectly)
{
    *needConversion = FALSE;
    *useBufferDirectly = FALSE;
    if(compositionUsed == NULL) {
        LOGE("null pointer");
        return -1;
    }

    if(format == HAL_PIXEL_FORMAT_RGB_565) {
       // Software video renderer gives the output in RGB565 format.
       // This can be handled by all compositors
       *needConversion = FALSE;
       *useBufferDirectly = TRUE;
    } else if(strncmp(compositionUsed, "cpu", 3) == 0){
        *needConversion = FALSE;
        *useBufferDirectly = FALSE;
    } else if(strncmp(compositionUsed, "gpu", 3) == 0) {
        if(format == HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED
           || format == HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO) {
            *needConversion = FALSE;
            *useBufferDirectly = TRUE;
        } else if(hasBlitEngine) {
            *needConversion = TRUE;
            *useBufferDirectly = FALSE;
        }
    } else if ((strncmp(compositionUsed, "mdp", 3) == 0) ||
               (strncmp(compositionUsed, "c2d", 3) == 0)){
        if(format == HAL_PIXEL_FORMAT_YCbCr_420_SP ||
           format == HAL_PIXEL_FORMAT_YCrCb_420_SP) {
            *needConversion = FALSE;
            *useBufferDirectly = TRUE;
        } else if((strncmp(compositionUsed, "c2d", 3) == 0) &&
           format == HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED) {
           *needConversion = FALSE;
           *useBufferDirectly = TRUE;
        } else if(hasBlitEngine) {
            *needConversion = TRUE;
            *useBufferDirectly = FALSE;
        }
    } else {
        LOGE("Invalid composition type %s", compositionUsed);
        return -1;
    }
    return 0;
}

size_t calculateBufferSize(int width, int height, int format)
{
    if(!width || !height)
        return 0;

    size_t size = 0;

    switch (format)
    {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED: {
            int aligned_height = (height + 31) & ~31;
            int pitch     = (width + 127) & ~127;
            size = pitch * aligned_height;
            size = (size + 8191) & ~8191;
            int secondPlaneOffset = size;

            aligned_height = ((height >> 1) + 31) & ~31;
            size += pitch * aligned_height;
            size = (size + 8191) & ~8191;
            break;
        }
        case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO: {
            int aligned_height = (height + 31) & ~31;
            int pitch = (width + 31) & ~31;
            size = pitch * aligned_height;
            size  = (size + 4095) & ~4095;
            int secondPlaneOffset = size;

            pitch = 2 * (((width >> 1) + 31) & ~31);
            aligned_height = ((height >> 1) + 31) & ~31;
            size += pitch * aligned_height;
            size = (size + 4095) & ~4095;
            break;
        }
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP: {
            /* Camera and video YUV 420 semi-planar buffers are allocated   with
            size equal to w * h * 1.5 */
            int aligned_width = (width + 15) & ~15;
            int aligned_chroma_width = ((width/2) + 15) & ~15;
            size = (aligned_width * height) + ((aligned_chroma_width * height/2) *2);
            break;
        }
        default:
            break;
    }
    return size;
}
