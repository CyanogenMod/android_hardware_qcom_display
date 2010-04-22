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

#include <unistd.h>
#include <fcntl.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <linux/android_pmem.h>

#include "allocator.h"
#include "gr.h"
#include "gpu.h"

/*****************************************************************************/

static int gralloc_alloc_buffer(alloc_device_t* dev,
        size_t size, int usage, buffer_handle_t* pHandle);

/*****************************************************************************/

int fb_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device);

static int gralloc_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device);

extern int gralloc_lock(gralloc_module_t const* module,
        buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        void** vaddr);

extern int gralloc_unlock(gralloc_module_t const* module, 
        buffer_handle_t handle);

extern int gralloc_register_buffer(gralloc_module_t const* module,
        buffer_handle_t handle);

extern int gralloc_unregister_buffer(gralloc_module_t const* module,
        buffer_handle_t handle);

extern int gralloc_perform(struct gralloc_module_t const* module,
        int operation, ... );

/*****************************************************************************/

/* On-device dependency implementation */
class PmemAllocatorDepsDeviceImpl : public PmemUserspaceAllocator::Deps,
        public PmemKernelAllocator::Deps {

    virtual size_t getPmemTotalSize(int fd, size_t* size) {
        pmem_region region;
        int err = ioctl(fd, PMEM_GET_TOTAL_SIZE, &region);
        if (err == 0) {
            *size = region.len;
        }
        return err;
    }

    virtual int connectPmem(int fd, int master_fd) {
        return ioctl(fd, PMEM_CONNECT, master_fd);
    }

    virtual int mapPmem(int fd, int offset, size_t size) {
        struct pmem_region sub = { offset, size };
        return ioctl(fd, PMEM_MAP, &sub);
    }

    virtual int unmapPmem(int fd, int offset, size_t size) {
        struct pmem_region sub = { offset, size };
        return ioctl(fd, PMEM_UNMAP, &sub);
    }

    virtual int getErrno() {
        return errno;
    }

    virtual void* mmap(void* start, size_t length, int prot, int flags, int fd,
            off_t offset) {
        return ::mmap(start, length, prot, flags, fd, offset);
    }

    virtual int munmap(void* start, size_t length) {
        return ::munmap(start, length);
    }

    virtual int open(const char* pathname, int flags, int mode) {
        return ::open(pathname, flags, mode);
    }

    virtual int close(int fd) {
        return ::close(fd);
    }
};

class GpuContextDepsDeviceImpl : public gpu_context_t::Deps {

 public:

    virtual int ashmem_create_region(const char *name, size_t size) {
        return ::ashmem_create_region(name, size);
    }

    virtual int mapFrameBufferLocked(struct private_module_t* module) {
        return ::mapFrameBufferLocked(module);
    }

    virtual int terminateBuffer(gralloc_module_t const* module,
            private_handle_t* hnd) {
        return ::terminateBuffer(module, hnd);
    }

    virtual int close(int fd) {
        return ::close(fd);
    }
};

static PmemAllocatorDepsDeviceImpl pmemAllocatorDeviceDepsImpl;
static GpuContextDepsDeviceImpl gpuContextDeviceDepsImpl;

/*****************************************************************************/

static SimpleBestFitAllocator pmemAllocMgr;
static PmemUserspaceAllocator pmemAllocator(pmemAllocatorDeviceDepsImpl, pmemAllocMgr,
        "/dev/pmem");

static PmemKernelAllocator pmemAdspAllocator(pmemAllocatorDeviceDepsImpl,
        "/dev/pmem_adsp");

/*****************************************************************************/

static struct hw_module_methods_t gralloc_module_methods = {
    open: gralloc_device_open
};

struct private_module_t HAL_MODULE_INFO_SYM = {
    base: {
        common: {
            tag: HARDWARE_MODULE_TAG,
            version_major: 1,
            version_minor: 0,
            id: GRALLOC_HARDWARE_MODULE_ID,
            name: "Graphics Memory Allocator Module",
            author: "The Android Open Source Project",
            methods: &gralloc_module_methods
        },
        registerBuffer: gralloc_register_buffer,
        unregisterBuffer: gralloc_unregister_buffer,
        lock: gralloc_lock,
        unlock: gralloc_unlock,
        perform: gralloc_perform,
    },
    framebuffer: 0,
    fbFormat: 0,
    flags: 0,
    numBuffers: 0,
    bufferMask: 0,
    lock: PTHREAD_MUTEX_INITIALIZER,
    currentBuffer: 0,
};

/*****************************************************************************/

int gralloc_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device)
{
    int status = -EINVAL;
    if (!strcmp(name, GRALLOC_HARDWARE_GPU0)) {
        const private_module_t* m = reinterpret_cast<const private_module_t*>(
                module);
        gpu_context_t *dev;
        dev = new gpu_context_t(gpuContextDeviceDepsImpl, pmemAllocator,
                pmemAdspAllocator, m);
        *device = &dev->common;
        status = 0;
    } else {
        status = fb_device_open(module, name, device);
    }
    return status;
}
