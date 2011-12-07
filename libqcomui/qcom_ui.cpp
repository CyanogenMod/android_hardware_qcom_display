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

#include <cutils/log.h>
#include <qcom_ui.h>
#include <gralloc_priv.h>
#include <alloc_controller.h>
#include <memalloc.h>
#include <errno.h>

using gralloc::IMemAlloc;
using gralloc::IonController;
using gralloc::alloc_data;
using android::sp;

namespace {

    static android::sp<gralloc::IAllocController> sAlloc = 0;

    int reallocate_memory(native_handle_t *buffer_handle, int mReqSize, int usage)
    {
        int ret = 0;
        if (sAlloc == 0) {
            sAlloc = gralloc::IAllocController::getInstance(true);
        }
        if (sAlloc == 0) {
            LOGE("sAlloc is still NULL");
            return -EINVAL;
        }

        // Dealloc the old memory
        private_handle_t *hnd = (private_handle_t *)buffer_handle;
        sp<IMemAlloc> memalloc = sAlloc->getAllocator(hnd->flags);
        ret = memalloc->free_buffer((void*)hnd->base, hnd->size, hnd->offset, hnd->fd);

        if (ret) {
            LOGE("%s: free_buffer failed", __FUNCTION__);
            return -1;
        }

        // Realloc new memory
        alloc_data data;
        data.base = 0;
        data.fd = -1;
        data.offset = 0;
        data.size = mReqSize;
        data.align = getpagesize();
        data.uncached = true;
        int allocFlags = usage;

        switch (hnd->format) {
            case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
            case (HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED^HAL_PIXEL_FORMAT_INTERLACE): {
                data.align = 8192;
            } break;
            default: break;
        }
        ret = sAlloc->allocate(data, allocFlags, 0);
        if (ret == 0) {
            hnd->fd = data.fd;
            hnd->base = (int)data.base;
            hnd->offset = data.offset;
            hnd->size = data.size;
        } else {
            LOGE("%s: allocate failed", __FUNCTION__);
            return -EINVAL;
        }
        return ret;
    }
}; // ANONYNMOUS NAMESPACE

/*
 * Gets the number of arguments required for this operation.
 *
 * @param: operation whose argument count is required.
 *
 * @return -EINVAL if the operation is invalid.
 */
int getNumberOfArgsForOperation(int operation) {
    int num_args = -EINVAL;
    switch(operation) {
        case NATIVE_WINDOW_SET_BUFFERS_SIZE:
            num_args = 1;
            break;
        default: LOGE("%s: invalid operation(0x%x)", __FUNCTION__, operation);
            break;
    };
    return num_args;
}

/*
 * Checks if the format is supported by the GPU.
 *
 * @param: format to check
 *
 * @return true if the format is supported by the GPU.
 */
bool isGPUSupportedFormat(int format) {
    bool isSupportedFormat = true;
    switch(format) {
        case (HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED^HAL_PIXEL_FORMAT_INTERLACE):
        case (HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED|HAL_3D_OUT_SIDE_BY_SIDE
              |HAL_3D_IN_SIDE_BY_SIDE_R_L):
        case (HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED|HAL_3D_OUT_SIDE_BY_SIDE
              |HAL_3D_IN_SIDE_BY_SIDE_L_R):
            isSupportedFormat = false;
            break;
        default: break;
    }
    return isSupportedFormat;
}

/*
 * Function to check if the allocated buffer is of the correct size.
 * Reallocate the buffer with the correct size, if the size doesn't
 * match
 *
 * @param: handle of the allocated buffer
 * @param: requested size for the buffer
 * @param: usage flags
 *
 * return 0 on success
 */
int checkBuffer(native_handle_t *buffer_handle, int size, int usage)
{
    // If the client hasn't set a size, return
    if (0 == size) {
        return 0;
    }

    // Validate the handle
    if (private_handle_t::validate(buffer_handle)) {
        LOGE("%s: handle is invalid", __FUNCTION__);
        return -EINVAL;
    }

    // Obtain the private_handle from the native handle
    private_handle_t *hnd = reinterpret_cast<private_handle_t*>(buffer_handle);
    if (hnd->size < size) {
        return reallocate_memory(hnd, size, usage);
    }

    return 0;
}




