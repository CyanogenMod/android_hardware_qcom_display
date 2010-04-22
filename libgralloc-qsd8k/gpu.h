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

#ifndef GRALLOC_QSD8K_GPU_H_
#define GRALLOC_QSD8K_GPU_H_

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <cutils/log.h>
#include <cutils/ashmem.h>

#include "gralloc_priv.h"
#include "pmemalloc.h"


class gpu_context_t : public alloc_device_t {
 public:

    class Deps {
     public:

        virtual ~Deps();

        // ashmem
        virtual int ashmem_create_region(const char *name, size_t size) = 0;

        // POSIX
        virtual int close(int fd) = 0;

        // Framebuffer (locally defined)
        virtual int mapFrameBufferLocked(struct private_module_t* module) = 0;
        virtual int terminateBuffer(gralloc_module_t const* module,
            private_handle_t* hnd) = 0;
    };

    gpu_context_t(Deps& deps, PmemAllocator& pmemAllocator,
            PmemAllocator& pmemAdspAllocator, const private_module_t* module);

    int gralloc_alloc_framebuffer_locked(size_t size, int usage,
            buffer_handle_t* pHandle);
    int gralloc_alloc_framebuffer(size_t size, int usage,
            buffer_handle_t* pHandle);
    int gralloc_alloc_buffer(size_t size, int usage, buffer_handle_t* pHandle);
    int free_impl(private_handle_t const* hnd);
    int alloc_impl(int w, int h, int format, int usage,
            buffer_handle_t* pHandle, int* pStride);

    static int gralloc_alloc(alloc_device_t* dev, int w, int h, int format,
            int usage, buffer_handle_t* pHandle, int* pStride);
    static int gralloc_free(alloc_device_t* dev, buffer_handle_t handle);
    static int gralloc_close(struct hw_device_t *dev);

 private:

    Deps& deps;
    PmemAllocator& pmemAllocator;
    PmemAllocator& pmemAdspAllocator;
};

#endif  // GRALLOC_QSD8K_GPU_H
