/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (c) 2011-2012 Code Aurora Forum. All rights reserved.
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
#include <cutils/properties.h>
#include <sys/mman.h>

#include <genlock.h>

#include "gr.h"
#include "gpu.h"
#include "memalloc.h"
#include "alloc_controller.h"

using namespace gralloc;
using android::sp;

gpu_context_t::gpu_context_t(const private_module_t* module,
                             sp<IAllocController> alloc_ctrl ) :
    mAllocCtrl(alloc_ctrl)
{
    // Zero out the alloc_device_t
    memset(static_cast<alloc_device_t*>(this), 0, sizeof(alloc_device_t));

    // Initialize the procs
    common.tag     = HARDWARE_DEVICE_TAG;
    common.version = 0;
    common.module  = const_cast<hw_module_t*>(&module->base.common);
    common.close   = gralloc_close;
    alloc          = gralloc_alloc;
    allocSize      = gralloc_alloc_size;
    free           = gralloc_free;

}

int gpu_context_t::gralloc_alloc_framebuffer_locked(size_t size, int usage,
                                                    buffer_handle_t* pHandle)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(common.module);

    // we don't support allocations with both the FB and PMEM_ADSP flags
    if (usage & GRALLOC_USAGE_PRIVATE_ADSP_HEAP) {
        return -EINVAL;
    }

    if (m->framebuffer == NULL) {
        ALOGE("%s: Invalid framebuffer", __FUNCTION__);
        return -EINVAL;
    }

    const uint32_t bufferMask = m->bufferMask;
    const uint32_t numBuffers = m->numBuffers;
    size_t bufferSize = m->finfo.line_length * m->info.yres;

    //adreno needs FB size to be page aligned
    bufferSize = roundUpToPageSize(bufferSize);

    if (numBuffers == 1) {
        // If we have only one buffer, we never use page-flipping. Instead,
        // we return a regular buffer which will be memcpy'ed to the main
        // screen when post is called.
        int newUsage = (usage & ~GRALLOC_USAGE_HW_FB) | GRALLOC_USAGE_HW_2D;
        return gralloc_alloc_buffer(bufferSize, newUsage, pHandle, BUFFER_TYPE_UI,
                                    m->fbFormat, m->info.xres, m->info.yres);
    }

    if (bufferMask >= ((1LU<<numBuffers)-1)) {
        // We ran out of buffers.
        return -ENOMEM;
    }

    // create a "fake" handles for it
    // Set the PMEM flag as well, since adreno
    // treats the FB memory as pmem
    intptr_t vaddr = intptr_t(m->framebuffer->base);
    private_handle_t* hnd = new private_handle_t(dup(m->framebuffer->fd), bufferSize,
                                                 private_handle_t::PRIV_FLAGS_USES_PMEM |
                                                 private_handle_t::PRIV_FLAGS_FRAMEBUFFER,
                                                 BUFFER_TYPE_UI, m->fbFormat, m->info.xres,
                                                 m->info.yres);

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

int gpu_context_t::gralloc_alloc_buffer(size_t size, int usage,
                                        buffer_handle_t* pHandle, int bufferType,
                                        int format, int width, int height)
{
    int err = 0;
    int flags = 0;
    size = roundUpToPageSize(size);
    alloc_data data;
    data.offset = 0;
    data.fd = -1;
    data.base = 0;
    data.size = size;
    if(format == HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED)
        data.align = 8192;
    else
        data.align = getpagesize();
    data.pHandle = (unsigned int) pHandle;
    err = mAllocCtrl->allocate(data, usage, 0);

    if (usage & GRALLOC_USAGE_PRIVATE_UNSYNCHRONIZED) {
        flags |= private_handle_t::PRIV_FLAGS_UNSYNCHRONIZED;
    }

    if (usage & GRALLOC_USAGE_PRIVATE_EXTERNAL_ONLY) {
        flags |= private_handle_t::PRIV_FLAGS_EXTERNAL_ONLY;
        //The EXTERNAL_BLOCK flag is always an add-on
        if (usage & GRALLOC_USAGE_PRIVATE_EXTERNAL_BLOCK) {
            flags |= private_handle_t::PRIV_FLAGS_EXTERNAL_BLOCK;
        }if (usage & GRALLOC_USAGE_PRIVATE_EXTERNAL_CC) {
            flags |= private_handle_t::PRIV_FLAGS_EXTERNAL_CC;
        }
    }

    if (err == 0) {
        flags |= data.allocType;
        private_handle_t* hnd = new private_handle_t(data.fd, size, flags,
                                                     bufferType, format, width,
                                                     height);

        hnd->offset = data.offset;
        hnd->base = int(data.base) + data.offset;
        *pHandle = hnd;
    }

    ALOGE_IF(err, "gralloc failed err=%s", strerror(-err));
    return err;
}

void gpu_context_t::getGrallocInformationFromFormat(int inputFormat,
                                                    int *colorFormat,
                                                    int *bufferType)
{
    *bufferType = BUFFER_TYPE_VIDEO;
    *colorFormat = inputFormat;

#ifdef USES_LEGACY_GRAPHICS
    if (inputFormat < 0x7) {
#else
    // HAL_PIXEL_FORMAT_RGB_888 is MPQ color format for VCAP videos
    // value of RGB_888 is less than 0x7 and this format is not supported
    // by the GPU
    if ((inputFormat < 0x7) && (inputFormat != HAL_PIXEL_FORMAT_RGB_888)) {
#endif
        // RGB formats
        *colorFormat = inputFormat;
        *bufferType = BUFFER_TYPE_UI;
    } else if ((inputFormat == HAL_PIXEL_FORMAT_R_8) ||
               (inputFormat == HAL_PIXEL_FORMAT_RG_88)) {
        *colorFormat = inputFormat;
        *bufferType = BUFFER_TYPE_UI;
    }
}

int gpu_context_t::alloc_impl(int w, int h, int format, int usage,
                              buffer_handle_t* pHandle, int* pStride,
                              size_t bufferSize) {
    if (!pHandle || !pStride)
        return -EINVAL;

    size_t size;
    int alignedw, alignedh;
    int colorFormat, bufferType;
    getGrallocInformationFromFormat(format, &colorFormat, &bufferType);
    size = getBufferSizeAndDimensions(w, h, colorFormat, alignedw, alignedh);

    if ((ssize_t)size <= 0)
        return -EINVAL;
    size = (bufferSize != 0)? bufferSize : size;

    // All buffers marked as protected or for external
    // display need to go to overlay
    if ((usage & GRALLOC_USAGE_EXTERNAL_DISP) ||
        (usage & GRALLOC_USAGE_PROTECTED) ||
        (usage & GRALLOC_USAGE_PRIVATE_CP_BUFFER)) {
        bufferType = BUFFER_TYPE_VIDEO;
    }
    int err;
    if (usage & GRALLOC_USAGE_HW_FB) {
        err = gralloc_alloc_framebuffer(size, usage, pHandle);
    } else {
        err = gralloc_alloc_buffer(size, usage, pHandle, bufferType,
                                   format, alignedw, alignedh);
    }

    if (err < 0) {
        return err;
    }

    // Create a genlock lock for this buffer handle.
    err = genlock_create_lock((native_handle_t*)(*pHandle));
    if (err) {
        ALOGE("%s: genlock_create_lock failed", __FUNCTION__);
        free_impl(reinterpret_cast<private_handle_t*>(pHandle));
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
        terminateBuffer(&m->base, const_cast<private_handle_t*>(hnd));
        sp<IMemAlloc> memalloc = mAllocCtrl->getAllocator(hnd->flags);
        int err = memalloc->free_buffer((void*)hnd->base, (size_t) hnd->size,
                                        hnd->offset, hnd->fd);
        if(err)
            return err;
    }

    // Release the genlock
    int err = genlock_release_lock((native_handle_t*)hnd);
    if (err) {
        ALOGE("%s: genlock_release_lock failed", __FUNCTION__);
    }

    delete hnd;
    return 0;
}

int gpu_context_t::gralloc_alloc(alloc_device_t* dev, int w, int h, int format,
                                 int usage, buffer_handle_t* pHandle,
                                 int* pStride)
{
    if (!dev) {
        return -EINVAL;
    }
    gpu_context_t* gpu = reinterpret_cast<gpu_context_t*>(dev);
    return gpu->alloc_impl(w, h, format, usage, pHandle, pStride, 0);
}
int gpu_context_t::gralloc_alloc_size(alloc_device_t* dev, int w, int h,
                                      int format, int usage,
                                      buffer_handle_t* pHandle, int* pStride,
                                      int bufferSize)
{
    if (!dev) {
        return -EINVAL;
    }
    gpu_context_t* gpu = reinterpret_cast<gpu_context_t*>(dev);
    return gpu->alloc_impl(w, h, format, usage, pHandle, pStride, bufferSize);
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

