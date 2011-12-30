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
#include <cutils/memory.h>
#include <qcom_ui.h>
#include <gralloc_priv.h>
#include <alloc_controller.h>
#include <memalloc.h>
#include <errno.h>
#include <cutils/properties.h>
#include <EGL/eglext.h>

using gralloc::IMemAlloc;
using gralloc::IonController;
using gralloc::alloc_data;
using android::sp;

static int sCompositionType = -1;

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
        case  NATIVE_WINDOW_UPDATE_BUFFERS_GEOMETRY:
            num_args = 3;
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

/*
 * Checks if memory needs to be reallocated for this buffer.
 *
 * @param: Geometry of the current buffer.
 * @param: Required Geometry.
 * @param: Geometry of the updated buffer.
 *
 * @return True if a memory reallocation is required.
 */
bool needNewBuffer(const qBufGeometry currentGeometry,
                   const qBufGeometry requiredGeometry,
                   const qBufGeometry updatedGeometry)
{
    // If the current buffer info matches the updated info,
    // we do not require any memory allocation.
    if (updatedGeometry.width && updatedGeometry.height &&
        updatedGeometry.format) {
        return false;
    }
    if (currentGeometry.width != requiredGeometry.width ||
        currentGeometry.height != requiredGeometry.height ||
        currentGeometry.format != requiredGeometry.format) {
        // Current and required geometry do not match. Allocation
        // required.
        return true;
    }
    return false;
}

/*
 * Update the geometry of this buffer without reallocation.
 *
 * @param: buffer whose geometry needs to be updated.
 * @param: Updated width
 * @param: Updated height
 * @param: Updated format
 */
int updateBufferGeometry(sp<GraphicBuffer> buffer, const qBufGeometry updatedGeometry)
{
    if (buffer == 0) {
        LOGE("%s: graphic buffer is NULL", __FUNCTION__);
        return -EINVAL;
    }

    if (!updatedGeometry.width || !updatedGeometry.height ||
        !updatedGeometry.format) {
        // No update required. Return.
        return 0;
    }
    if (buffer->width == updatedGeometry.width &&
        buffer->height == updatedGeometry.height &&
        buffer->format == updatedGeometry.format) {
        // The buffer has already been updated. Return.
        return 0;
    }

    // Validate the handle
    if (private_handle_t::validate(buffer->handle)) {
        LOGE("%s: handle is invalid", __FUNCTION__);
        return -EINVAL;
    }
    buffer->width  = updatedGeometry.width;
    buffer->height = updatedGeometry.height;
    buffer->format = updatedGeometry.format;
    private_handle_t *hnd = (private_handle_t*)(buffer->handle);
    if (hnd) {
        hnd->width  = updatedGeometry.width;
        hnd->height = updatedGeometry.height;
        hnd->format = updatedGeometry.format;
    } else {
        LOGE("%s: hnd is NULL", __FUNCTION__);
        return -EINVAL;
    }

    return 0;
}

/*
 * Updates the flags for the layer
 *
 * @param: Attribute
 * @param: Identifies if the attribute was enabled or disabled.
 *
 * @return: -EINVAL if the attribute is invalid
 */
int updateLayerQcomFlags(eLayerAttrib attribute, bool enable, int& currentFlags)
{
    int ret = 0;
    switch (attribute) {
        case LAYER_UPDATE_STATUS: {
            if (enable)
                currentFlags |= LAYER_UPDATING;
            else
                currentFlags &= ~LAYER_UPDATING;
        } break;
        default: LOGE("%s: invalid attribute(0x%x)", __FUNCTION__, attribute);
                 break;
    }
    return ret;
}

/*
 * Gets the per frame HWC flags for this layer.
 *
 * @param: current hwcl flags
 * @param: current layerFlags
 *
 * @return: the per frame flags.
 */
int getPerFrameFlags(int hwclFlags, int layerFlags) {
    int flags = hwclFlags;
    if (layerFlags & LAYER_UPDATING)
        flags &= ~HWC_LAYER_NOT_UPDATING;
    else
        flags |= HWC_LAYER_NOT_UPDATING;

    return flags;
}


/*
 * Checks if FB is updated by this composition type
 *
 * @param: composition type
 * @return: true if FB is updated, false if not
 */

bool isUpdatingFB(HWCCompositionType compositionType)
{
    switch(compositionType)
    {
        case HWC_USE_COPYBIT:
            return true;
        default:
            LOGE("%s: invalid composition type(%d)", __FUNCTION__, compositionType);
            return false;
    };
}

/*
 * Get the current composition Type
 *
 * @return the compositon Type
 */
int getCompositionType() {
    char property[PROPERTY_VALUE_MAX];
    int compositionType = 0;
    if (property_get("debug.sf.hw", property, NULL) > 0) {
        if(atoi(property) == 0) {
            compositionType = COMPOSITION_TYPE_CPU;
        } else { //debug.sf.hw = 1
            property_get("debug.composition.type", property, NULL);
            if (property == NULL) {
                compositionType = COMPOSITION_TYPE_GPU;
            } else if ((strncmp(property, "mdp", 3)) == 0) {
                compositionType = COMPOSITION_TYPE_MDP;
            } else if ((strncmp(property, "c2d", 3)) == 0) {
                compositionType = COMPOSITION_TYPE_C2D;
            } else if ((strncmp(property, "dyn", 3)) == 0) {
                compositionType = COMPOSITION_TYPE_DYN;
            } else {
                compositionType = COMPOSITION_TYPE_GPU;
            }
        }
    } else { //debug.sf.hw is not set. Use cpu composition
        compositionType = COMPOSITION_TYPE_CPU;
    }
    return compositionType;
}

/*
 * Clear Region implementation for C2D/MDP versions.
 *
 * @param: region to be cleared
 * @param: EGL Display
 * @param: EGL Surface
 *
 * @return 0 on success
 */
int qcomuiClearRegion(Region region, EGLDisplay dpy, EGLSurface sur)
{
    int ret = 0;

    if (-1 == sCompositionType) {
        sCompositionType = getCompositionType();
    }

    if ((COMPOSITION_TYPE_MDP != sCompositionType) &&
        (COMPOSITION_TYPE_C2D != sCompositionType) &&
        (COMPOSITION_TYPE_CPU != sCompositionType)) {
        // For non CPU/C2D/MDP composition, return an error, so that SF can use
        // the GPU to draw the wormhole.
        return -1;
    }

    android_native_buffer_t *renderBuffer = (android_native_buffer_t *)
                                        eglGetRenderBufferANDROID(dpy, sur);
    if (!renderBuffer) {
        LOGE("%s: eglGetRenderBufferANDROID returned NULL buffer",
            __FUNCTION__);
            return -1;
    }
    private_handle_t *fbHandle = (private_handle_t *)renderBuffer->handle;
    if(!fbHandle) {
        LOGE("%s: Framebuffer handle is NULL", __FUNCTION__);
        return -1;
    }

    int bytesPerPixel = 4;
    if (HAL_PIXEL_FORMAT_RGB_565 == fbHandle->format) {
        bytesPerPixel = 2;
    }

    Region::const_iterator it = region.begin();
    Region::const_iterator const end = region.end();
    const int32_t stride = renderBuffer->stride*bytesPerPixel;
    while (it != end) {
        const Rect& r = *it++;
        uint8_t* dst = (uint8_t*) fbHandle->base +
                       (r.left + r.top*renderBuffer->stride)*bytesPerPixel;
        int w = r.width()*bytesPerPixel;
        int h = r.height();
        do {
            if(4 == bytesPerPixel)
                android_memset32((uint32_t*)dst, 0, w);
            else
                android_memset16((uint16_t*)dst, 0, w);
            dst += stride;
        } while(--h);
    }

    return 0;
}
