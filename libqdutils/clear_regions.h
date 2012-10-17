/* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
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
#ifndef QCOM_CLEARREGIONS_H
#define QCOM_CLEARREGIONS_H

#include <gralloc_priv.h>
#include <cutils/memory.h>
#include <comptype.h>
#include "egl_handles.h"

namespace qdutils {
/*
 * Clear Region implementation for C2D/MDP versions.
 *
 * @param: region to be cleared
 * @param: EGL Display
 * @param: EGL Surface
 *
 * @return 0 on success
 */
static int qcomuiClearRegion(Region region, EGLDisplay dpy){

    int ret = 0;
    int compositionType = QCCompositionType::getInstance().getCompositionType();
    if (compositionType == COMPOSITION_TYPE_GPU){
         //SF can use the GPU to draw the wormhole.
        return -1;
    }

    android_native_buffer_t *renderBuffer =
          qdutils::eglHandles::getInstance().getAndroidNativeRenderBuffer(dpy);
    if (!renderBuffer) {
        ALOGE("%s: eglGetRenderBufferANDROID returned NULL buffer",
             __FUNCTION__);
        return -1;
    }
    private_handle_t *fbHandle = (private_handle_t *)renderBuffer->handle;
    if(!fbHandle) {
        ALOGE("%s: Framebuffer handle is NULL", __FUNCTION__);
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
}; //namespace qdutils
#endif /* end of include guard: QCOM_CLEARREGIONS_H*/
