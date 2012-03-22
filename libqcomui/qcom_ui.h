/*
 * Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.

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
#include <utils/Singleton.h>
#include <cutils/properties.h>
#include "../libgralloc/gralloc_priv.h"

using namespace android;
using android::sp;
using android::GraphicBuffer;

#define HWC_BYPASS_INDEX_MASK 0x00000030

/*
 * Qcom specific Native Window perform operations
 */
enum {
    NATIVE_WINDOW_SET_BUFFERS_SIZE        = 0x10000000,
    NATIVE_WINDOW_UPDATE_BUFFERS_GEOMETRY = 0x20000000,
    NATIVE_WINDOW_SET_S3D_FORMAT          = 0x40000000,
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
    LAYER_ASYNCHRONOUS_STATUS,
};

/*
 * Layer Flags
 */
enum {
    LAYER_UPDATING     = 1<<0,
    LAYER_ASYNCHRONOUS = 1<<1,
};

/*
 * Flags set by the layer and sent to HWC
 */
enum {
    HWC_LAYER_NOT_UPDATING      = 0x00000002,
    HWC_LAYER_ASYNCHRONOUS      = 0x00000004,
    HWC_USE_ORIGINAL_RESOLUTION = 0x10000000,
    HWC_DO_NOT_USE_OVERLAY      = 0x20000000,
    HWC_COMP_BYPASS             = 0x40000000,
    HWC_USE_EXT_ONLY            = 0x80000000, //Layer displayed on external only
    HWC_USE_EXT_BLOCK           = 0x01000000, //Layer displayed on external only
    HWC_BYPASS_RESERVE_0        = 0x00000010,
    HWC_BYPASS_RESERVE_1        = 0x00000020,
};

enum HWCCompositionType {
    HWC_USE_GPU = HWC_FRAMEBUFFER, // This layer is to be handled by Surfaceflinger
    HWC_USE_OVERLAY = HWC_OVERLAY, // This layer is to be handled by the overlay
    HWC_USE_COPYBIT                // This layer is to be handled by copybit
};

enum external_display_state {
    EXT_DISPLAY_OFF,
    EXT_DISPLAY_HDMI,
    EXT_DISPLAY_WIFI
};

enum external_display_type {
    EXT_TYPE_HDMI = 1,
    EXT_TYPE_WIFI
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

class QCBaseLayer
{
//    int mS3DFormat;
    int32_t mComposeS3DFormat;
public:
    QCBaseLayer()
    {
        mComposeS3DFormat = 0;
    }
    enum { // S3D formats
        eS3D_SIDE_BY_SIDE   = 0x10000,
        eS3D_TOP_BOTTOM     = 0x20000
    };
/*
    virtual status_t setStereoscopic3DFormat(int format) { mS3DFormat = format; return 0; }
    virtual int getStereoscopic3DFormat() const { return mS3DFormat; }
 */
    void setS3DComposeFormat (int32_t hints)
    {
        if (hints & HWC_HINT_DRAW_S3D_SIDE_BY_SIDE)
            mComposeS3DFormat = eS3D_SIDE_BY_SIDE;
        else if (hints & HWC_HINT_DRAW_S3D_TOP_BOTTOM)
            mComposeS3DFormat = eS3D_TOP_BOTTOM;
        else
            mComposeS3DFormat = 0;
    }
    int32_t needsS3DCompose () const { return mComposeS3DFormat; }
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
 * Update the S3D format of this buffer.
 *
 * @param: buffer whosei S3D format needs to be updated.
 * @param: Updated buffer S3D format
 */
int updateBufferS3DFormat(sp<GraphicBuffer> buffer, const int s3dFormat);

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

/*
 * Handles the externalDisplay event
 * HDMI has highest priority compared to WifiDisplay
 * Based on the current and the new display event, decides the
 * external display to be enabled
 *
 * @param: newEvent - new external event
 * @param: currEvent - currently enabled external event
 * @return: external display to be enabled
 *
 */
external_display_state handleEventHDMI(external_display_type disp,
                                 external_display_state newState,
                                 external_display_state currState);

/*
 * Checks if layers need to be dumped based on system property "debug.sf.dump"
 * for raw dumps and "debug.sf.dump.png" for png dumps.
 *
 * For example, to dump 25 frames in raw format, do,
 *     adb shell setprop debug.sf.dump 25
 * Layers are dumped in a time-stamped location: /data/sfdump*.
 *
 * To dump 10 frames in png format, do,
 *     adb shell setprop debug.sf.dump.png 10
 * To dump another 25 or so frames in raw format, do,
 *     adb shell setprop debug.sf.dump 26
 *
 * To turn off logcat logging of layer-info, set both properties to 0,
 *     adb shell setprop debug.sf.dump.png 0
 *     adb shell setprop debug.sf.dump 0
 *
 * @return: true if layers need to be dumped (or logcat-ed).
 */
bool needToDumpLayers();

/*
 * Dumps a layer's info into logcat and its buffer into raw/png files.
 *
 * @param: moduleCompositionType - Composition type set in hwcomposer module.
 * @param: listFlags - Flags used in hwcomposer's list.
 * @param: layerIndex - Index of layer being dumped.
 * @param: hwLayers - Address of hwc_layer_t to log and dump.
 *
 */
void dumpLayer(int moduleCompositionType, int listFlags, size_t layerIndex,
                                                    hwc_layer_t hwLayers[]);

#endif // INCLUDE_LIBQCOM_UI
