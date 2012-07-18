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

#include <cutils/log.h>
#include <cutils/memory.h>
#include <qcom_ui.h>
#include <gralloc_priv.h>
#include <alloc_controller.h>
#include <memalloc.h>
#include <errno.h>
#include <EGL/eglext.h>
#include <sys/stat.h>
#include <SkBitmap.h>
#include <SkImageEncoder.h>
#include <Transform.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

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

#ifndef NON_QCOM_TARGET
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
#endif
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
    if (format == HAL_PIXEL_FORMAT_YV12) {
        // We check the YV12 formats, since some Qcom specific formats
        // could have the bits set.
        return true;
    } else if (format & INTERLACE_MASK) {
        // Interlaced content
        return false;
    } else if (format & S3D_FORMAT_MASK) {
        // S3D Formats are not supported by the GPU
       return false;
    }
    return true;
}

#ifdef DECIDE_TEXTURE_TARGET
/* decide the texture target dynamically, based on the pixel format*/

int decideTextureTarget(int pixel_format)
{

  // Default the return value to GL_TEXTURE_EXTERAL_OES
  int retVal = GL_TEXTURE_EXTERNAL_OES;

  // Change texture target to TEXTURE_2D for RGB formats
  switch (pixel_format) {

     case HAL_PIXEL_FORMAT_RGBA_8888:
     case HAL_PIXEL_FORMAT_RGBX_8888:
     case HAL_PIXEL_FORMAT_RGB_888:
     case HAL_PIXEL_FORMAT_RGB_565:
     case HAL_PIXEL_FORMAT_BGRA_8888:
     case HAL_PIXEL_FORMAT_RGBA_5551:
     case HAL_PIXEL_FORMAT_RGBA_4444:
          retVal = GL_TEXTURE_2D;
          break;
     default:
          retVal = GL_TEXTURE_EXTERNAL_OES;
          break;
  }
  return retVal;
}
#endif

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
    if (0 >= size) {
        return 0;
    }

    // Validate the handle
    if (private_handle_t::validate(buffer_handle)) {
        LOGE("%s: handle is invalid", __FUNCTION__);
        return -EINVAL;
    }

    // Obtain the private_handle from the native handle
    private_handle_t *hnd = reinterpret_cast<private_handle_t*>(buffer_handle);
    if (hnd->size != size) {
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

/* Update the S3D format of this buffer.
*
* @param: buffer whosei S3D format needs to be updated.
* @param: Updated buffer S3D format
*/
int updateBufferS3DFormat(sp<GraphicBuffer> buffer, const int s3dFormat)
{
    if (buffer == 0) {
        LOGE("%s: graphic buffer is NULL", __FUNCTION__);
        return -EINVAL;
    }

    buffer->format |= s3dFormat;
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
        case LAYER_ASYNCHRONOUS_STATUS: {
            if (enable)
                currentFlags |= LAYER_ASYNCHRONOUS;
            else
                currentFlags &= ~LAYER_ASYNCHRONOUS;
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

    if (layerFlags & LAYER_ASYNCHRONOUS)
        flags |= HWC_LAYER_ASYNCHRONOUS;
    else
        flags &= ~HWC_LAYER_ASYNCHRONOUS;

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
        (COMPOSITION_TYPE_DYN != sCompositionType) &&
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

/*
 * Handles the externalDisplay event
 * HDMI has highest priority compared to WifiDisplay
 * Based on the current and the new display type, decides the
 * external display to be enabled
 *
 * @param: disp - external display type(wfd/hdmi)
 * @param: value - external event(0/1)
 * @param: currdispType - Current enabled external display Type
 * @return: external display type to be enabled
 *
 */
external_display_type handleEventHDMI(external_display_type disp, int value,
                                       external_display_type currDispType)
{
    external_display_type retDispType = currDispType;
    switch(disp) {
        case EXT_TYPE_HDMI:
            if(value)
                retDispType = EXT_TYPE_HDMI;
            else
                retDispType = EXT_TYPE_NONE;
            break;
        case EXT_TYPE_WIFI:
            if(currDispType != EXT_TYPE_HDMI) {
                if(value)
                    retDispType = EXT_TYPE_WIFI;
                else
                    retDispType = EXT_TYPE_NONE;
            }
            break;
        default:
            LOGE("%s: Unknown External Display Type!!");
            break;
    }
    return retDispType;
}

// Using global variables for layer dumping since "property_set("debug.sf.dump",
// property)" does not work.
int sfdump_countlimit_raw = 0;
int sfdump_counter_raw = 1;
char sfdump_propstr_persist_raw[PROPERTY_VALUE_MAX] = "";
char sfdumpdir_raw[256] = "";
int sfdump_countlimit_png = 0;
int sfdump_counter_png = 1;
char sfdump_propstr_persist_png[PROPERTY_VALUE_MAX] = "";
char sfdumpdir_png[256] = "";

bool needToDumpLayers()
{
    bool bDumpLayer = false;
    char sfdump_propstr[PROPERTY_VALUE_MAX];
    time_t timenow;
    tm sfdump_time;

    time(&timenow);
    localtime_r(&timenow, &sfdump_time);

    if ((property_get("debug.sf.dump.png", sfdump_propstr, NULL) > 0) &&
            (strncmp(sfdump_propstr, sfdump_propstr_persist_png,
                                                    PROPERTY_VALUE_MAX - 1))) {
        // Strings exist & not equal implies it has changed, so trigger a dump
        strncpy(sfdump_propstr_persist_png, sfdump_propstr,
                                                    PROPERTY_VALUE_MAX - 1);
        sfdump_countlimit_png = atoi(sfdump_propstr);
        sfdump_countlimit_png = (sfdump_countlimit_png < 0) ? 0:
                        (sfdump_countlimit_png >= LONG_MAX) ? (LONG_MAX - 1):
                                                        sfdump_countlimit_png;
        if (sfdump_countlimit_png) {
            sprintf(sfdumpdir_png,"/data/sfdump.png%04d%02d%02d.%02d%02d%02d",
            sfdump_time.tm_year + 1900, sfdump_time.tm_mon + 1,
            sfdump_time.tm_mday, sfdump_time.tm_hour,
            sfdump_time.tm_min, sfdump_time.tm_sec);
            if (0 == mkdir(sfdumpdir_png, 0777))
                sfdump_counter_png = 0;
            else
                LOGE("sfdump: Error: %s. Failed to create sfdump directory"
                ": %s", strerror(errno), sfdumpdir_png);
        }
    }

    if (sfdump_counter_png <= sfdump_countlimit_png)
        sfdump_counter_png++;

    if ((property_get("debug.sf.dump", sfdump_propstr, NULL) > 0) &&
            (strncmp(sfdump_propstr, sfdump_propstr_persist_raw,
                                                    PROPERTY_VALUE_MAX - 1))) {
        // Strings exist & not equal implies it has changed, so trigger a dump
        strncpy(sfdump_propstr_persist_raw, sfdump_propstr,
                                                    PROPERTY_VALUE_MAX - 1);
        sfdump_countlimit_raw = atoi(sfdump_propstr);
        sfdump_countlimit_raw = (sfdump_countlimit_raw < 0) ? 0:
                        (sfdump_countlimit_raw >= LONG_MAX) ? (LONG_MAX - 1):
                                                        sfdump_countlimit_raw;
        if (sfdump_countlimit_raw) {
            sprintf(sfdumpdir_raw,"/data/sfdump.raw%04d%02d%02d.%02d%02d%02d",
            sfdump_time.tm_year + 1900, sfdump_time.tm_mon + 1,
            sfdump_time.tm_mday, sfdump_time.tm_hour,
            sfdump_time.tm_min, sfdump_time.tm_sec);
            if (0 == mkdir(sfdumpdir_raw, 0777))
                sfdump_counter_raw = 0;
            else
                LOGE("sfdump: Error: %s. Failed to create sfdump directory"
                ": %s", strerror(errno), sfdumpdir_raw);
        }
    }

    if (sfdump_counter_raw <= sfdump_countlimit_raw)
        sfdump_counter_raw++;

    bDumpLayer = (sfdump_countlimit_png || sfdump_countlimit_raw)? true : false;
    return bDumpLayer;
}

inline void getHalPixelFormatStr(int format, char pixelformatstr[])
{
    if (!pixelformatstr)
        return;

    switch(format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
        strcpy(pixelformatstr, "RGBA_8888");
        break;
    case HAL_PIXEL_FORMAT_RGBX_8888:
        strcpy(pixelformatstr, "RGBX_8888");
        break;
    case HAL_PIXEL_FORMAT_RGB_888:
        strcpy(pixelformatstr, "RGB_888");
        break;
    case HAL_PIXEL_FORMAT_RGB_565:
        strcpy(pixelformatstr, "RGB_565");
        break;
    case HAL_PIXEL_FORMAT_BGRA_8888:
        strcpy(pixelformatstr, "BGRA_8888");
        break;
    case HAL_PIXEL_FORMAT_RGBA_5551:
        strcpy(pixelformatstr, "RGBA_5551");
        break;
    case HAL_PIXEL_FORMAT_RGBA_4444:
        strcpy(pixelformatstr, "RGBA_4444");
        break;
    case HAL_PIXEL_FORMAT_YV12:
        strcpy(pixelformatstr, "YV12");
        break;
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        strcpy(pixelformatstr, "YCbCr_422_SP_NV16");
        break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        strcpy(pixelformatstr, "YCrCb_420_SP_NV21");
        break;
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
        strcpy(pixelformatstr, "YCbCr_422_I_YUY2");
        break;
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
        strcpy(pixelformatstr, "NV12_ENCODEABLE");
        break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
        strcpy(pixelformatstr, "YCbCr_420_SP_TILED_TILE_4x2");
        break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        strcpy(pixelformatstr, "YCbCr_420_SP");
        break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO:
        strcpy(pixelformatstr, "YCrCb_420_SP_ADRENO");
        break;
    case HAL_PIXEL_FORMAT_YCrCb_422_SP:
        strcpy(pixelformatstr, "YCrCb_422_SP");
        break;
    case HAL_PIXEL_FORMAT_R_8:
        strcpy(pixelformatstr, "R_8");
        break;
    case HAL_PIXEL_FORMAT_RG_88:
        strcpy(pixelformatstr, "RG_88");
        break;
    case HAL_PIXEL_FORMAT_INTERLACE:
        strcpy(pixelformatstr, "INTERLACE");
        break;
    default:
        sprintf(pixelformatstr, "Unknown0x%X", format);
        break;
    }
}

void dumpLayer(int moduleCompositionType, int listFlags, size_t layerIndex,
                                                        hwc_layer_t hwLayers[])
{
    char dumplogstr_png[128] = "";
    char dumplogstr_raw[128] = "";
    if (sfdump_counter_png <= sfdump_countlimit_png) {
        sprintf(dumplogstr_png, "[png-dump-frame: %03d of %03d] ",
                                    sfdump_counter_png, sfdump_countlimit_png);
    }
    if (sfdump_counter_raw <= sfdump_countlimit_raw) {
        sprintf(dumplogstr_raw, "[raw-dump-frame: %03d of %03d]",
                                    sfdump_counter_raw, sfdump_countlimit_raw);
    }
    if (NULL == hwLayers) {
        LOGE("sfdump: Error.%s%sLayer[%d] No hwLayers to dump.",
                                dumplogstr_raw, dumplogstr_png, layerIndex);
        return;
    }
    hwc_layer *layer = &hwLayers[layerIndex];
    hwc_rect_t sourceCrop = layer->sourceCrop;
    hwc_rect_t displayFrame = layer->displayFrame;
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    char pixelformatstr[32] = "None";
    uint32_t transform = layer->transform & FINAL_TRANSFORM_MASK;

    if (hnd)
        getHalPixelFormatStr(hnd->format, pixelformatstr);

    LOGE("sfdump: %s%s[%s]-Composition, Layer[%d] SrcBuff[%dx%d] "
        "SrcCrop[%dl, %dt, %dr, %db] "
        "DispFrame[%dl, %dt, %dr, %db] Composition-type = %s, Format = %s, "
        "Orientation = %s, Flags = %s%s%s%s%s%s%s%s%s%s",
        dumplogstr_raw, dumplogstr_png,
        (moduleCompositionType == COMPOSITION_TYPE_GPU)? "GPU":
        (moduleCompositionType == COMPOSITION_TYPE_MDP)? "MDP":
        (moduleCompositionType == COMPOSITION_TYPE_C2D)? "C2D":
        (moduleCompositionType == COMPOSITION_TYPE_CPU)? "CPU":
        (moduleCompositionType == COMPOSITION_TYPE_DYN)? "DYN": "???",
        layerIndex,
        (hnd)? hnd->width : -1, (hnd)? hnd->height : -1,
        sourceCrop.left, sourceCrop.top,
        sourceCrop.right, sourceCrop.bottom,
        displayFrame.left, displayFrame.top,
        displayFrame.right, displayFrame.bottom,
        (layer->compositionType == HWC_FRAMEBUFFER)? "Framebuffer (OpenGL ES)":
        (layer->compositionType == HWC_OVERLAY)? "Overlay":
        (layer->compositionType == HWC_USE_COPYBIT)? "Copybit": "???",
        pixelformatstr,
        (transform == Transform::ROT_0)? "ROT_0":
        (transform == Transform::FLIP_H)? "FLIP_H":
        (transform == Transform::FLIP_V)? "FLIP_V":
        (transform == Transform::ROT_90)? "ROT_90":
        (transform == Transform::ROT_180)? "ROT_180":
        (transform == Transform::ROT_270)? "ROT_270":
        (transform == Transform::ROT_INVALID)? "ROT_INVALID":"???",
        (layer->flags == 0)? "[None]":"",
        (layer->flags & HWC_SKIP_LAYER)? "[Skip layer]":"",
        (layer->flags & HWC_LAYER_NOT_UPDATING)? "[Layer not updating]":"",
        (layer->flags & HWC_USE_ORIGINAL_RESOLUTION)? "[Original Resolution]":"",
        (layer->flags & HWC_DO_NOT_USE_OVERLAY)? "[Do not use Overlay]":"",
        (layer->flags & HWC_COMP_BYPASS)? "[Bypass]":"",
        (layer->flags & HWC_BYPASS_RESERVE_0)? "[Bypass Reserve 0]":"",
        (layer->flags & HWC_BYPASS_RESERVE_1)? "[Bypass Reserve 1]":"",
        (listFlags & HWC_GEOMETRY_CHANGED)? "[List: Geometry Changed]":"",
        (listFlags & HWC_SKIP_COMPOSITION)? "[List: Skip Composition]":"");

        if (NULL == hnd) {
            LOGE("sfdump: %s%sLayer[%d] private-handle is invalid.",
                                dumplogstr_raw, dumplogstr_png, layerIndex);
            return;
        }

        if ((sfdump_counter_png <= sfdump_countlimit_png) && hnd->base) {
            bool bResult = false;
            char sfdumpfile_name[256];
            SkBitmap *tempSkBmp = new SkBitmap();
            SkBitmap::Config tempSkBmpConfig = SkBitmap::kNo_Config;
            sprintf(sfdumpfile_name, "%s/sfdump%03d_layer%d.png", sfdumpdir_png,
                    sfdump_counter_png, layerIndex);

            switch (hnd->format) {
                case HAL_PIXEL_FORMAT_RGBA_8888:
                case HAL_PIXEL_FORMAT_RGBX_8888:
                case HAL_PIXEL_FORMAT_BGRA_8888:
                    tempSkBmpConfig = SkBitmap::kARGB_8888_Config;
                    break;
                case HAL_PIXEL_FORMAT_RGB_565:
                case HAL_PIXEL_FORMAT_RGBA_5551:
                case HAL_PIXEL_FORMAT_RGBA_4444:
                    tempSkBmpConfig = SkBitmap::kRGB_565_Config;
                    break;
                case HAL_PIXEL_FORMAT_RGB_888:
                default:
                    tempSkBmpConfig = SkBitmap::kNo_Config;
                    break;
            }
            if (SkBitmap::kNo_Config != tempSkBmpConfig) {
                tempSkBmp->setConfig(tempSkBmpConfig, hnd->width, hnd->height);
                tempSkBmp->setPixels((void*)hnd->base);
                bResult = SkImageEncoder::EncodeFile(sfdumpfile_name,
                                *tempSkBmp, SkImageEncoder::kPNG_Type, 100);
                LOGE("sfdump: %sDumped Layer[%d] to %s: %s", dumplogstr_png,
                    layerIndex, sfdumpfile_name, bResult ? "Success" : "Fail");
            }
            else {
                LOGE("sfdump: %sSkipping Layer[%d] dump: Unsupported layer "
                    "format %s for png encoder.", dumplogstr_png, layerIndex,
                                            pixelformatstr);
            }
            delete tempSkBmp; // Calls SkBitmap::freePixels() internally.
        }

        if ((sfdump_counter_raw <= sfdump_countlimit_raw) && hnd->base) {
            char sfdumpfile_name[256];
            bool bResult = false;
            sprintf(sfdumpfile_name, "%s/sfdump%03d_layer%d_%dx%d_%s.raw",
                sfdumpdir_raw,
                sfdump_counter_raw, layerIndex, hnd->width, hnd->height,
                pixelformatstr);
            FILE* fp = fopen(sfdumpfile_name, "w+");
            if (fp != NULL) {
                bResult = (bool) fwrite((void*)hnd->base, hnd->size, 1, fp);
                fclose(fp);
            }
            LOGE("sfdump: %s Dumped Layer[%d] to %s: %s", dumplogstr_raw,
                layerIndex, sfdumpfile_name, bResult ? "Success" : "Fail");
        }
}

