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

/*****************************************************************************/
#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))
#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

enum HWCLayerType{
    HWC_SINGLE_VIDEO           = 0x1,
    HWC_ORIG_RESOLUTION        = 0x2,
    HWC_S3D_LAYER              = 0x4,
    HWC_STOP_UI_MIRRORING_MASK = 0xF
};

#ifdef COMPOSITION_BYPASS
enum BypassState {
    BYPASS_ON,
    BYPASS_OFF,
    BYPASS_OFF_PENDING,
};

enum {
    MAX_BYPASS_LAYERS = 2,
    ANIM_FRAME_COUNT = 30,
};

enum BypassBufferLockState {
    BYPASS_BUFFER_UNLOCKED,
    BYPASS_BUFFER_LOCKED,
};
#endif

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
    int animCount;
    BypassState bypassState;
#endif
#if defined HDMI_DUAL_DISPLAY
    bool mHDMIEnabled;
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
    return 0;
}

#ifdef COMPOSITION_BYPASS
// To-do: Merge this with other blocks & move them to a separate file.
void unlockPreviousBypassBuffers(hwc_context_t* ctx) {
    // Unlock the previous bypass buffers. We can blindly unlock the buffers here,
    // because buffers will be in this list only if the lock was successfully acquired.
    for(int i = 0; i < MAX_BYPASS_LAYERS; i++) {
        if (ctx->previousBypassHandle[i]) {
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
}

void closeBypass(hwc_context_t* ctx) {
        unlockPreviousBypassBuffers(ctx);
        for (int index = 0 ; index < MAX_BYPASS_LAYERS; index++) {
            ctx->mOvUI[index]->closeChannel();
        }
        #ifdef DEBUG
            LOGE("%s", __FUNCTION__);
        #endif
    }
#endif

/*
 * Configures mdp pipes
 */
static int prepareOverlay(hwc_context_t *ctx, hwc_layer_t *layer, const bool waitForVsync) {
     int ret = 0;

#ifdef COMPOSITION_BYPASS
     if(ctx && (ctx->bypassState != BYPASS_OFF)) {
        closeBypass(ctx);
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

        ret = ovLibObject->setSource(info, layer->transform,
                            (ovLibObject->getHDMIStatus()?true:false), waitForVsync);
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

    bool compCountChanged = false;
    if (yuvBufferCount == 1) {
        if (currentLayerCount != ctx->previousLayerCount) {
            compCountChanged = true;
            ctx->previousLayerCount = currentLayerCount;
        }

        if (!compCountChanged) {
            if ((currentLayerCount == 1) ||
                ((currentLayerCount-1) == numLayersNotUpdating)) {
                // We either have only one overlay layer or we have
                // all the non-UI layers not updating. In this case
                // we can skip the composition of the UI layers.
                return true;
            }
        }
    } else {
        ctx->previousLayerCount = -1;
    }
    return false;
}

static bool isFullScreenUpdate(const framebuffer_device_t* fbDev, const hwc_layer_list_t* list) {

    if(!fbDev) {
       LOGE("ERROR: %s : fb device is invalid",__func__);
       return false;
    }

    int fb_w = fbDev->width;
    int fb_h = fbDev->height;

    /*
     *  We have full screen condition when
     * 1. We have 1 layer to compose
     *    a. layers dest rect equals display resolution.
     * 2. We have 2 layers to compose
     *    a. Sum of their dest rects equals display resolution.
     */

    if(list->numHwLayers == 1)
    {
        hwc_rect_t rect = list->hwLayers[0].displayFrame;

        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;

        int transform = list->hwLayers[0].transform;

        if(transform & (HWC_TRANSFORM_ROT_90 | HWC_TRANSFORM_ROT_270))
            return ((fb_w == h) && (fb_h == w));
        else
            return ((fb_h == h) && (fb_w == w));
    }

    if(list->numHwLayers == 2) {

        hwc_rect_t rect_1 = list->hwLayers[0].displayFrame;
        hwc_rect_t rect_2 = list->hwLayers[1].displayFrame;

        int transform_1 = list->hwLayers[0].transform;
        int transform_2 = list->hwLayers[1].transform;

        int w1 = rect_1.right - rect_1.left;
        int h1 = rect_1.bottom - rect_1.top;
        int w2 = rect_2.right - rect_2.left;
        int h2 = rect_2.bottom - rect_2.top;

        if(transform_1 == transform_2) {
            if(transform_1 & (HWC_TRANSFORM_ROT_90 | HWC_TRANSFORM_ROT_270)) {
                if((fb_w == (w1 + w2)) && (fb_h == h1) && (fb_h == h2))
                    return true;
            } else {
                if((fb_w == w1) && (fb_w == w2) && (fb_h == (h1 + h2)))
                    return true;
            }
        }
    }
    return false;
}

#ifdef COMPOSITION_BYPASS
/*
 * Configures pipe(s) for composition bypass
 */
static int prepareBypass(hwc_context_t *ctx, hwc_layer_t *layer, int index,
        int lastLayerIndex) {
    if (ctx && ctx->mOvUI[index]) {
        private_hwc_module_t* hwcModule = reinterpret_cast<
                private_hwc_module_t*>(ctx->device.common.module);
        if (!hwcModule) {
            LOGE("prepareBypass null module ");
            return -1;
        }
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(!hnd) {
            LOGE("prepareBypass handle null");
            return -1;
        }
        hwc_rect_t sourceCrop = layer->sourceCrop;
        if((sourceCrop.right - sourceCrop.left) > hwcModule->fbDevice->width ||
                (sourceCrop.bottom - sourceCrop.top) > hwcModule->fbDevice->height) {
            ctx->animCount = ANIM_FRAME_COUNT;
            return -1;
        }
        overlay::OverlayUI *ovUI = ctx->mOvUI[index];
        int ret = 0;
        int orientation = layer->transform;
        overlay_buffer_info info;
        info.width = sourceCrop.right - sourceCrop.left;
        info.height = sourceCrop.bottom - sourceCrop.top;
        info.format = hnd->format;
        info.size = hnd->size;
        const bool useVGPipe = true;
        //only last layer should wait for vsync
        const bool waitForVsync = (index == lastLayerIndex);
        const int fbnum = 0;
        const bool isFg = (index == 0);
        //Just to differentiate zorders for different layers
        const int zorder = index;
        const bool isVGPipe = true;
        ovUI->setSource(info, orientation);
        ovUI->setDisplayParams(fbnum, waitForVsync, isFg, zorder, isVGPipe);

        hwc_rect_t displayFrame = layer->displayFrame;
        ovUI->setPosition(displayFrame.left, displayFrame.top,
                (displayFrame.right - displayFrame.left),
                (displayFrame.bottom - displayFrame.top));
        if(ovUI->commit() != overlay::NO_ERROR) {
            LOGE("%s: Bypass Overlay Commit failed", __FUNCTION__);
            return -1;
        }
    }
    return 0;
}

static int drawLayerUsingBypass(hwc_context_t *ctx, hwc_layer_t *layer,
        int index) {
    if (ctx && ctx->mOvUI[index]) {
        overlay::OverlayUI *ovUI = ctx->mOvUI[index];
        int ret = 0;
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        ctx->bypassBufferLockState[index] = BYPASS_BUFFER_UNLOCKED;
        if (GENLOCK_FAILURE == genlock_lock_buffer(hnd, GENLOCK_READ_LOCK,
                                                   GENLOCK_MAX_TIMEOUT)) {
            LOGE("%s: genlock_lock_buffer(READ) failed", __FUNCTION__);
            return -1;
        }
        ctx->bypassBufferLockState[index] = BYPASS_BUFFER_LOCKED;
        ret = ovUI->queueBuffer(hnd);
        if (ret) {
            LOGE("drawLayerUsingBypass queueBuffer failed");
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

/* Checks if 2 layers intersect */
static bool isIntersect(const hwc_rect_t& one, const hwc_rect_t& two) {
    hwc_rect_t result;
    result.left = max(one.left, two.left);
    result.top = max(one.top, two.top);
    result.right = min(one.right, two.right);
    result.bottom = min(one.bottom, two.bottom);
    const int width = result.right - result.left;
    const int height = result.bottom - result.top;
    const bool isEmpty = width <= 0 || height <= 0;
    return !isEmpty;
}

/* Check if layers are disjoint */
static bool isDisjoint(const hwc_layer_list_t* list) {
    //Validate supported layer range
    if(list->numHwLayers <= 0 || list->numHwLayers > MAX_BYPASS_LAYERS) {
        return false;
    }
    for(int i = 0; i < (list->numHwLayers) - 1; i++) {
        for(int j = i + 1; j < list->numHwLayers; j++) {
            if(isIntersect(list->hwLayers[i].displayFrame,
                list->hwLayers[j].displayFrame)) {
                return false;
            }
        }
    }
    return true;
}

static bool usesContiguousMemory(const hwc_layer_list_t* list) {
    for(int i = 0; i < list->numHwLayers; i++) {
        const private_handle_t *hnd =
            reinterpret_cast<const private_handle_t *>(list->hwLayers[i].handle);
        if(hnd != NULL && (hnd->flags &
                           private_handle_t::PRIV_FLAGS_NONCONTIGUOUS_MEM
                          )) {
            // Bypass cannot work for non contiguous buffers
            return false;
        }
    }
    return true;
}

/*
 * Checks if doing comp. bypass is possible.
 * It is possible if
 * 1. If video is not on
 * 2. There are 2 layers
 * 3. The memory type is contiguous
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
    // Check if memory type is contiguous
    if(!usesContiguousMemory(list))
       return false;
    //Disable bypass during animation
    if(UNLIKELY(ctx->animCount)) {
        --(ctx->animCount);
        return false;
    }
#if defined HDMI_DUAL_DISPLAY
    //Disable bypass when HDMI is enabled
    if(ctx->mHDMIEnabled || ctx->pendingHDMI) {
        return false;
    }
#endif
    return (yuvCount == 0) && (ctx->hwcOverlayStatus == HWC_OVERLAY_CLOSED) && isDisjoint(list);
}

/*
 * Bypass is not efficient if area is greater than 1280x720
 * AND rotation is necessary, since the rotator consumes
 * time greater than 1 Vsync and is sequential.
 */
inline static bool isBypassEfficient(const framebuffer_device_t* fbDev,
        const hwc_layer_list_t* list, hwc_context_t* ctx) {
    bool rotationNeeded = false;
    for(int i = 0; i < list->numHwLayers; ++i) {
        if(list->hwLayers[i].transform) {
            rotationNeeded = true;
            break;
        }
    }
    return !(rotationNeeded);
}

bool setupBypass(hwc_context_t* ctx, hwc_layer_list_t* list) {
    for (int index = 0 ; index < list->numHwLayers; index++) {
        if(prepareBypass(ctx, &(list->hwLayers[index]), index,
                list->numHwLayers - 1) != 0) {
            return false;
        }
    }
    return true;
}

void setBypassLayerFlags(hwc_context_t* ctx, hwc_layer_list_t* list) {
    for (int index = 0 ; index < list->numHwLayers; index++) {
        list->hwLayers[index].flags |= HWC_COMP_BYPASS;
        list->hwLayers[index].compositionType = HWC_USE_OVERLAY;
        #ifdef DEBUG
            LOGE("%s: layer = %d", __FUNCTION__, index);
        #endif
    }
}

void unsetBypassLayerFlags(hwc_layer_list_t* list) {
    for (int index = 0 ; index < list->numHwLayers; index++) {
        if(list->hwLayers[index].flags & HWC_COMP_BYPASS) {
            list->hwLayers[index].flags = 0;
        }
    }
}

void unsetBypassBufferLockState(hwc_context_t* ctx) {
    for (int i=0; i< MAX_BYPASS_LAYERS; i++) {
        ctx->bypassBufferLockState[i] = BYPASS_BUFFER_UNLOCKED;
    }
}

void storeLockedBypassHandle(hwc_layer_list_t* list, hwc_context_t* ctx) {
    for (int index = 0 ; index < list->numHwLayers; index++) {
        // Store the current bypass handle.
        if (list->hwLayers[index].flags & HWC_COMP_BYPASS) {
            private_handle_t *hnd = (private_handle_t*)list->hwLayers[index].handle;
            if (ctx->bypassBufferLockState[index] == BYPASS_BUFFER_LOCKED) {
                ctx->previousBypassHandle[index] = (native_handle_t*)list->hwLayers[index].handle;
                hnd->flags |= private_handle_t::PRIV_FLAGS_HWC_LOCK;
            } else
                ctx->previousBypassHandle[index] = NULL;
        }
    }
}
#endif  //COMPOSITION_BYPASS


static void handleHDMIStateChange(hwc_composer_device_t *dev) {
#if defined HDMI_DUAL_DISPLAY
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    framebuffer_device_t *fbDev = hwcModule->fbDevice;
    if (fbDev) {
        fbDev->enableHDMIOutput(fbDev, ctx->mHDMIEnabled);
    }

    if(ctx && ctx->mOverlayLibObject) {
        overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
        ovLibObject->setHDMIStatus(ctx->mHDMIEnabled);
        if (!(ctx->mHDMIEnabled)) {
            // Close the overlay channels if HDMI is disconnected
            ovLibObject->closeChannel();
        }
    }
#endif
}


/* Just mark flags and do stuff after eglSwapBuffers */
static void hwc_enableHDMIOutput(hwc_composer_device_t *dev, bool enable) {
#if defined HDMI_DUAL_DISPLAY
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    ctx->mHDMIEnabled = enable;
    if(enable) { //On connect, allow bypass to draw once to FB
        ctx->pendingHDMI = true;
    } else { //On disconnect, close immediately (there will be no bypass)
        handleHDMIStateChange(dev);
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
    if (!list || !hwcModule) {
        LOGE("hwc_prepare invalid list or module");
#ifdef COMPOSITION_BYPASS
        unlockPreviousBypassBuffers(ctx);
        unsetBypassBufferLockState(ctx);
#endif
        unlockPreviousOverlayBuffer(ctx);
        return -1;
    }

    int yuvBufferCount = 0;
    int layerType = 0;
    bool isS3DCompositionNeeded = false;
    int s3dVideoFormat = 0;
    int numLayersNotUpdating = 0;
    bool fullscreen = false;

    if (list) {
        fullscreen = isFullScreenUpdate(hwcModule->fbDevice, list);
        yuvBufferCount = getYUVBufferCount(list);

        bool skipComposition = false;
        if (yuvBufferCount == 1) {
            numLayersNotUpdating = getLayersNotUpdatingCount(list);
            skipComposition = canSkipComposition(ctx, yuvBufferCount,
                                         list->numHwLayers, numLayersNotUpdating);
            s3dVideoFormat = getS3DVideoFormat(list);
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
                if (isS3DCompositionNeeded)
                    markUILayerForS3DComposition(list->hwLayers[i], s3dVideoFormat);

                if (hwcModule->compositionType
                        & (COMPOSITION_TYPE_C2D | COMPOSITION_TYPE_MDP)) {
                    // Ensure that HWC_OVERLAY layers below skip layers do not
                    // overwrite GPU composed skip layers.
                    ssize_t layer_countdown = ((ssize_t)i) - 1;
                    while (layer_countdown >= 0)
                    {
                        // Mark every non-mdp overlay layer below the
                        // skip-layer for GPU composition.
                        switch(list->hwLayers[layer_countdown].compositionType) {
                        case HWC_FRAMEBUFFER:
                        case HWC_USE_OVERLAY:
                            break;
                        case HWC_USE_COPYBIT:
                        default:
                            list->hwLayers[layer_countdown].compositionType = HWC_FRAMEBUFFER;
                            break;
                        }
                        layer_countdown--;
                    }
                }
                continue;
            }
            if (hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO) && (yuvBufferCount == 1)) {
                bool waitForVsync = true;
                if (!isValidDestination(hwcModule->fbDevice, list->hwLayers[i].displayFrame)) {
                    list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
#ifdef USE_OVERLAY
                } else if(prepareOverlay(ctx, &(list->hwLayers[i]), waitForVsync) == 0) {
                    list->hwLayers[i].compositionType = HWC_USE_OVERLAY;
                    list->hwLayers[i].hints |= HWC_HINT_CLEAR_FB;
                    // We've opened the channel. Set the state to open.
                    ctx->hwcOverlayStatus = HWC_OVERLAY_OPEN;
#endif
                }
                else if (hwcModule->compositionType & (COMPOSITION_TYPE_C2D|
                            COMPOSITION_TYPE_MDP)) {
                    //Fail safe path: If drawing with overlay fails,

                    //Use C2D if available.
                    list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
                }
                else {
                    //If C2D is not enabled fall back to GPU.
                    list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
                }
                if (HWC_USE_OVERLAY != list->hwLayers[i].compositionType) {
                    unlockPreviousOverlayBuffer(ctx);
                    skipComposition = false;
                }
            } else if (isS3DCompositionNeeded) {
                markUILayerForS3DComposition(list->hwLayers[i], s3dVideoFormat);
            } else if (list->hwLayers[i].flags & HWC_USE_ORIGINAL_RESOLUTION) {
                list->hwLayers[i].compositionType = HWC_USE_OVERLAY;
                list->hwLayers[i].hints |= HWC_HINT_CLEAR_FB;
                layerType |= HWC_ORIG_RESOLUTION;
            }
            else if (hnd && (hwcModule->compositionType &
                    (COMPOSITION_TYPE_C2D|COMPOSITION_TYPE_MDP))) {
                list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
            } else if ((hwcModule->compositionType == COMPOSITION_TYPE_DYN)
                    && fullscreen) {
                list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
            }
            else {
                list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
            }
        }

        if (skipComposition) {
            list->flags |= HWC_SKIP_COMPOSITION;
        } else {
            list->flags &= ~HWC_SKIP_COMPOSITION;
        }

#ifdef COMPOSITION_BYPASS
        //Check if bypass is feasible
        if(isBypassDoable(dev, yuvBufferCount, list) &&
                isBypassEfficient(hwcModule->fbDevice, list, ctx)) {
            //Setup bypass
            if(setupBypass(ctx, list)) {
                //Overwrite layer flags only if setup succeeds.
                setBypassLayerFlags(ctx, list);
                list->flags |= HWC_SKIP_COMPOSITION;
                ctx->bypassState = BYPASS_ON;
            }
        } else {
            unlockPreviousBypassBuffers(ctx);
            unsetBypassLayerFlags(list);
            unsetBypassBufferLockState(ctx);
            if(ctx->bypassState == BYPASS_ON) {
                ctx->bypassState = BYPASS_OFF_PENDING;
            }
        }
#endif
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

    // Copybit region
    hwc_region_t region = layer->visibleRegionScreen;
    region_iterator copybitRegion(region);

    copybit_device_t *copybit = hwcModule->copybitEngine;
    copybit->set_parameter(copybit, COPYBIT_FRAMEBUFFER_WIDTH, renderBuffer->width);
    copybit->set_parameter(copybit, COPYBIT_FRAMEBUFFER_HEIGHT, renderBuffer->height);
    copybit->set_parameter(copybit, COPYBIT_TRANSFORM, layer->transform);
    copybit->set_parameter(copybit, COPYBIT_PLANE_ALPHA,
                           (layer->blending == HWC_BLENDING_NONE) ? 0x0 : layer->alpha);
    copybit->set_parameter(copybit, COPYBIT_PREMULTIPLIED_ALPHA,
                           (layer->blending == HWC_BLENDING_PREMULT)? COPYBIT_ENABLE : COPYBIT_DISABLE);
    err = copybit->stretch(copybit, &dst, &src, &dstRect, &srcRect, &copybitRegion);

    if(err < 0)
        LOGE("copybit stretch failed");

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

static int hwc_set(hwc_composer_device_t *dev,
        hwc_display_t dpy,
        hwc_surface_t sur,
        hwc_layer_list_t* list)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
        LOGE("hwc_set invalid context");
        return -1;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);

    framebuffer_device_t *fbDev = hwcModule->fbDevice;

    if (!list || !hwcModule) {
        LOGE("hwc_set invalid list or module");
#ifdef COMPOSITION_BYPASS
        unlockPreviousBypassBuffers(ctx);
        unsetBypassBufferLockState(ctx);
#endif
        unlockPreviousOverlayBuffer(ctx);
        return -1;
    }

    int ret = 0;
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
            break;
        }
        else if (list->hwLayers[i].compositionType == HWC_USE_COPYBIT) {
            drawLayerUsingCopybit(dev, &(list->hwLayers[i]), (EGLDisplay)dpy, (EGLSurface)sur);
        }
    }

#ifdef COMPOSITION_BYPASS
    unlockPreviousBypassBuffers(ctx);
    storeLockedBypassHandle(list, ctx);
    // We have stored the handles, unset the current lock states in the context.
    unsetBypassBufferLockState(ctx);

    //Setup for waiting until 1 FB post is done before closing bypass mode.
    if (ctx->bypassState == BYPASS_OFF_PENDING) {
        fbDev->resetBufferPostStatus(fbDev);
    }
#endif

    // Do not call eglSwapBuffers if we the skip composition flag is set on the list.
    if (!(list->flags & HWC_SKIP_COMPOSITION)) {
        EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
        if (!sucess) {
            ret = HWC_EGL_ERROR;
            LOGE("eglSwapBuffers() failed in %s", __FUNCTION__);
        }
    }
#ifdef COMPOSITION_BYPASS
    if(ctx->bypassState == BYPASS_OFF_PENDING) {
        //Close channels only after fb content is displayed.
        //We have already reset status before eglSwapBuffers.
        if (!(list->flags & HWC_SKIP_COMPOSITION)) {
            fbDev->waitForBufferPost(fbDev);
        }

        closeBypass(ctx);
        ctx->bypassState = BYPASS_OFF;
    }
#endif
#if defined HDMI_DUAL_DISPLAY
    if(ctx->pendingHDMI) {
        handleHDMIStateChange(dev);
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
        dev->mOverlayLibObject = new overlay::Overlay();
#ifdef COMPOSITION_BYPASS
        for(int i = 0; i < MAX_BYPASS_LAYERS; i++) {
            dev->mOvUI[i] = new overlay::OverlayUI();
            dev->previousBypassHandle[i] = NULL;
        }
        unsetBypassBufferLockState(dev);
        dev->animCount = 0;
        dev->bypassState = BYPASS_OFF;
#endif

#if defined HDMI_DUAL_DISPLAY
        dev->mHDMIEnabled = false;
        dev->pendingHDMI = false;
#endif
        dev->previousOverlayHandle = NULL;
        dev->hwcOverlayStatus = HWC_OVERLAY_CLOSED;
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
