/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>

#include <hardware/hwcomposer.h>
#include <overlayLib.h>
#include <overlayLibUI.h>
#include <copybit.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <ui/android_native_buffer.h>
#include <gralloc_priv.h>
#include <genlock.h>
#include <qcom_ui.h>
#include <gr.h>

/*****************************************************************************/
#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))
#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

#ifdef COMPOSITION_BYPASS
#define MAX_BYPASS_LAYERS 3
#define BYPASS_DEBUG 0
#define BYPASS_INDEX_OFFSET 4

enum BypassState {
    BYPASS_ON,
    BYPASS_OFF,
    BYPASS_OFF_PENDING,
};

enum BypassBufferLockState {
    BYPASS_BUFFER_UNLOCKED,
    BYPASS_BUFFER_LOCKED,
};
#endif

enum HWCLayerType{
    HWC_SINGLE_VIDEO           = 0x1,
    HWC_ORIG_RESOLUTION        = 0x2,
    HWC_S3D_LAYER              = 0x4,
    HWC_STOP_UI_MIRRORING_MASK = 0xF
};

enum eHWCOverlayStatus {
    HWC_OVERLAY_OPEN,
    HWC_OVERLAY_PREPARE_TO_CLOSE,
    HWC_OVERLAY_CLOSED
};

struct hwc_context_t {
    hwc_composer_device_t device;
    /* our private state goes below here */
    overlay::Overlay* mOverlayLibObject;
    native_handle_t *previousOverlayHandle;
#ifdef COMPOSITION_BYPASS
    overlay::OverlayUI* mOvUI[MAX_BYPASS_LAYERS];
    native_handle_t* previousBypassHandle[MAX_BYPASS_LAYERS];
    BypassBufferLockState bypassBufferLockState[MAX_BYPASS_LAYERS];
    int layerindex[MAX_BYPASS_LAYERS];
    int nPipesUsed;
    BypassState bypassState;
#endif
#if defined HDMI_DUAL_DISPLAY
    external_display mHDMIEnabled; // Type of external display
    bool pendingHDMI;
#endif
    int previousLayerCount;
    eHWCOverlayStatus hwcOverlayStatus;
};

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};


struct private_hwc_module_t {
    hwc_module_t base;
    copybit_device_t *copybitEngine;
    framebuffer_device_t *fbDevice;
    int compositionType;
    bool isBypassEnabled; //from build.prop ro.sf.compbypass.enable
};

struct private_hwc_module_t HAL_MODULE_INFO_SYM = {
    base: {
        common: {
            tag: HARDWARE_MODULE_TAG,
            version_major: 1,
            version_minor: 0,
            id: HWC_HARDWARE_MODULE_ID,
            name: "Hardware Composer Module",
            author: "The Android Open Source Project",
            methods: &hwc_module_methods,
        }
   },
   copybitEngine: NULL,
   fbDevice: NULL,
   compositionType: 0,
   isBypassEnabled: false,
};

//Only at this point would the compiler know all storage class sizes.
//The header has hooks which need to know those beforehand.
#include <external_display_only.h>

/*****************************************************************************/

static void dump_layer(hwc_layer_t const* l) {
    LOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}, {%d,%d,%d,%d}",
            l->compositionType, l->flags, l->handle, l->transform, l->blending,
            l->sourceCrop.left,
            l->sourceCrop.top,
            l->sourceCrop.right,
            l->sourceCrop.bottom,
            l->displayFrame.left,
            l->displayFrame.top,
            l->displayFrame.right,
            l->displayFrame.bottom);
}

static inline int min(const int& a, const int& b) {
    return (a < b) ? a : b;
}

static inline int max(const int& a, const int& b) {
    return (a > b) ? a : b;
}
#ifdef COMPOSITION_BYPASS
void setLayerbypassIndex(hwc_layer_t* layer, const int bypass_index)
{
    layer->flags &= ~HWC_BYPASS_INDEX_MASK;
    layer->flags |= bypass_index << BYPASS_INDEX_OFFSET;
}

int  getLayerbypassIndex(hwc_layer_t* layer)
{
    int byp_index = -1;

    if(layer->flags & HWC_COMP_BYPASS) {
        byp_index = ((layer->flags & HWC_BYPASS_INDEX_MASK) >> BYPASS_INDEX_OFFSET);
        byp_index = (byp_index < MAX_BYPASS_LAYERS ? byp_index : -1 );
    }
    return byp_index;
}

void unlockPreviousBypassBuffers(hwc_context_t* ctx) {
    // Unlock the previous bypass buffers. We can blindly unlock the buffers here,
    // because buffers will be in this list only if the lock was successfully acquired.
    for(int i = 0; i < MAX_BYPASS_LAYERS && ctx->previousBypassHandle[i]; i++) {
       private_handle_t *hnd = (private_handle_t*) ctx->previousBypassHandle[i];

       // Validate the handle to make sure it hasn't been deallocated.
       if (private_handle_t::validate(ctx->previousBypassHandle[i])) {
            continue;
       }
       // Check if the handle was locked previously
       if (private_handle_t::PRIV_FLAGS_HWC_LOCK & hnd->flags) {
          if (GENLOCK_FAILURE == genlock_unlock_buffer(ctx->previousBypassHandle[i])) {
              LOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
          } else {
              ctx->previousBypassHandle[i] = NULL;
              // Reset the lock flag
              hnd->flags &= ~private_handle_t::PRIV_FLAGS_HWC_LOCK;
          }
       }
    }
}

void print_info(hwc_layer_t* layer)
{
     hwc_rect_t sourceCrop = layer->sourceCrop;
     hwc_rect_t displayFrame = layer->displayFrame;

     int s_l = sourceCrop.left;
     int s_t = sourceCrop.top;
     int s_r = sourceCrop.right;
     int s_b = sourceCrop.bottom;

     int d_l = displayFrame.left;
     int d_t = displayFrame.top;
     int d_r = displayFrame.right;
     int d_b = displayFrame.bottom;

     LOGE_IF(BYPASS_DEBUG, "src:[%d,%d,%d,%d] (%d x %d) dst:[%d,%d,%d,%d] (%d x %d)",
                             s_l, s_t, s_r, s_b, (s_r - s_l), (s_b - s_t),
                             d_l, d_t, d_r, d_b, (d_r - d_l), (d_b - d_t));
}

//Crops source buffer against destination and FB boundaries
void calculate_crop_rects(hwc_rect_t& crop, hwc_rect_t& dst, int hw_w, int hw_h) {

    int& crop_x = crop.left;
    int& crop_y = crop.top;
    int& crop_r = crop.right;
    int& crop_b = crop.bottom;
    int crop_w = crop.right - crop.left;
    int crop_h = crop.bottom - crop.top;

    int& dst_x = dst.left;
    int& dst_y = dst.top;
    int& dst_r = dst.right;
    int& dst_b = dst.bottom;
    int dst_w = dst.right - dst.left;
    int dst_h = dst.bottom - dst.top;

    if(dst_x < 0) {
        float scale_x =  crop_w * 1.0f / dst_w;
        float diff_factor = (scale_x * abs(dst_x));
        crop_x = crop_x + (int)diff_factor;
        crop_w = crop_r - crop_x;

        dst_x = 0;
        dst_w = dst_r - dst_x;;
    }
    if(dst_r > hw_w) {
        float scale_x = crop_w * 1.0f / dst_w;
        float diff_factor = scale_x * (dst_r - hw_w);
        crop_r = crop_r - diff_factor;
        crop_w = crop_r - crop_x;

        dst_r = hw_w;
        dst_w = dst_r - dst_x;
    }
    if(dst_y < 0) {
        float scale_y = crop_h * 1.0f / dst_h;
        float diff_factor = scale_y * abs(dst_y);
        crop_y = crop_y + diff_factor;
        crop_h = crop_b - crop_y;

        dst_y = 0;
        dst_h = dst_b - dst_y;
    }
    if(dst_b > hw_h) {
        float scale_y = crop_h * 1.0f / dst_h;
        float diff_factor = scale_y * (dst_b - hw_h);
        crop_b = crop_b - diff_factor;
        crop_h = crop_b - crop_y;

        dst_b = hw_h;
        dst_h = dst_b - dst_y;
    }

    LOGE_IF(BYPASS_DEBUG,"crop: [%d,%d,%d,%d] dst:[%d,%d,%d,%d]",
                     crop_x, crop_y, crop_w, crop_h,dst_x, dst_y, dst_w, dst_h);
}

/*
 * Configures pipe(s) for composition bypass
 */
static int prepareBypass(hwc_context_t *ctx, hwc_layer_t *layer,
                        int nPipeIndex, int vsync_wait, int isFG) {

    if (ctx && ctx->mOvUI[nPipeIndex]) {
        overlay::OverlayUI *ovUI = ctx->mOvUI[nPipeIndex];

        private_hwc_module_t* hwcModule = reinterpret_cast<
                private_hwc_module_t*>(ctx->device.common.module);
        if (!hwcModule) {
            LOGE("%s: NULL Module", __FUNCTION__);
            return -1;
        }

        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(!hnd) {
            LOGE("%s: layer handle is NULL", __FUNCTION__);
            return -1;
        }

        int hw_w = hwcModule->fbDevice->width;
        int hw_h = hwcModule->fbDevice->height;

        hwc_rect_t sourceCrop = layer->sourceCrop;
        hwc_rect_t displayFrame = layer->displayFrame;

        const int src_w = sourceCrop.right - sourceCrop.left;
        const int src_h = sourceCrop.bottom - sourceCrop.top;

        hwc_rect_t crop = sourceCrop;
        int crop_w = crop.right - crop.left;
        int crop_h = crop.bottom - crop.top;

        hwc_rect_t dst = displayFrame;
        int dst_w = dst.right - dst.left;
        int dst_h = dst.bottom - dst.top;

        if(hnd != NULL && (hnd->flags & private_handle_t::PRIV_FLAGS_NONCONTIGUOUS_MEM )) {
            LOGE("%s: Unable to setup bypass due to non-pmem memory",__FUNCTION__);
            return -1;
        }

        if(dst.left < 0 || dst.top < 0 || dst.right > hw_w || dst.bottom > hw_h) {
            LOGE_IF(BYPASS_DEBUG,"%s: Destination has negative coordinates", __FUNCTION__);

            calculate_crop_rects(crop, dst, hw_w, hw_h);

            //Update calulated width and height
            crop_w = crop.right - crop.left;
            crop_h = crop.bottom - crop.top;

            dst_w = dst.right - dst.left;
            dst_h = dst.bottom - dst.top;
        }

        if( (dst_w > hw_w)|| (dst_h > hw_h)) {
            LOGE_IF(BYPASS_DEBUG,"%s: Destination rectangle exceeds FB resolution", __FUNCTION__);
            print_info(layer);
            dst_w = hw_w;
            dst_h = hw_h;
        }

        overlay_buffer_info info;
        info.width = src_w;
        info.height = src_h;
        info.format = hnd->format;
        info.size = hnd->size;

        int fbnum = 0;
        int orientation = layer->transform;
        const bool useVGPipe =  (nPipeIndex != (MAX_BYPASS_LAYERS-1));
        //only last layer should wait for vsync
        const bool waitForVsync = vsync_wait;
        const bool isFg = isFG;
        //Just to differentiate zorders for different layers
        const int zorder = nPipeIndex;

        ovUI->setSource(info, orientation);
        ovUI->setCrop(crop.left, crop.top, crop_w, crop_h);
        ovUI->setDisplayParams(fbnum, waitForVsync, isFg, zorder, useVGPipe);
        ovUI->setPosition(dst.left, dst.top, dst_w, dst_h);

        LOGE_IF(BYPASS_DEBUG,"%s: Bypass set: crop[%d,%d,%d,%d] dst[%d,%d,%d,%d] waitforVsync: %d \
                                isFg: %d zorder: %d VG = %d nPipe: %d",__FUNCTION__,
                                crop.left, crop.top, crop_w, crop_h,
                                dst.left, dst.top, dst_w, dst_h,
                                waitForVsync, isFg, zorder, useVGPipe, nPipeIndex );

        if(ovUI->commit() != overlay::NO_ERROR) {
            LOGE("%s: Overlay Commit failed", __FUNCTION__);
            return -1;
        }
    }
    return 0;
}

/*
 * Checks if doing comp. bypass is possible.
 * It is possible if
 * 1. No MDP pipe is used
 * 2. Rotation is not needed
 * 3. We have atmost MAX_BYPASS_LAYERS
 */
inline static bool isBypassDoable(hwc_composer_device_t *dev, const int yuvCount,
        const hwc_layer_list_t* list) {
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    //Check if enabled in build.prop
    if(hwcModule->isBypassEnabled == false) {
        return false;
    }

    if(list->numHwLayers < 1) {
        return false;
    }

#if defined HDMI_DUAL_DISPLAY
    //Disable bypass when HDMI is enabled
    if(ctx->mHDMIEnabled || ctx->pendingHDMI) {
        return false;
    }
#endif

    if(ExtDispOnly::isModeOn()) {
        return false;
    }

    //Bypass is not efficient if rotation or asynchronous mode is needed.
    for(int i = 0; i < list->numHwLayers; ++i) {
        if(list->hwLayers[i].transform) {
            return false;
        }
        if(list->hwLayers[i].flags & HWC_LAYER_ASYNCHRONOUS) {
            return false;
        }
    }

    return (yuvCount == 0) && (ctx->hwcOverlayStatus == HWC_OVERLAY_CLOSED)
                                   && (list->numHwLayers <= MAX_BYPASS_LAYERS);
}

void setBypassLayerFlags(hwc_context_t* ctx, hwc_layer_list_t* list)
{
    for(int index = 0 ; index < MAX_BYPASS_LAYERS; index++ )
    {
        int layer_index = ctx->layerindex[index];
        if(layer_index >= 0) {
            hwc_layer_t* layer = &(list->hwLayers[layer_index]);

            layer->flags |= HWC_COMP_BYPASS;
            layer->compositionType = HWC_USE_OVERLAY;
            layer->hints |= HWC_HINT_CLEAR_FB;
        }
    }

    if( list->numHwLayers > ctx->nPipesUsed ) {
         list->flags &= ~HWC_SKIP_COMPOSITION; //Compose to FB
    } else {
         list->flags |= HWC_SKIP_COMPOSITION; // Dont
    }
}

bool setupBypass(hwc_context_t* ctx, hwc_layer_list_t* list) {
    int nPipeIndex, vsync_wait, isFG;
    int numHwLayers = list->numHwLayers;
    int nPipeAvailable = MAX_BYPASS_LAYERS;

    for (int index = 0 ; (index < numHwLayers) && nPipeAvailable; index++) {

        hwc_layer_t* layer = &(list->hwLayers[index]);

        nPipeIndex =  MAX_BYPASS_LAYERS - nPipeAvailable;
        //Set VSYNC wait is needed only for the last pipe queued
        vsync_wait = (nPipeIndex == (numHwLayers-1));
        //Set isFG to true for layer with z-order zero
        isFG = !index;

        //Clear Bypass flags for the layer
        layer->flags &= ~HWC_COMP_BYPASS;
        layer->flags |= HWC_BYPASS_INDEX_MASK;

        if( prepareBypass(ctx, &(list->hwLayers[index]), nPipeIndex, vsync_wait, isFG) != 0 ) {
           LOGE_IF(BYPASS_DEBUG, "%s: layer %d failed to configure bypass for pipe index: %d",
                                                               __FUNCTION__, index, nPipeIndex);
           return false;
         } else {
           ctx->layerindex[nPipeIndex] = index;
           setLayerbypassIndex(layer, nPipeIndex);
           nPipeAvailable--;
         }
    }
    ctx->nPipesUsed =  MAX_BYPASS_LAYERS - nPipeAvailable;
    return true;
}

void unsetBypassLayerFlags(hwc_layer_list_t* list) {
    if (!list)
        return;

    for (int index = 0 ; index < list->numHwLayers; index++) {
        if(list->hwLayers[index].flags & HWC_COMP_BYPASS) {
            list->hwLayers[index].flags &= ~HWC_COMP_BYPASS;
        }
    }
}

void unsetBypassBufferLockState(hwc_context_t* ctx) {
    for (int i= 0; i< MAX_BYPASS_LAYERS; i++) {
        ctx->bypassBufferLockState[i] = BYPASS_BUFFER_UNLOCKED;
    }
}

void storeLockedBypassHandle(hwc_layer_list_t* list, hwc_context_t* ctx) {
   if (!list)
        return;

   for(int index = 0; index < MAX_BYPASS_LAYERS; index++ ) {
       hwc_layer_t layer = list->hwLayers[ctx->layerindex[index]];

       if (layer.flags & HWC_COMP_BYPASS) {
            private_handle_t *hnd = (private_handle_t*)layer.handle;

            if (ctx->bypassBufferLockState[index] == BYPASS_BUFFER_LOCKED) {
               ctx->previousBypassHandle[index] = (native_handle_t*)layer.handle;
               hnd->flags |= private_handle_t::PRIV_FLAGS_HWC_LOCK;
           } else {
              ctx->previousBypassHandle[index] = NULL;
           }
       }
   }
}

void closeExtraPipes(hwc_context_t* ctx) {

    int pipes_used = ctx->nPipesUsed;

    //Unused pipes must be of higher z-order
    for (int i =  pipes_used ; i < MAX_BYPASS_LAYERS; i++) {
        if (ctx->previousBypassHandle[i]) {
            private_handle_t *hnd = (private_handle_t*) ctx->previousBypassHandle[i];

            if (private_handle_t::validate(ctx->previousBypassHandle[i])) {
                continue;
            }

            if (GENLOCK_FAILURE == genlock_unlock_buffer(ctx->previousBypassHandle[i])) {
                LOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
            } else {
                ctx->previousBypassHandle[i] = NULL;
                ctx->bypassBufferLockState[i] = BYPASS_BUFFER_UNLOCKED;
                hnd->flags &= ~private_handle_t::PRIV_FLAGS_HWC_LOCK;
            }
        }
        ctx->mOvUI[i]->closeChannel();
        ctx->layerindex[i] = -1;
    }
}
#endif  //COMPOSITION_BYPASS

static int setVideoOverlayStatusInGralloc(hwc_context_t* ctx, const bool enable) {
#if defined HDMI_DUAL_DISPLAY
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           ctx->device.common.module);
    if(!hwcModule) {
        LOGE("%s: invalid params", __FUNCTION__);
        return -1;
    }

    framebuffer_device_t *fbDev = hwcModule->fbDevice;
    if (!fbDev) {
        LOGE("%s: fbDev is NULL", __FUNCTION__);
        return -1;
    }

    // Inform the gralloc to stop or start UI mirroring
    fbDev->videoOverlayStarted(fbDev, enable);
#endif
    return 0;
}

static void setHWCOverlayStatus(hwc_context_t *ctx, bool isVideoPresent) {

    switch (ctx->hwcOverlayStatus) {
        case HWC_OVERLAY_OPEN:
            ctx->hwcOverlayStatus =
                isVideoPresent ? HWC_OVERLAY_OPEN : HWC_OVERLAY_PREPARE_TO_CLOSE;
        break;
        case HWC_OVERLAY_PREPARE_TO_CLOSE:
            ctx->hwcOverlayStatus =
                isVideoPresent ? HWC_OVERLAY_OPEN : HWC_OVERLAY_CLOSED;
        break;
        case HWC_OVERLAY_CLOSED:
            ctx->hwcOverlayStatus =
                isVideoPresent ? HWC_OVERLAY_OPEN : HWC_OVERLAY_CLOSED;
        break;
        default:
          LOGE("%s: Invalid hwcOverlayStatus (status =%d)", __FUNCTION__,
                ctx->hwcOverlayStatus);
        break;
    }
}

static int hwc_closeOverlayChannels(hwc_context_t* ctx) {
#ifdef USE_OVERLAY
    overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
    if(!ovLibObject) {
        LOGE("%s: invalid params", __FUNCTION__);
        return -1;
    }

    if (HWC_OVERLAY_PREPARE_TO_CLOSE == ctx->hwcOverlayStatus) {
        // Video mirroring is going on, and we do not have any layers to
        // mirror directly. Close the current video channel and inform the
        // gralloc to start UI mirroring
        ovLibObject->closeChannel();
        // Inform the gralloc that video overlay has stopped.
        setVideoOverlayStatusInGralloc(ctx, false);
    }
#endif
    return 0;
}

/*
 * Configures mdp pipes
 */
static int prepareOverlay(hwc_context_t *ctx, hwc_layer_t *layer, const int flags) {
     int ret = 0;

#ifdef COMPOSITION_BYPASS
     if(ctx && (ctx->bypassState != BYPASS_OFF)) {
        ctx->nPipesUsed = 0;
        closeExtraPipes(ctx);
        ctx->bypassState = BYPASS_OFF;
     }
#endif

     if (LIKELY(ctx && ctx->mOverlayLibObject)) {
        private_hwc_module_t* hwcModule =
            reinterpret_cast<private_hwc_module_t*>(ctx->device.common.module);
        if (UNLIKELY(!hwcModule)) {
            LOGE("prepareOverlay null module ");
            return -1;
        }

        private_handle_t *hnd = (private_handle_t *)layer->handle;
        overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
        overlay_buffer_info info;
        info.width = hnd->width;
        info.height = hnd->height;
        info.format = hnd->format;
        info.size = hnd->size;
        info.secure = (hnd->flags &
                       private_handle_t::PRIV_FLAGS_SECURE_BUFFER)? true:false;

        int hdmiConnected = 0;

#if defined HDMI_DUAL_DISPLAY
        if(!ctx->pendingHDMI) //makes sure the UI channel is opened first
            hdmiConnected = (int)ctx->mHDMIEnabled;
#endif
        ret = ovLibObject->setSource(info, layer->transform,
                            hdmiConnected, flags);
        if (!ret) {
            LOGE("prepareOverlay setSource failed");
            return -1;
        }

        ret = ovLibObject->setTransform(layer->transform);
        if (!ret) {
            LOGE("prepareOverlay setTransform failed transform %x",
                    layer->transform);
            return -1;
        }

        hwc_rect_t sourceCrop = layer->sourceCrop;
        ret = ovLibObject->setCrop(sourceCrop.left, sourceCrop.top,
                                  (sourceCrop.right - sourceCrop.left),
                                  (sourceCrop.bottom - sourceCrop.top));
        if (!ret) {
            LOGE("prepareOverlay setCrop failed");
            return -1;
        }
#if defined HDMI_DUAL_DISPLAY
        // Send the device orientation to  overlayLib
        if(hwcModule) {
            framebuffer_device_t *fbDev = reinterpret_cast<framebuffer_device_t*>
                                                            (hwcModule->fbDevice);
            if(fbDev) {
                private_module_t* m = reinterpret_cast<private_module_t*>(
                                                         fbDev->common.module);
                if(m)
                    ovLibObject->setDeviceOrientation(m->orientation);
            }
        }
#endif
        if (layer->flags & HWC_USE_ORIGINAL_RESOLUTION) {
            framebuffer_device_t* fbDev = hwcModule->fbDevice;
            ret = ovLibObject->setPosition(0, 0,
                                           fbDev->width, fbDev->height);
        } else {
            hwc_rect_t displayFrame = layer->displayFrame;
            ret = ovLibObject->setPosition(displayFrame.left, displayFrame.top,
                                    (displayFrame.right - displayFrame.left),
                                    (displayFrame.bottom - displayFrame.top));
        }
        if (!ret) {
            LOGE("prepareOverlay setPosition failed");
            return -1;
        }
     }
     return 0;
}

void unlockPreviousOverlayBuffer(hwc_context_t* ctx)
{
    if (ctx->previousOverlayHandle) {
        // Validate the handle before attempting to use it.
        if (!private_handle_t::validate(ctx->previousOverlayHandle)) {
            private_handle_t *hnd = (private_handle_t*)ctx->previousOverlayHandle;
            // Unlock any previously locked buffers
            if (private_handle_t::PRIV_FLAGS_HWC_LOCK & hnd->flags) {
                if (GENLOCK_NO_ERROR == genlock_unlock_buffer(ctx->previousOverlayHandle)) {
                    ctx->previousOverlayHandle = NULL;
                    hnd->flags &= ~private_handle_t::PRIV_FLAGS_HWC_LOCK;
                } else {
                    LOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
                }
            }
        }
    }
}

bool canSkipComposition(hwc_context_t* ctx, int yuvBufferCount, int currentLayerCount,
                        int numLayersNotUpdating)
{
    if (!ctx) {
        LOGE("canSkipComposition invalid context");
        return false;
    }

    hwc_composer_device_t* dev = (hwc_composer_device_t *)(ctx);
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    if (hwcModule->compositionType == COMPOSITION_TYPE_CPU)
        return false;

    //Video / Camera case
    if (yuvBufferCount == 1) {
        //If the previousLayerCount is anything other than the current count, it
        //means something changed and we need to compose atleast once to FB.
        if (currentLayerCount != ctx->previousLayerCount) {
            ctx->previousLayerCount = currentLayerCount;
            return false;
        }
        // We either have only one overlay layer or we have
        // all non-updating UI layers.
        // We can skip the composition of the UI layers.
        if ((currentLayerCount == 1) ||
            ((currentLayerCount - 1) == numLayersNotUpdating)) {
            return true;
        }
    } else {
        ctx->previousLayerCount = -1;
    }
    return false;
}

inline void getLayerResolution(const hwc_layer_t* layer, int& width, int& height)
{
   hwc_rect_t displayFrame  = layer->displayFrame;

   width = displayFrame.right - displayFrame.left;
   height = displayFrame.bottom - displayFrame.top;
}

static bool canUseCopybit(const framebuffer_device_t* fbDev, const hwc_layer_list_t* list) {

    if(!fbDev) {
       LOGE("ERROR: %s : fb device is invalid",__func__);
       return false;
    }

    if (!list)
        return false;

    int fb_w = fbDev->width;
    int fb_h = fbDev->height;

    /*
     * Use copybit only when we need to blit
     * max 2 full screen sized regions
     */

    unsigned int renderArea = 0;

    for(int i = 0; i < list->numHwLayers; i++ ) {
        int w, h;
        getLayerResolution(&list->hwLayers[i], w, h);
        renderArea += w*h;
    }

    return (renderArea <= (2 * fb_w * fb_h));
}

static void handleHDMIStateChange(hwc_composer_device_t *dev, int externaltype) {
#if defined HDMI_DUAL_DISPLAY
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    //Route the event to fbdev only if we are in default mirror mode
    if(ExtDispOnly::isModeOn() == false) {
        framebuffer_device_t *fbDev = hwcModule->fbDevice;
        if (fbDev) {
            fbDev->enableHDMIOutput(fbDev, externaltype);
        }

        if(ctx && ctx->mOverlayLibObject) {
            overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
            if (!externaltype) {
                // Close the external overlay channels if HDMI is disconnected
                ovLibObject->closeExternalChannel();
            }
        }
    }
#endif
}

/*
 * function to set the status of external display in hwc
 * Just mark flags and do stuff after eglSwapBuffers
 * externaltype - can be HDMI, WIFI or OFF
 */
static void hwc_enableHDMIOutput(hwc_composer_device_t *dev, int externaltype) {
#if defined HDMI_DUAL_DISPLAY
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    framebuffer_device_t *fbDev = hwcModule->fbDevice;
    overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
    if(externaltype && ctx->mHDMIEnabled &&
            (externaltype != ctx->mHDMIEnabled)) {
        // Close the current external display - as the SF will
        // prioritize and send the correct external display HDMI/WFD
        handleHDMIStateChange(dev, 0);
    }
    // Store the external display
    ctx->mHDMIEnabled = (external_display)externaltype;
    if(ctx->mHDMIEnabled) { //On connect, allow bypass to draw once to FB
        ctx->pendingHDMI = true;
    } else { //On disconnect, close immediately (there will be no bypass)
        handleHDMIStateChange(dev, ctx->mHDMIEnabled);
    }
#endif
}

static bool isValidDestination(const framebuffer_device_t* fbDev, const hwc_rect_t& rect)
{
    if (!fbDev) {
        LOGE("%s: fbDev is null", __FUNCTION__);
        return false;
    }

    int dest_width = (rect.right - rect.left);
    int dest_height = (rect.bottom - rect.top);

    if (rect.left < 0 || rect.right < 0 || rect.top < 0 || rect.bottom < 0
        || dest_width <= 0 || dest_height <= 0) {
        LOGE("%s: destination: left=%d right=%d top=%d bottom=%d width=%d"
             "height=%d", __FUNCTION__, rect.left, rect.right, rect.top,
             rect.bottom, dest_width, dest_height);
        return false;
    }

    if ((rect.left+dest_width) > fbDev->width || (rect.top+dest_height) > fbDev->height) {
        LOGE("%s: destination out of bound params", __FUNCTION__);
        return false;
    }

    return true;
}

static int getYUVBufferCount (const hwc_layer_list_t* list) {
    int yuvBufferCount = 0;
    if (list) {
        for (size_t i=0 ; i<list->numHwLayers; i++) {
            private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
            if (hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO) &&
               !(list->hwLayers[i].flags & HWC_DO_NOT_USE_OVERLAY)) {
                yuvBufferCount++;
                if (yuvBufferCount > 1) {
                    break;
                }
            }
        }
    }
    return yuvBufferCount;
}

static int getS3DVideoFormat (const hwc_layer_list_t* list) {
    int s3dFormat = 0;
    if (list) {
        for (size_t i=0; i<list->numHwLayers; i++) {
            private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
            if (hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO))
                s3dFormat = FORMAT_3D_INPUT(hnd->format);
            if (s3dFormat)
                break;
        }
    }
    return s3dFormat;
}

static int getS3DFormat (const hwc_layer_list_t* list) {
    int s3dFormat = 0;
    if (list) {
        for (size_t i=0; i<list->numHwLayers; i++) {
            private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
            if (hnd)
                s3dFormat = FORMAT_3D_INPUT(hnd->format);
            if (s3dFormat)
                break;
        }
    }
    return s3dFormat;
}


static int getLayerS3DFormat (hwc_layer_t &layer) {
    int s3dFormat = 0;
    private_handle_t *hnd = (private_handle_t *)layer.handle;
    if (hnd)
        s3dFormat = FORMAT_3D_INPUT(hnd->format);
    return s3dFormat;
}
static bool isS3DCompositionRequired() {
#ifdef HDMI_AS_PRIMARY
    return overlay::is3DTV();
#endif
    return false;
}

static void markUILayerForS3DComposition (hwc_layer_t &layer, int s3dVideoFormat) {
#ifdef HDMI_AS_PRIMARY
    layer.compositionType = HWC_FRAMEBUFFER;
    switch(s3dVideoFormat) {
        case HAL_3D_IN_SIDE_BY_SIDE_L_R:
        case HAL_3D_IN_SIDE_BY_SIDE_R_L:
            layer.hints |= HWC_HINT_DRAW_S3D_SIDE_BY_SIDE;
            break;
        case HAL_3D_IN_TOP_BOTTOM:
            layer.hints |= HWC_HINT_DRAW_S3D_TOP_BOTTOM;
            break;
        default:
            LOGE("%s: Unknown S3D input format 0x%x", __FUNCTION__, s3dVideoFormat);
            break;
    }
#endif
    return;
}

static int getLayersNotUpdatingCount(const hwc_layer_list_t* list) {
    int numLayersNotUpdating = 0;
    if (list) {
        for (size_t i=0 ; i<list->numHwLayers; i++) {
            private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
            if (hnd && (hnd->bufferType != BUFFER_TYPE_VIDEO) &&
               list->hwLayers[i].flags & HWC_LAYER_NOT_UPDATING)
               numLayersNotUpdating++;
        }
    }
    return numLayersNotUpdating;
}

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list) {

    hwc_context_t* ctx = (hwc_context_t*)(dev);

    if(!ctx) {
        LOGE("hwc_prepare invalid context");
        return -1;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    if (!hwcModule) {
        LOGE("hwc_prepare invalid module");
#ifdef COMPOSITION_BYPASS
        unlockPreviousBypassBuffers(ctx);
        unsetBypassBufferLockState(ctx);
#endif
        unlockPreviousOverlayBuffer(ctx);
        ExtDispOnly::close();
        return -1;
    }

    int yuvBufferCount = 0;
    int layerType = 0;
    bool isS3DCompositionNeeded = false;
    int s3dVideoFormat = 0;
    int numLayersNotUpdating = 0;
    bool useCopybit = false;
    bool isSkipLayerPresent = false;
    bool skipComposition = false;

    if (list) {
        useCopybit = canUseCopybit(hwcModule->fbDevice, list);
        yuvBufferCount = getYUVBufferCount(list);
        numLayersNotUpdating = getLayersNotUpdatingCount(list);
        skipComposition = canSkipComposition(ctx, yuvBufferCount,
                                list->numHwLayers, numLayersNotUpdating);

        if (yuvBufferCount == 1) {
            s3dVideoFormat = getS3DVideoFormat(list);
            if (s3dVideoFormat)
                isS3DCompositionNeeded = isS3DCompositionRequired();
        } else if((s3dVideoFormat = getS3DFormat(list))){
            if (s3dVideoFormat)
                isS3DCompositionNeeded = isS3DCompositionRequired();
        } else {
            unlockPreviousOverlayBuffer(ctx);
        }

        if (list->flags & HWC_GEOMETRY_CHANGED) {
            if (yuvBufferCount == 1) {
                // Inform the gralloc of the current video overlay status
                setVideoOverlayStatusInGralloc(ctx, true);
            }
        }

        for (size_t i=0 ; i<list->numHwLayers ; i++) {
            private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;

            // If there is a single Fullscreen layer, we can bypass it - TBD
            // If there is only one video/camera buffer, we can bypass itn
            if (list->hwLayers[i].flags & HWC_SKIP_LAYER) {
                // During the animaton UI layers are marked as SKIP
                // need to still mark the layer for S3D composition
                isSkipLayerPresent = true;
                skipComposition = false;
                //Reset count, so that we end up composing once after animation
                //is over, in case of overlay.
                ctx->previousLayerCount = -1;

                if (isS3DCompositionNeeded)
                    markUILayerForS3DComposition(list->hwLayers[i], s3dVideoFormat);

                ssize_t layer_countdown = ((ssize_t)i);
                // Mark every layer below the SKIP layer to be composed by the GPU
                while (layer_countdown >= 0)
                {
                    private_handle_t *countdown_handle =
                               (private_handle_t *)list->hwLayers[layer_countdown].handle;
                    if (countdown_handle && (countdown_handle->bufferType == BUFFER_TYPE_VIDEO)
                        && (yuvBufferCount == 1)) {
                        unlockPreviousOverlayBuffer(ctx);
                    }
                    list->hwLayers[layer_countdown].compositionType = HWC_FRAMEBUFFER;
                    list->hwLayers[layer_countdown].hints &= ~HWC_HINT_CLEAR_FB;
                    layer_countdown--;
                }
            } else if (hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO) && (yuvBufferCount == 1)) {
                int flags = WAIT_FOR_VSYNC;
                flags |= (1 == list->numHwLayers) ? DISABLE_FRAMEBUFFER_FETCH : 0;
                if (!isValidDestination(hwcModule->fbDevice, list->hwLayers[i].displayFrame)) {
                    list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
                    //Even though there are no skip layers, animation is still
                    //ON and in its final stages.
                    //Reset count, so that we end up composing once after animation
                    //is done, if overlay is used.
                    ctx->previousLayerCount = -1;
                    skipComposition = false;
#ifdef USE_OVERLAY
                } else if(prepareOverlay(ctx, &(list->hwLayers[i]), flags) == 0) {
                    list->hwLayers[i].compositionType = HWC_USE_OVERLAY;
                    list->hwLayers[i].hints |= HWC_HINT_CLEAR_FB;
                    // We've opened the channel. Set the state to open.
                    ctx->hwcOverlayStatus = HWC_OVERLAY_OPEN;
#endif
                } else if (hwcModule->compositionType & (COMPOSITION_TYPE_C2D|
                            COMPOSITION_TYPE_MDP)) {
                    //Fail safe path: If drawing with overlay fails,

                    //Use C2D if available.
                    list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
                } else {
                    //If C2D is not enabled fall back to GPU.
                    list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
                }
                if (HWC_USE_OVERLAY != list->hwLayers[i].compositionType) {
                    unlockPreviousOverlayBuffer(ctx);
                    skipComposition = false;
                }
            } else if (getLayerS3DFormat(list->hwLayers[i])) {
                int flags = WAIT_FOR_VSYNC;
                flags |= (1 == list->numHwLayers) ? DISABLE_FRAMEBUFFER_FETCH : 0;
#ifdef USE_OVERLAY
                if(prepareOverlay(ctx, &(list->hwLayers[i]), flags) == 0) {
                    list->hwLayers[i].compositionType = HWC_USE_OVERLAY;
                    list->hwLayers[i].hints |= HWC_HINT_CLEAR_FB;
                    // We've opened the channel. Set the state to open.
                    ctx->hwcOverlayStatus = HWC_OVERLAY_OPEN;
                }
#endif
            } else if (isS3DCompositionNeeded) {
                markUILayerForS3DComposition(list->hwLayers[i], s3dVideoFormat);
            } else if (list->hwLayers[i].flags & HWC_USE_ORIGINAL_RESOLUTION) {
                list->hwLayers[i].compositionType = HWC_USE_OVERLAY;
                list->hwLayers[i].hints |= HWC_HINT_CLEAR_FB;
                layerType |= HWC_ORIG_RESOLUTION;
            } else if (hnd && hnd->flags & private_handle_t::PRIV_FLAGS_EXTERNAL_ONLY) {
                //handle later after other layers are handled
            } else if (hnd && (hwcModule->compositionType &
                    (COMPOSITION_TYPE_C2D|COMPOSITION_TYPE_MDP))) {
                list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
            } else if ((hwcModule->compositionType == COMPOSITION_TYPE_DYN)
                    && useCopybit) {
                list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
            }
            else {
                list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
            }
        }

        //Update the stats and pipe config for external-only layers
        ExtDispOnly::update(ctx, list);

        if (skipComposition) {
            list->flags |= HWC_SKIP_COMPOSITION;
        } else {
            list->flags &= ~HWC_SKIP_COMPOSITION;
        }

#ifdef COMPOSITION_BYPASS
        bool isBypassUsed = true;
        bool isDoable = isBypassDoable(dev, yuvBufferCount, list);
        //Check if bypass is feasible
        if(isDoable && !isSkipLayerPresent) {
            if(setupBypass(ctx, list)) {
                setBypassLayerFlags(ctx, list);
                ctx->bypassState = BYPASS_ON;
            } else {
                LOGE_IF(BYPASS_DEBUG,"%s: Bypass setup Failed",__FUNCTION__);
                isBypassUsed = false;
            }
        } else {
            LOGE_IF(BYPASS_DEBUG,"%s: Bypass not possible[%d,%d]",__FUNCTION__,
                       isDoable, !isSkipLayerPresent );
            isBypassUsed = false;
        }

        //Reset bypass states
        if(!isBypassUsed) {
            ctx->nPipesUsed = 0;
            unsetBypassLayerFlags(list);
            if(ctx->bypassState == BYPASS_ON) {
                ctx->bypassState = BYPASS_OFF_PENDING;
            }
        }
#endif
    } else {
#ifdef COMPOSITION_BYPASS
        unlockPreviousBypassBuffers(ctx);
        unsetBypassBufferLockState(ctx);
#endif
        unlockPreviousOverlayBuffer(ctx);
    }
    return 0;
}
// ---------------------------------------------------------------------------
struct range {
    int current;
    int end;
};
struct region_iterator : public copybit_region_t {
    
    region_iterator(hwc_region_t region) {
        mRegion = region;
        r.end = region.numRects;
        r.current = 0;
        this->next = iterate;
    }

private:
    static int iterate(copybit_region_t const * self, copybit_rect_t* rect) {
        if (!self || !rect) {
            LOGE("iterate invalid parameters");
            return 0;
        }

        region_iterator const* me = static_cast<region_iterator const*>(self);
        if (me->r.current != me->r.end) {
            rect->l = me->mRegion.rects[me->r.current].left;
            rect->t = me->mRegion.rects[me->r.current].top;
            rect->r = me->mRegion.rects[me->r.current].right;
            rect->b = me->mRegion.rects[me->r.current].bottom;
            me->r.current++;
            return 1;
        }
        return 0;
    }
    
    hwc_region_t mRegion;
    mutable range r; 
};

static int drawLayerUsingCopybit(hwc_composer_device_t *dev, hwc_layer_t *layer, EGLDisplay dpy,
                                 EGLSurface surface)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
         LOGE("drawLayerUsingCopybit null context ");
         return -1;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(dev->common.module);
    if(!hwcModule) {
        LOGE("drawLayerUsingCopybit null module ");
        return -1;
    }

    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(!hnd) {
        LOGE("drawLayerUsingCopybit invalid handle");
        return -1;
    }

    // Lock this buffer for read.
    genlock_lock_type lockType = GENLOCK_READ_LOCK;
    int err = genlock_lock_buffer(hnd, lockType, GENLOCK_MAX_TIMEOUT);
    if (GENLOCK_FAILURE == err) {
        LOGE("%s: genlock_lock_buffer(READ) failed", __FUNCTION__);
        return -1;
    }
    //render buffer
    android_native_buffer_t *renderBuffer = (android_native_buffer_t *)eglGetRenderBufferANDROID(dpy, surface);
    if (!renderBuffer) {
        LOGE("eglGetRenderBufferANDROID returned NULL buffer");
        genlock_unlock_buffer(hnd);
        return -1;
    }
    private_handle_t *fbHandle = (private_handle_t *)renderBuffer->handle;
    if(!fbHandle) {
        LOGE("Framebuffer handle is NULL");
        genlock_unlock_buffer(hnd);
        return -1;
    }
    int alignment = 32;
    if( HAL_PIXEL_FORMAT_RGB_565 == fbHandle->format )
        alignment = 16;
     // Set the copybit source:
    copybit_image_t src;
    src.w = ALIGN(hnd->width, alignment);
    src.h = hnd->height;
    src.format = hnd->format;
    src.base = (void *)hnd->base;
    src.handle = (native_handle_t *)layer->handle;
    src.horiz_padding = src.w - hnd->width;
    // Initialize vertical padding to zero for now,
    // this needs to change to accomodate vertical stride
    // if needed in the future
    src.vert_padding = 0;

    // Copybit source rect
    hwc_rect_t sourceCrop = layer->sourceCrop;
    copybit_rect_t srcRect = {sourceCrop.left, sourceCrop.top,
                              sourceCrop.right,
                              sourceCrop.bottom};

    // Copybit destination rect
    hwc_rect_t displayFrame = layer->displayFrame;
    copybit_rect_t dstRect = {displayFrame.left, displayFrame.top,
                              displayFrame.right,
                              displayFrame.bottom};

    // Copybit dst
    copybit_image_t dst;
    dst.w = ALIGN(fbHandle->width,alignment);
    dst.h = fbHandle->height;
    dst.format = fbHandle->format;
    dst.base = (void *)fbHandle->base;
    dst.handle = (native_handle_t *)renderBuffer->handle;

    copybit_device_t *copybit = hwcModule->copybitEngine;

    int32_t screen_w        = displayFrame.right - displayFrame.left;
    int32_t screen_h        = displayFrame.bottom - displayFrame.top;
    int32_t src_crop_width  = sourceCrop.right - sourceCrop.left;
    int32_t src_crop_height = sourceCrop.bottom -sourceCrop.top;

    float copybitsMaxScale = (float)copybit->get(copybit,COPYBIT_MAGNIFICATION_LIMIT);
    float copybitsMinScale = (float)copybit->get(copybit,COPYBIT_MINIFICATION_LIMIT);

    if((layer->transform == HWC_TRANSFORM_ROT_90) ||
                           (layer->transform == HWC_TRANSFORM_ROT_270)) {
        //swap screen width and height
        int tmp = screen_w;
        screen_w  = screen_h;
        screen_h = tmp;
    }
    private_handle_t *tmpHnd = NULL;

    if(screen_w <=0 || screen_h<=0 ||src_crop_width<=0 || src_crop_height<=0 ) {
        LOGE("%s: wrong params for display screen_w=%d src_crop_width=%d screen_w=%d \
                                src_crop_width=%d", __FUNCTION__, screen_w,
                                src_crop_width,screen_w,src_crop_width);
        genlock_unlock_buffer(hnd);
        return -1;
    }

    float dsdx = (float)screen_w/src_crop_width;
    float dtdy = (float)screen_h/src_crop_height;

    float scaleLimitMax = copybitsMaxScale * copybitsMaxScale;
    float scaleLimitMin = copybitsMinScale * copybitsMinScale;
    if(dsdx > scaleLimitMax || dtdy > scaleLimitMax || dsdx < 1/scaleLimitMin || dtdy < 1/scaleLimitMin) {
        LOGE("%s: greater than max supported size dsdx=%f dtdy=%f scaleLimitMax=%f scaleLimitMin=%f", __FUNCTION__,dsdx,dtdy,scaleLimitMax,1/scaleLimitMin);
        genlock_unlock_buffer(hnd);
        return -1;
    }
    if(dsdx > copybitsMaxScale || dtdy > copybitsMaxScale || dsdx < 1/copybitsMinScale || dtdy < 1/copybitsMinScale){
        // The requested scale is out of the range the hardware
        // can support.
       LOGD("%s:%d::Need to scale twice dsdx=%f, dtdy=%f,copybitsMaxScale=%f,copybitsMinScale=%f,screen_w=%d,screen_h=%d \
                  src_crop_width=%d src_crop_height=%d",__FUNCTION__,__LINE__,
                  dsdx,dtdy,copybitsMaxScale,1/copybitsMinScale,screen_w,screen_h,src_crop_width,src_crop_height);

       //Driver makes width and height as even
       //that may cause wrong calculation of the ratio
       //in display and crop.Hence we make
       //crop width and height as even.
       src_crop_width  = (src_crop_width/2)*2;
       src_crop_height = (src_crop_height/2)*2;

       int tmp_w =  src_crop_width;
       int tmp_h =  src_crop_height;

       if (dsdx > copybitsMaxScale || dtdy > copybitsMaxScale ){
         tmp_w = src_crop_width*copybitsMaxScale;
         tmp_h = src_crop_height*copybitsMaxScale;
       }else if (dsdx < 1/copybitsMinScale ||dtdy < 1/copybitsMinScale ){
         tmp_w = src_crop_width/copybitsMinScale;
         tmp_h = src_crop_height/copybitsMinScale;
         tmp_w  = (tmp_w/2)*2;
         tmp_h = (tmp_h/2)*2;
       }
       LOGD("%s:%d::tmp_w = %d,tmp_h = %d",__FUNCTION__,__LINE__,tmp_w,tmp_h);

       int usage = GRALLOC_USAGE_PRIVATE_ADSP_HEAP |
                   GRALLOC_USAGE_PRIVATE_MM_HEAP;

       if (0 == alloc_buffer(&tmpHnd, tmp_w, tmp_h, fbHandle->format, usage)){
            copybit_image_t tmp_dst;
            copybit_rect_t tmp_rect;
            tmp_dst.w = tmp_w;
            tmp_dst.h = tmp_h;
            tmp_dst.format = tmpHnd->format;
            tmp_dst.handle = tmpHnd;
            tmp_dst.horiz_padding = src.horiz_padding;
            tmp_dst.vert_padding = src.vert_padding;
            tmp_rect.l = 0;
            tmp_rect.t = 0;
            tmp_rect.r = tmp_dst.w;
            tmp_rect.b = tmp_dst.h;
            //create one clip region
            hwc_rect tmp_hwc_rect = {0,0,tmp_rect.r,tmp_rect.b};
            hwc_region_t tmp_hwc_reg = {1,(hwc_rect_t const*)&tmp_hwc_rect};
            region_iterator tmp_it(tmp_hwc_reg);
            copybit->set_parameter(copybit,COPYBIT_TRANSFORM,0);
            copybit->set_parameter(copybit, COPYBIT_PLANE_ALPHA,
                        (layer->blending == HWC_BLENDING_NONE) ? -1 : layer->alpha);
            err = copybit->stretch(copybit,&tmp_dst, &src, &tmp_rect, &srcRect, &tmp_it);
            if(err < 0){
                LOGE("%s:%d::tmp copybit stretch failed",__FUNCTION__,__LINE__);
                if(tmpHnd)
                    free_buffer(tmpHnd);
                genlock_unlock_buffer(hnd);
                return err;
            }
            // copy new src and src rect crop
            src = tmp_dst;
            srcRect = tmp_rect;
      }
    }
    // Copybit region
    hwc_region_t region = layer->visibleRegionScreen;
    region_iterator copybitRegion(region);

    copybit->set_parameter(copybit, COPYBIT_FRAMEBUFFER_WIDTH, renderBuffer->width);
    copybit->set_parameter(copybit, COPYBIT_FRAMEBUFFER_HEIGHT, renderBuffer->height);
    copybit->set_parameter(copybit, COPYBIT_TRANSFORM, layer->transform);
    copybit->set_parameter(copybit, COPYBIT_PLANE_ALPHA,
                           (layer->blending == HWC_BLENDING_NONE) ? -1 : layer->alpha);
    copybit->set_parameter(copybit, COPYBIT_PREMULTIPLIED_ALPHA,
                           (layer->blending == HWC_BLENDING_PREMULT)? COPYBIT_ENABLE : COPYBIT_DISABLE);
    copybit->set_parameter(copybit, COPYBIT_DITHER,
                            (dst.format == HAL_PIXEL_FORMAT_RGB_565)? COPYBIT_ENABLE : COPYBIT_DISABLE);
    err = copybit->stretch(copybit, &dst, &src, &dstRect, &srcRect, &copybitRegion);

    if(tmpHnd)
        free_buffer(tmpHnd);

    if(err < 0)
        LOGE("%s: copybit stretch failed",__FUNCTION__);

    // Unlock this buffer since copybit is done with it.
    err = genlock_unlock_buffer(hnd);
    if (GENLOCK_FAILURE == err) {
        LOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
    }

    return err;
}

static int drawLayerUsingOverlay(hwc_context_t *ctx, hwc_layer_t *layer)
{
    if (ctx && ctx->mOverlayLibObject) {
        private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(ctx->device.common.module);
        if (!hwcModule) {
            LOGE("drawLayerUsingLayer null module ");
            return -1;
        }
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
        int ret = 0;

        // Lock this buffer for read.
        if (GENLOCK_NO_ERROR != genlock_lock_buffer(hnd, GENLOCK_READ_LOCK,
                                                    GENLOCK_MAX_TIMEOUT)) {
            LOGE("%s: genlock_lock_buffer(READ) failed", __FUNCTION__);
            return -1;
        }

        ret = ovLibObject->queueBuffer(hnd);

        // Unlock the previously locked buffer, since the overlay has completed reading the buffer
        unlockPreviousOverlayBuffer(ctx);

        if (!ret) {
            LOGE("drawLayerUsingOverlay queueBuffer failed");
            // Unlock the buffer handle
            genlock_unlock_buffer(hnd);
            ctx->previousOverlayHandle = NULL;
        } else {
            // Store the current buffer handle as the one that is to be unlocked after
            // the next overlay play call.
            ctx->previousOverlayHandle = hnd;
            hnd->flags |= private_handle_t::PRIV_FLAGS_HWC_LOCK;
        }

        return ret;
    }
    return -1;
}

#ifdef COMPOSITION_BYPASS
static int drawLayerUsingBypass(hwc_context_t *ctx, hwc_layer_t *layer, int layer_index) {

    int index = getLayerbypassIndex(layer);

    if(index < 0) {
        LOGE("%s: Invalid bypass index (%d)", __FUNCTION__, index);
        return -1;
    }

    if (ctx && ctx->mOvUI[index]) {
        overlay::OverlayUI *ovUI = ctx->mOvUI[index];
        int ret = 0;

        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(!hnd) {
            LOGE("%s handle null", __FUNCTION__);
            return -1;
        }

        ctx->bypassBufferLockState[index] = BYPASS_BUFFER_UNLOCKED;

        if (GENLOCK_FAILURE == genlock_lock_buffer(hnd, GENLOCK_READ_LOCK,
                                                   GENLOCK_MAX_TIMEOUT)) {
            LOGE("%s: genlock_lock_buffer(READ) failed", __FUNCTION__);
            return -1;
        }

        ctx->bypassBufferLockState[index] = BYPASS_BUFFER_LOCKED;

        LOGE_IF(BYPASS_DEBUG,"%s: Bypassing layer: %p using pipe: %d",__FUNCTION__, layer, index );

        ret = ovUI->queueBuffer(hnd);

        if (ret) {
            // Unlock the locked buffer
            if (GENLOCK_FAILURE == genlock_unlock_buffer(hnd)) {
                LOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
            }
            ctx->bypassBufferLockState[index] = BYPASS_BUFFER_UNLOCKED;
            return -1;
        }
    }
    return 0;
}
#endif

static int hwc_set(hwc_composer_device_t *dev,
        hwc_display_t dpy,
        hwc_surface_t sur,
        hwc_layer_list_t* list)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
        LOGE("hwc_set invalid context");
        ExtDispOnly::close();
        return -1;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    if (!hwcModule) {
        LOGE("hwc_set invalid module");
#ifdef COMPOSITION_BYPASS
        unlockPreviousBypassBuffers(ctx);
        unsetBypassBufferLockState(ctx);
#endif
        ExtDispOnly::close();
        unlockPreviousOverlayBuffer(ctx);
        return -1;
    }

    int ret = 0;
    if (list) {
        for (size_t i=0; i<list->numHwLayers; i++) {
            if (list->hwLayers[i].flags & HWC_SKIP_LAYER) {
                continue;
#ifdef COMPOSITION_BYPASS
            } else if (list->hwLayers[i].flags & HWC_COMP_BYPASS) {
                drawLayerUsingBypass(ctx, &(list->hwLayers[i]), i);
#endif
            } else if (list->hwLayers[i].compositionType == HWC_USE_OVERLAY) {
                drawLayerUsingOverlay(ctx, &(list->hwLayers[i]));
            } else if (list->flags & HWC_SKIP_COMPOSITION) {
                continue;
            } else if (list->hwLayers[i].compositionType == HWC_USE_COPYBIT) {
                drawLayerUsingCopybit(dev, &(list->hwLayers[i]), (EGLDisplay)dpy, (EGLSurface)sur);
            }
        }
    }
    

    bool canSkipComposition = list && list->flags & HWC_SKIP_COMPOSITION;
#ifdef COMPOSITION_BYPASS
    unlockPreviousBypassBuffers(ctx);
    storeLockedBypassHandle(list, ctx);
    // We have stored the handles, unset the current lock states in the context.
    unsetBypassBufferLockState(ctx);
    closeExtraPipes(ctx);
#if BYPASS_DEBUG
    if(canSkipComposition)
        LOGE("%s: skipping eglSwapBuffer call", __FUNCTION__);
#endif
#endif
    // Do not call eglSwapBuffers if we the skip composition flag is set on the list.
    if (dpy && sur && !canSkipComposition) {
        EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
        if (!sucess) {
            ret = HWC_EGL_ERROR;
        } else {
            CALC_FPS();
        }
    }
#if defined HDMI_DUAL_DISPLAY
    if(ctx->pendingHDMI) {
        handleHDMIStateChange(dev, ctx->mHDMIEnabled);
        ctx->pendingHDMI = false;
    }
#endif

    hwc_closeOverlayChannels(ctx);
    int yuvBufferCount = getYUVBufferCount(list);
    setHWCOverlayStatus(ctx, yuvBufferCount);

    return ret;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    if(!dev) {
        LOGE("hwc_device_close null device pointer");
        return -1;
    }

    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
            ctx->device.common.module);
    // Close the overlay and copybit modules
    if(hwcModule->copybitEngine) {
        copybit_close(hwcModule->copybitEngine);
        hwcModule->copybitEngine = NULL;
    }
    if(hwcModule->fbDevice) {
        framebuffer_close(hwcModule->fbDevice);
        hwcModule->fbDevice = NULL;
    }

    unlockPreviousOverlayBuffer(ctx);

    if (ctx) {
         delete ctx->mOverlayLibObject;
         ctx->mOverlayLibObject = NULL;
#ifdef COMPOSITION_BYPASS
            for(int i = 0; i < MAX_BYPASS_LAYERS; i++) {
                delete ctx->mOvUI[i];
            }
            unlockPreviousBypassBuffers(ctx);
            unsetBypassBufferLockState(ctx);
#endif
        ExtDispOnly::close();
        ExtDispOnly::destroy();

        free(ctx);
    }
    return 0;
}

/*****************************************************************************/
static int hwc_module_initialize(struct private_hwc_module_t* hwcModule)
{

    // Open the overlay and copybit modules
    hw_module_t const *module;
    if (hw_get_module(COPYBIT_HARDWARE_MODULE_ID, &module) == 0) {
        copybit_open(module, &(hwcModule->copybitEngine));
    }
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0) {
        framebuffer_open(module, &(hwcModule->fbDevice));
    }

    // get the current composition type
    char property[PROPERTY_VALUE_MAX];
    if (property_get("debug.sf.hw", property, NULL) > 0) {
        if(atoi(property) == 0) {
            //debug.sf.hw = 0
            hwcModule->compositionType = COMPOSITION_TYPE_CPU;
        } else { //debug.sf.hw = 1
            // Get the composition type
            property_get("debug.composition.type", property, NULL);
            if (property == NULL) {
                hwcModule->compositionType = COMPOSITION_TYPE_GPU;
            } else if ((strncmp(property, "mdp", 3)) == 0) {
                hwcModule->compositionType = COMPOSITION_TYPE_MDP;
            } else if ((strncmp(property, "c2d", 3)) == 0) {
                hwcModule->compositionType = COMPOSITION_TYPE_C2D;
            } else if ((strncmp(property, "dyn", 3)) == 0) {
                hwcModule->compositionType = COMPOSITION_TYPE_DYN;
            } else {
                hwcModule->compositionType = COMPOSITION_TYPE_GPU;
            }

            if(!hwcModule->copybitEngine)
                hwcModule->compositionType = COMPOSITION_TYPE_GPU;
        }
    } else { //debug.sf.hw is not set. Use cpu composition
        hwcModule->compositionType = COMPOSITION_TYPE_CPU;
    }

    //Check if composition bypass is enabled
    if(property_get("ro.sf.compbypass.enable", property, NULL) > 0) {
        if(atoi(property) == 1) {
            hwcModule->isBypassEnabled = true;
        }
    }

    CALC_INIT();

    return 0;
}


static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;

    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
        private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>
                                        (const_cast<hw_module_t*>(module));
        hwc_module_initialize(hwcModule);
        struct hwc_context_t *dev;
        dev = (hwc_context_t*)malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));
#ifdef USE_OVERLAY
        dev->mOverlayLibObject = new overlay::Overlay();
        if(overlay::initOverlay() == -1)
            LOGE("overlay::initOverlay() ERROR!!");
#else
        dev->mOverlayLibObject = NULL;
#endif
#ifdef COMPOSITION_BYPASS
        for(int i = 0; i < MAX_BYPASS_LAYERS; i++) {
            dev->mOvUI[i] = new overlay::OverlayUI();
            dev->previousBypassHandle[i] = NULL;
        }
        unsetBypassBufferLockState(dev);
        dev->bypassState = BYPASS_OFF;
#endif
        ExtDispOnly::init();
#if defined HDMI_DUAL_DISPLAY
        dev->mHDMIEnabled = EXT_DISPLAY_OFF;
        dev->pendingHDMI = false;
#endif
        dev->previousOverlayHandle = NULL;
        dev->hwcOverlayStatus = HWC_OVERLAY_CLOSED;
        dev->previousLayerCount = -1;
        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = hwc_device_close;

        dev->device.prepare = hwc_prepare;
        dev->device.set = hwc_set;
        dev->device.enableHDMIOutput = hwc_enableHDMIOutput;
        *device = &dev->device.common;

        status = 0;
    }
    return status;
}
