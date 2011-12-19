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

#ifndef INCLUDE_LIBQCOM_UI
#define INCLUDE_LIBQCOM_UI

#include <cutils/native_handle.h>
#include <ui/GraphicBuffer.h>
#include <hardware/hwcomposer.h>
#include <ui/Region.h>
#include <EGL/egl.h>

using namespace android;
using android::sp;
using android::GraphicBuffer;

/*
 * Qcom specific Native Window perform operations
 */
enum {
    NATIVE_WINDOW_SET_BUFFERS_SIZE        = 0x10000000,
    NATIVE_WINDOW_UPDATE_BUFFERS_GEOMETRY = 0x20000000,
};

// Enum containing the supported composition types
enum {
    COMPOSITION_TYPE_GPU = 0,
    COMPOSITION_TYPE_MDP = 0x1,
    COMPOSITION_TYPE_C2D = 0x2,
    COMPOSITION_TYPE_CPU = 0x4,
    COMPOSITION_TYPE_DYN = 0x8
};

/*
 * Layer Attributes
 */
enum eLayerAttrib {
    LAYER_UPDATE_STATUS,
};

/*
 * Layer Flags
 */
enum {
    LAYER_UPDATING = 1<<0,
};

/*
 * Flags set by the layer and sent to HWC
 */
enum {
    HWC_LAYER_NOT_UPDATING      = 0x00000002,
    HWC_USE_ORIGINAL_RESOLUTION = 0x10000000,
    HWC_DO_NOT_USE_OVERLAY      = 0x20000000,
    HWC_COMP_BYPASS             = 0x40000000,
};

enum HWCCompositionType {
    HWC_USE_GPU = HWC_FRAMEBUFFER, // This layer is to be handled by Surfaceflinger
    HWC_USE_OVERLAY = HWC_OVERLAY, // This layer is to be handled by the overlay
    HWC_USE_COPYBIT                // This layer is to be handled by copybit
};

/*
 * Structure to hold the buffer geometry
 */
struct qBufGeometry {
    int width;
    int height;
    int format;
    void set(int w, int h, int f) {
       width = w;
       height = h;
       format = f;
    }
};

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
int checkBuffer(native_handle_t *buffer_handle, int size, int usage);

/*
 * Checks if the format is supported by the GPU.
 *
 * @param: format to check
 *
 * @return true if the format is supported by the GPU.
 */
bool isGPUSupportedFormat(int format);

/*
 * Gets the number of arguments required for this operation.
 *
 * @param: operation whose argument count is required.
 *
 * @return -EINVAL if the operation is invalid.
 */
int getNumberOfArgsForOperation(int operation);

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
                            const qBufGeometry updatedGeometry);

/*
 * Update the geometry of this buffer without reallocation.
 *
 * @param: buffer whose geometry needs to be updated.
 * @param: Updated buffer geometry
 */
int updateBufferGeometry(sp<GraphicBuffer> buffer, const qBufGeometry bufGeometry);

/*
 * Updates the flags for the layer
 *
 * @param: Attribute
 * @param: Identifies if the attribute was enabled or disabled.
 * @param: current Layer flags.
 *
 * @return: Flags for the layer
 */
int updateLayerQcomFlags(eLayerAttrib attribute, bool enable, int& currentFlags);

/*
 * Gets the per frame HWC flags for this layer.
 *
 * @param: current hwcl flags
 * @param: current layerFlags
 *
 * @return: the per frame flags.
 */
int getPerFrameFlags(int hwclFlags, int layerFlags);

/*
 * Checks if FB is updated by this composition type
 *
 * @param: composition type
 * @return: true if FB is updated, false if not
 */

bool isUpdatingFB(HWCCompositionType compositionType);

/*
 * Get the current composition Type
 *
 * @return the compositon Type
 */
int getCompositionType();

/*
 * Clear region implementation for C2D/MDP versions.
 *
 * @param: region to be cleared
 * @param: EGL Display
 * @param: EGL Surface
 *
 * @return 0 on success
 */
int qcomuiClearRegion(Region region, EGLDisplay dpy, EGLSurface sur);
#endif // INCLUDE_LIBQCOM_UI
