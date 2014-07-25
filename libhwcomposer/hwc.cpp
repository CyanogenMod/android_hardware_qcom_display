/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
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
#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)
#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <EGL/egl.h>
#include <utils/Trace.h>
#include <sys/ioctl.h>
#include <overlay.h>
#include <overlayRotator.h>
#include <mdp_version.h>
#include "hwc_utils.h"
#include "hwc_fbupdate.h"
#include "hwc_mdpcomp.h"
#include "hwc_dump_layers.h"
#include "external.h"
#include "hwc_copybit.h"
#include "profiler.h"

using namespace qhwc;
using namespace overlay;

#define VSYNC_DEBUG 0
#define POWER_MODE_DEBUG 1

#define NON_PRO_8960_SOC_ID 87

static int hwc_device_open(const struct hw_module_t* module,
                           const char* name,
                           struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

static void reset_panel(struct hwc_composer_device_1* dev);

hwc_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        version_major: 2,
        version_minor: 0,
        id: HWC_HARDWARE_MODULE_ID,
        name: "Qualcomm Hardware Composer Module",
        author: "CodeAurora Forum",
        methods: &hwc_module_methods,
        dso: 0,
        reserved: {0},
    }
};

/* In case of non-hybrid WFD session, we are fooling SF by piggybacking on
 * HDMI display ID for virtual. This helper is needed to differentiate their
 * paths in HAL.
 * TODO: Not needed once we have WFD client working on top of Google API's */

static int getDpyforExternalDisplay(hwc_context_t *ctx, int dpy) {
    if(dpy == HWC_DISPLAY_EXTERNAL && ctx->mVirtualonExtActive)
        return HWC_DISPLAY_VIRTUAL;
    return dpy;
}

/*
 * Save callback functions registered to HWC
 */
static void hwc_registerProcs(struct hwc_composer_device_1* dev,
                              hwc_procs_t const* procs)
{
    ALOGI("%s", __FUNCTION__);
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
        ALOGE("%s: Invalid context", __FUNCTION__);
        return;
    }
    ctx->proc = procs;

    // Now that we have the functions needed, kick off
    // the uevent & vsync threads
    init_uevent_thread(ctx);
    init_vsync_thread(ctx);
}

//Helper
static void reset(hwc_context_t *ctx, int numDisplays,
                  hwc_display_contents_1_t** displays) {
    ctx->isPaddingRound = false;
    memset(ctx->listStats, 0, sizeof(ctx->listStats));
    for(int i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        hwc_display_contents_1_t *list = displays[i];
        // XXX:SurfaceFlinger no longer guarantees that this
        // value is reset on every prepare. However, for the layer
        // cache we need to reset it.
        // We can probably rethink that later on
        if (LIKELY(list && list->numHwLayers > 0)) {
            for(uint32_t j = 0; j < list->numHwLayers; j++) {
                if(list->hwLayers[j].compositionType != HWC_FRAMEBUFFER_TARGET)
                    list->hwLayers[j].compositionType = HWC_FRAMEBUFFER;
            }

            if((ctx->mPrevHwLayerCount[i] == 1) and (list->numHwLayers > 1)) {
                /* If the previous cycle for dpy 'i' has 0 AppLayers and the
                 * current cycle has atleast 1 AppLayer, padding round needs
                 * to be invoked on current cycle to free up the resources.
                 */
                ctx->isPaddingRound = true;
            }
            ctx->mPrevHwLayerCount[i] = list->numHwLayers;
        } else {
            ctx->mPrevHwLayerCount[i] = 0;
        }

        if(ctx->mFBUpdate[i])
            ctx->mFBUpdate[i]->reset();
        if(ctx->mCopyBit[i])
            ctx->mCopyBit[i]->reset();
        if(ctx->mLayerRotMap[i])
            ctx->mLayerRotMap[i]->reset();
    }

}

//clear prev layer prop flags and realloc for current frame
static void reset_layer_prop(hwc_context_t* ctx, int dpy, int numAppLayers) {
    if(ctx->layerProp[dpy]) {
       delete[] ctx->layerProp[dpy];
       ctx->layerProp[dpy] = NULL;
    }
    ctx->layerProp[dpy] = new LayerProp[numAppLayers];
}


static int hwc_prepare_primary(hwc_composer_device_1 *dev,
        hwc_display_contents_1_t *list) {
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    const int dpy = HWC_DISPLAY_PRIMARY;
    int ret = -1;
    if(UNLIKELY(!ctx->mBasePipeSetup))
        setupBasePipe(ctx);
    if (LIKELY(list && list->numHwLayers > 1) &&
            ctx->dpyAttr[dpy].isActive) {
        reset_layer_prop(ctx, dpy, list->numHwLayers - 1);
        setListStats(ctx, list, dpy);
        if((ret = ctx->mMDPComp[dpy]->prepare(ctx, list)) < 0) {
            const int fbZ = 0;
            ctx->mFBUpdate[dpy]->prepare(ctx, list, fbZ);
        }
#ifdef USE_COPYBIT_COMPOSITION_FALLBACK
        // Use Copybit, when Full/Partial MDP comp fails
        // (only for 8960 which has  dedicated 2D core)
        if((ret < 1) && (ctx->mSocId == NON_PRO_8960_SOC_ID) && ctx->mCopyBit[dpy])
            ctx->mCopyBit[dpy]->prepare(ctx, list, dpy);
#endif
    }
    return 0;
}

static int hwc_prepare_external(hwc_composer_device_1 *dev,
        hwc_display_contents_1_t *list) {
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    const int dpy = HWC_DISPLAY_EXTERNAL;
    int ret = -1;

    if (LIKELY(list && list->numHwLayers > 1) &&
            ctx->dpyAttr[dpy].isActive &&
            ctx->dpyAttr[dpy].connected) {
        reset_layer_prop(ctx, dpy, list->numHwLayers - 1);
        if(!ctx->dpyAttr[dpy].isPause) {
           ctx->dpyAttr[dpy].isConfiguring = false;
           setListStats(ctx, list, dpy);
           if((ret = ctx->mMDPComp[dpy]->prepare(ctx, list)) < 0) {
              const int fbZ = 0;
              ctx->mFBUpdate[dpy]->prepare(ctx, list, fbZ);
           }
#ifdef USE_COPYBIT_COMPOSITION_FALLBACK
           // Use Copybit, when Full/Partial MDP comp fails
           // (only for 8960 which has  dedicated 2D core)
           if((ret < 1) && (ctx->mSocId == NON_PRO_8960_SOC_ID) && ctx->mCopyBit[dpy] &&
                 !ctx->listStats[dpy].isDisplayAnimating)
                ctx->mCopyBit[dpy]->prepare(ctx, list, dpy);
#endif
        } else {
            /* External Display is in Pause state.
             * Mark all application layers as OVERLAY so that
             * GPU will not compose.
             */
            for(size_t i = 0 ;i < (size_t)(list->numHwLayers - 1); i++) {
                hwc_layer_1_t *layer = &list->hwLayers[i];
                layer->compositionType = HWC_OVERLAY;
            }
        }
    }
    return 0;
}

static int hwc_prepare_virtual(hwc_composer_device_1 *dev,
        hwc_display_contents_1_t *list) {

    hwc_context_t* ctx = (hwc_context_t*)(dev);
    const int dpy = HWC_DISPLAY_VIRTUAL;

    if (LIKELY(list && list->numHwLayers > 1) &&
            ctx->dpyAttr[dpy].isActive &&
            ctx->dpyAttr[dpy].connected) {
        reset_layer_prop(ctx, dpy, list->numHwLayers - 1);
        if(!ctx->dpyAttr[dpy].isPause) {
            ctx->dpyAttr[dpy].isConfiguring = false;
            setListStats(ctx, list, dpy);
            if(ctx->mMDPComp[dpy]->prepare(ctx, list) < 0) {
                const int fbZ = 0;
                ctx->mFBUpdate[dpy]->prepare(ctx, list, fbZ);
            }
        } else {
            /* Virtual Display is in Pause state.
             * Mark all application layers as OVERLAY so that
             * GPU will not compose.
             */
            for(size_t i = 0 ;i < (size_t)(list->numHwLayers - 1); i++) {
                hwc_layer_1_t *layer = &list->hwLayers[i];
                layer->compositionType = HWC_OVERLAY;
            }
        }
    }
    return 0;
}


static int hwc_prepare(hwc_composer_device_1 *dev, size_t numDisplays,
                       hwc_display_contents_1_t** displays)
{
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);

    if (ctx->mPanelResetStatus) {
        ALOGW("%s: panel is in bad state. reset the panel", __FUNCTION__);
        reset_panel(dev);
    }

    //Will be unlocked at the end of set
    ctx->mDrawLock.lock();
    reset(ctx, numDisplays, displays);

    ctx->mOverlay->configBegin();
    ctx->mRotMgr->configBegin();
    ctx->mNeedsRotator = false;

    for (int32_t i = numDisplays; i >= 0; i--) {
        hwc_display_contents_1_t *list = displays[i];
        int dpy = getDpyforExternalDisplay(ctx, i);
        switch(dpy) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_prepare_primary(dev, list);
                break;
            case HWC_DISPLAY_EXTERNAL:
                ret = hwc_prepare_external(dev, list);
                break;
            case HWC_DISPLAY_VIRTUAL:
                ret = hwc_prepare_virtual(dev, list);
                break;
            default:
                ret = -EINVAL;
        }
    }

    ctx->mOverlay->configDone();
    ctx->mRotMgr->configDone();

    return ret;
}

static int hwc_eventControl(struct hwc_composer_device_1* dev, int dpy,
                             int event, int enable)
{
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);

    if(!ctx->dpyAttr[dpy].isActive) {
        ALOGE("Display is blanked - Cannot %s vsync",
              enable ? "enable" : "disable");
        return -EINVAL;
    }

    switch(event) {
        case HWC_EVENT_VSYNC:
            if (ctx->vstate.enable == enable)
                break;
            ret = hwc_vsync_control(ctx, dpy, enable);
            if(ret == 0)
                ctx->vstate.enable = !!enable;
            ALOGD_IF (VSYNC_DEBUG, "VSYNC state changed to %s",
                      (enable)?"ENABLED":"DISABLED");
            break;
#ifdef QCOM_BSP
        case  HWC_EVENT_ORIENTATION:
            if(dpy == HWC_DISPLAY_PRIMARY) {
                Locker::Autolock _l(ctx->mDrawLock);
                // store the primary display orientation
                ctx->deviceOrientation = enable;
            }
            break;
#endif
        default:
            ret = -EINVAL;
    }
    return ret;
}

static int hwc_setPowerMode(struct hwc_composer_device_1* dev, int dpy,
        int mode)
{
    ATRACE_CALL();
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    int ret = 0, value = 0;

    Locker::Autolock _l(ctx->mDrawLock);
    /* In case of non-hybrid WFD session, we are fooling SF by
     * piggybacking on HDMI display ID for virtual.
     * TODO: Not needed once we have WFD client working on top
     * of Google API's.
     */
    dpy = getDpyforExternalDisplay(ctx,dpy);

    ALOGD_IF(POWER_MODE_DEBUG, "%s: Setting mode %d on display: %d",
            __FUNCTION__, mode, dpy);

    switch(mode) {
        case HWC_POWER_MODE_OFF:
            // free up all the overlay pipes in use
            // when we get a blank for either display
            // makes sure that all pipes are freed
            ctx->mOverlay->configBegin();
            ctx->mOverlay->configDone();
            ctx->mRotMgr->clear();
            // If VDS is connected, do not clear WB object as it
            // will end up detaching IOMMU. This is required
            // to send black frame to WFD sink on power suspend.
            // Note: With this change, we keep the WriteBack object
            // alive on power suspend for AD use case.
            value = FB_BLANK_POWERDOWN;
            break;
        case HWC_POWER_MODE_DOZE:
        case HWC_POWER_MODE_DOZE_SUSPEND:
            value = FB_BLANK_VSYNC_SUSPEND;
            break;
        case HWC_POWER_MODE_NORMAL:
            value = FB_BLANK_UNBLANK;
            break;
    }

    switch(dpy) {
    case HWC_DISPLAY_PRIMARY:
        if(mode == HWC_POWER_MODE_OFF) {
            if(!Overlay::displayCommit(ctx->dpyAttr[dpy].fd)){
                ALOGE("%s: display commit fail for primary!", __FUNCTION__);
                ret = -1;
            }
        }

        if(ioctl(ctx->dpyAttr[dpy].fd, FBIOBLANK, value) < 0 ) {
            ALOGE("%s: ioctl FBIOBLANK failed for Primary with error %s"
                    " value %d", __FUNCTION__, strerror(errno), value);
            return -errno;
        }

        if(mode == HWC_POWER_MODE_NORMAL) {
            // Enable HPD here, as during bootup POWER_MODE_NORMAL is set
            // when SF is completely initialized
            ctx->mExtDisplay->setHPD(1);
        }

        ctx->dpyAttr[dpy].isActive =  not(mode == HWC_POWER_MODE_OFF);

        if(ctx->mVirtualonExtActive) {
            /* if mVirtualonExtActive is true, display hal will
             * receive unblank calls for non-hybrid WFD solution
             * since we piggyback on HDMI.
             * TODO: Not needed once we have WFD client working on top
             of Google API's */
            break;
        }
        //Deliberate fall through since there is no explicit power mode for
        //virtual displays.
    case HWC_DISPLAY_VIRTUAL:
        /* There are two ways to reach this block of code.

         * Display hal has received unblank call on HWC_DISPLAY_EXTERNAL
         and ctx->mVirtualonExtActive is true. In this case, non-hybrid
         WFD is active. If so, getDpyforExternalDisplay will return dpy
         as HWC_DISPLAY_VIRTUAL.

         * Display hal has received unblank call on HWC_DISPLAY_PRIMARY
         and since SF is not aware of VIRTUAL DISPLAY being handle by HWC,
         it wont send blank / unblank events for it. We piggyback on
         PRIMARY DISPLAY events to release mdp pipes and
         activate/deactivate VIRTUAL DISPLAY.

         * TODO: This separate case statement is not needed once we have
         WFD client working on top of Google API's.

         */

        if(ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].connected) {
            const int dpy = HWC_DISPLAY_VIRTUAL;
            if(mode == HWC_POWER_MODE_OFF and
                    (not ctx->dpyAttr[dpy].isPause)) {
                if(!Overlay::displayCommit(ctx->dpyAttr[dpy].fd,1)) {
                    ALOGE("%s: displayCommit failed for virtual", __FUNCTION__);
                    ret = -1;
                }
            }
            ctx->dpyAttr[dpy].isActive = not(mode == HWC_POWER_MODE_OFF);
        }
        break;
    case HWC_DISPLAY_EXTERNAL:
        if(mode == HWC_POWER_MODE_OFF) {
            if(!Overlay::displayCommit(ctx->dpyAttr[dpy].fd,1)) {
                ALOGE("%s: displayCommit failed for external", __FUNCTION__);
                ret = -1;
            }
        }
        ctx->dpyAttr[dpy].isActive = not(mode == HWC_POWER_MODE_OFF);
        break;
    default:
        return -EINVAL;
    }

    ALOGD_IF(POWER_MODE_DEBUG, "%s: Done setting mode %d on display %d",
            __FUNCTION__, mode, dpy);
    return ret;
}

static void reset_panel(struct hwc_composer_device_1* dev)
{
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);

    if (!ctx->mPanelResetStatus)
        return;

    ALOGD("%s: setting power mode off", __FUNCTION__);
    ret = hwc_setPowerMode(dev, HWC_DISPLAY_PRIMARY, HWC_POWER_MODE_OFF);
    if (ret < 0) {
        ALOGE("%s: FBIOBLANK failed to BLANK:  %s", __FUNCTION__,
                strerror(errno));
    }

    ALOGD("%s: setting power mode normal and enabling vsync", __FUNCTION__);
    ret = hwc_setPowerMode(dev, HWC_DISPLAY_PRIMARY, HWC_POWER_MODE_NORMAL);
    if (ret < 0) {
        ALOGE("%s: FBIOBLANK failed to UNBLANK : %s", __FUNCTION__,
                strerror(errno));
    }
    hwc_vsync_control(ctx, HWC_DISPLAY_PRIMARY, 1);

    ctx->mPanelResetStatus = false;
}


static int hwc_query(struct hwc_composer_device_1* dev,
                     int param, int* value)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    int supported = HWC_DISPLAY_PRIMARY_BIT;

    switch (param) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
        // Not supported for now
        value[0] = 0;
        break;
    case HWC_DISPLAY_TYPES_SUPPORTED:
        if(ctx->mMDP.hasOverlay)
            supported |= HWC_DISPLAY_EXTERNAL_BIT;
        value[0] = supported;
        break;
    default:
        return -EINVAL;
    }
    return 0;

}


static int hwc_set_primary(hwc_context_t *ctx, hwc_display_contents_1_t* list) {
    ATRACE_CALL();
    int ret = 0;
    const int dpy = HWC_DISPLAY_PRIMARY;
    if (LIKELY(list) && ctx->dpyAttr[dpy].isActive) {
        uint32_t last = list->numHwLayers - 1;
        hwc_layer_1_t *fbLayer = &list->hwLayers[last];
        int fd = -1; //FenceFD from the Copybit(valid in async mode)
        bool copybitDone = false;
        if(ctx->mCopyBit[dpy])
            copybitDone = ctx->mCopyBit[dpy]->draw(ctx, list, dpy, &fd);
        if(list->numHwLayers > 1)
            hwc_sync(ctx, list, dpy, fd);

        // Dump the layers for primary
        if(ctx->mHwcDebug[dpy])
            ctx->mHwcDebug[dpy]->dumpLayers(list);

        if (!ctx->mMDPComp[dpy]->draw(ctx, list)) {
            ALOGE("%s: MDPComp draw failed", __FUNCTION__);
            ret = -1;
        }

        //TODO We dont check for SKIP flag on this layer because we need PAN
        //always. Last layer is always FB
        private_handle_t *hnd = (private_handle_t *)fbLayer->handle;
        if(copybitDone && ctx->mMDP.version > qdutils::MDP_V4_3) {
            hnd = ctx->mCopyBit[dpy]->getCurrentRenderBuffer();
        }

        if(hnd) {
            if (!ctx->mFBUpdate[dpy]->draw(ctx, hnd)) {
                ALOGE("%s: FBUpdate draw failed", __FUNCTION__);
                ret = -1;
            }
        }

        if(!Overlay::displayCommit(ctx->dpyAttr[dpy].fd)) {
            ALOGE("%s: display commit fail for %d dpy!", __FUNCTION__, dpy);
            ret = -1;
        }
    }

    closeAcquireFds(list);
    return ret;
}

static int hwc_set_external(hwc_context_t *ctx,
                            hwc_display_contents_1_t* list)
{
    ATRACE_CALL();
    int ret = 0;

    const int dpy = HWC_DISPLAY_EXTERNAL;


    if (LIKELY(list) && ctx->dpyAttr[dpy].isActive &&
        ctx->dpyAttr[dpy].connected &&
        !ctx->dpyAttr[dpy].isPause) {
        uint32_t last = list->numHwLayers - 1;
        hwc_layer_1_t *fbLayer = &list->hwLayers[last];
        int fd = -1; //FenceFD from the Copybit(valid in async mode)
        bool copybitDone = false;
        if(ctx->mCopyBit[dpy])
            copybitDone = ctx->mCopyBit[dpy]->draw(ctx, list, dpy, &fd);

        if(list->numHwLayers > 1)
            hwc_sync(ctx, list, dpy, fd);

        // Dump the layers for external
        if(ctx->mHwcDebug[dpy])
            ctx->mHwcDebug[dpy]->dumpLayers(list);

        if (!ctx->mMDPComp[dpy]->draw(ctx, list)) {
            ALOGE("%s: MDPComp draw failed", __FUNCTION__);
            ret = -1;
        }

        int extOnlyLayerIndex =
                ctx->listStats[dpy].extOnlyLayerIndex;

        private_handle_t *hnd = (private_handle_t *)fbLayer->handle;
        if(extOnlyLayerIndex!= -1) {
            hwc_layer_1_t *extLayer = &list->hwLayers[extOnlyLayerIndex];
            hnd = (private_handle_t *)extLayer->handle;
        } else if(copybitDone && ctx->mMDP.version > qdutils::MDP_V4_3) {
            hnd = ctx->mCopyBit[dpy]->getCurrentRenderBuffer();
        }

        if(hnd && !isYuvBuffer(hnd)) {
            if (!ctx->mFBUpdate[dpy]->draw(ctx, hnd)) {
                ALOGE("%s: FBUpdate::draw fail!", __FUNCTION__);
                ret = -1;
            }
        }

        if(!Overlay::displayCommit(ctx->dpyAttr[dpy].fd)) {
            ALOGE("%s: display commit fail for %d dpy!", __FUNCTION__, dpy);
            ret = -1;
        }
    }

    closeAcquireFds(list);
    return ret;
}

static int hwc_set_virtual(hwc_context_t *ctx,
                            hwc_display_contents_1_t* list)
{
    ATRACE_CALL();
    int ret = 0;

    const int dpy = HWC_DISPLAY_VIRTUAL;


    if (LIKELY(list) && ctx->dpyAttr[dpy].isActive &&
            ctx->dpyAttr[dpy].connected &&
            !ctx->dpyAttr[dpy].isPause) {
        uint32_t last = list->numHwLayers - 1;
        hwc_layer_1_t *fbLayer = &list->hwLayers[last];
        int fd = -1; //FenceFD from the Copybit(valid in async mode)
        bool copybitDone = false;
        if(ctx->mCopyBit[dpy])
            copybitDone = ctx->mCopyBit[dpy]->draw(ctx, list, dpy, &fd);

        if(list->numHwLayers > 1)
            hwc_sync(ctx, list, dpy, fd);


        if (!ctx->mMDPComp[dpy]->draw(ctx, list)) {
            ALOGE("%s: MDPComp draw failed", __FUNCTION__);
            ret = -1;
        }

        int extOnlyLayerIndex =
            ctx->listStats[dpy].extOnlyLayerIndex;

        private_handle_t *hnd = (private_handle_t *)fbLayer->handle;
        if(extOnlyLayerIndex!= -1) {
            hwc_layer_1_t *extLayer = &list->hwLayers[extOnlyLayerIndex];
            hnd = (private_handle_t *)extLayer->handle;
        } else if(copybitDone) {
            hnd = ctx->mCopyBit[dpy]->getCurrentRenderBuffer();
        }

        if(hnd && !isYuvBuffer(hnd)) {
            if (!ctx->mFBUpdate[dpy]->draw(ctx, hnd)) {
                ALOGE("%s: FBUpdate::draw fail!", __FUNCTION__);
                ret = -1;
            }
        }

        if(!Overlay::displayCommit(ctx->dpyAttr[dpy].fd)) {
            ALOGE("%s: display commit fail for %d dpy!", __FUNCTION__, dpy);
            ret = -1;
        }
    }

    closeAcquireFds(list);

    if (list && !ctx->mVirtualonExtActive && (list->retireFenceFd < 0) ) {
        // SF assumes HWC waits for the acquire fence and returns a new fence
        // that signals when we're done. Since we don't wait, and also don't
        // touch the buffer, we can just handle the acquire fence back to SF
        // as the retire fence.
        list->retireFenceFd = list->outbufAcquireFenceFd;
    }

    return ret;
}


static int hwc_set(hwc_composer_device_1 *dev,
                   size_t numDisplays,
                   hwc_display_contents_1_t** displays)
{
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    for (uint32_t i = 0; i <= numDisplays; i++) {
        hwc_display_contents_1_t* list = displays[i];
        int dpy = getDpyforExternalDisplay(ctx, i);
        switch(dpy) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_set_primary(ctx, list);
                break;
            case HWC_DISPLAY_EXTERNAL:
                ret = hwc_set_external(ctx, list);
                break;
            case HWC_DISPLAY_VIRTUAL:
                ret = hwc_set_virtual(ctx, list);
                break;
            default:
                ret = -EINVAL;
        }
    }
    // This is only indicative of how many times SurfaceFlinger posts
    // frames to the display.
    CALC_FPS();
    MDPComp::resetIdleFallBack();
    //Was locked at the beginning of prepare
    //Composition cycle is complete signal all waiting threads
    ctx->mDrawLock.signal();
    ctx->mDrawLock.unlock();
    return ret;
}

int hwc_getDisplayConfigs(struct hwc_composer_device_1* dev, int disp,
        uint32_t* configs, size_t* numConfigs) {
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    disp = getDpyforExternalDisplay(ctx, disp);

    //Currently we allow only 1 config, reported as config id # 0
    //This config is passed in to getDisplayAttributes. Ignored for now.
    switch(disp) {
        case HWC_DISPLAY_PRIMARY:
            if(*numConfigs > 0) {
                configs[0] = 0;
                *numConfigs = 1;
            }
            ret = 0; //NO_ERROR
            break;
        case HWC_DISPLAY_EXTERNAL:
        case HWC_DISPLAY_VIRTUAL:
            ret = -1; //Not connected
            if(ctx->dpyAttr[disp].connected) {
                ret = 0; //NO_ERROR
                if(*numConfigs > 0) {
                    configs[0] = 0;
                    *numConfigs = 1;
                }
            }
            break;
    }
    return ret;
}

int hwc_getDisplayAttributes(struct hwc_composer_device_1* dev, int disp,
        uint32_t config, const uint32_t* attributes, int32_t* values) {

    hwc_context_t* ctx = (hwc_context_t*)(dev);
    disp = getDpyforExternalDisplay(ctx, disp);
    //If hotpluggable displays(i.e, HDMI, WFD) are inactive return error
    if( (disp != HWC_DISPLAY_PRIMARY) && !ctx->dpyAttr[disp].connected) {
        return -1;
    }

    //From HWComposer
    static const uint32_t DISPLAY_ATTRIBUTES[] = {
        HWC_DISPLAY_VSYNC_PERIOD,
        HWC_DISPLAY_WIDTH,
        HWC_DISPLAY_HEIGHT,
        HWC_DISPLAY_DPI_X,
        HWC_DISPLAY_DPI_Y,
        HWC_DISPLAY_SECURE,
        HWC_DISPLAY_NO_ATTRIBUTE,
    };

    const int NUM_DISPLAY_ATTRIBUTES = (sizeof(DISPLAY_ATTRIBUTES) /
            sizeof(DISPLAY_ATTRIBUTES)[0]);

    for (size_t i = 0; i < NUM_DISPLAY_ATTRIBUTES - 1; i++) {
        switch (attributes[i]) {
        case HWC_DISPLAY_VSYNC_PERIOD:
            values[i] = ctx->dpyAttr[disp].vsync_period;
            break;
        case HWC_DISPLAY_WIDTH:
            values[i] = ctx->dpyAttr[disp].xres;
            ALOGD("%s disp = %d, width = %d",__FUNCTION__, disp,
                    ctx->dpyAttr[disp].xres);
            break;
        case HWC_DISPLAY_HEIGHT:
            values[i] = ctx->dpyAttr[disp].yres;
            ALOGD("%s disp = %d, height = %d",__FUNCTION__, disp,
                    ctx->dpyAttr[disp].yres);
            break;
        case HWC_DISPLAY_DPI_X:
            values[i] = (int32_t) (ctx->dpyAttr[disp].xdpi*1000.0);
            break;
        case HWC_DISPLAY_DPI_Y:
            values[i] = (int32_t) (ctx->dpyAttr[disp].ydpi*1000.0);
            break;
        case HWC_DISPLAY_SECURE:
            values[i] = (int32_t) (ctx->dpyAttr[disp].secure);
            break;
        default:
            ALOGE("Unknown display attribute %d",
                    attributes[i]);
            return -EINVAL;
        }
    }
    return 0;
}

void hwc_dump(struct hwc_composer_device_1* dev, char *buff, int buff_len)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    Locker::Autolock _l(ctx->mDrawLock);
    android::String8 aBuf("");
    dumpsys_log(aBuf, "Qualcomm HWC state:\n");
    dumpsys_log(aBuf, "  MDPVersion=%d\n", ctx->mMDP.version);
    dumpsys_log(aBuf, "  DisplayPanel=%c\n", ctx->mMDP.panel);
    for(int dpy = 0; dpy < HWC_NUM_DISPLAY_TYPES; dpy++) {
        if(ctx->mMDPComp[dpy])
            ctx->mMDPComp[dpy]->dump(aBuf);
    }
    char ovDump[2048] = {'\0'};
    ctx->mOverlay->getDump(ovDump, 2048);
    dumpsys_log(aBuf, ovDump);
    ovDump[0] = '\0';
    ctx->mRotMgr->getDump(ovDump, 2048);
    dumpsys_log(aBuf, ovDump);
    strlcpy(buff, aBuf.string(), buff_len);
}

int hwc_getActiveConfig(struct hwc_composer_device_1* /*dev*/, int /*disp*/) {
    //Supports only the default config (0th index) for now
    return 0;
}

int hwc_setActiveConfig(struct hwc_composer_device_1* /*dev*/, int /*disp*/,
        int index) {
    //Supports only the default config (0th index) for now
    return (index == 0) ? index : -EINVAL;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    if(!dev) {
        ALOGE("%s: NULL device pointer", __FUNCTION__);
        return -1;
    }
    closeContext((hwc_context_t*)dev);
    free(dev);

    return 0;
}

static int hwc_device_open(const struct hw_module_t* module, const char* name,
                           struct hw_device_t** device)
{
    int status = -EINVAL;

    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
        struct hwc_context_t *dev;
        dev = (hwc_context_t*)malloc(sizeof(*dev));
        memset(dev, 0, sizeof(*dev));

        //Initialize hwc context
        initContext(dev);

        //Setup HWC methods
        dev->device.common.tag          = HARDWARE_DEVICE_TAG;
        dev->device.common.version      = HWC_DEVICE_API_VERSION_1_4;
        dev->device.common.module       = const_cast<hw_module_t*>(module);
        dev->device.common.close        = hwc_device_close;
        dev->device.prepare             = hwc_prepare;
        dev->device.set                 = hwc_set;
        dev->device.eventControl        = hwc_eventControl;
        dev->device.setPowerMode        = hwc_setPowerMode;
        dev->device.query               = hwc_query;
        dev->device.registerProcs       = hwc_registerProcs;
        dev->device.dump                = hwc_dump;
        dev->device.getDisplayConfigs   = hwc_getDisplayConfigs;
        dev->device.getDisplayAttributes = hwc_getDisplayAttributes;
        dev->device.getActiveConfig     = hwc_getActiveConfig;
        dev->device.setActiveConfig     = hwc_setActiveConfig;
        *device = &dev->device.common;
        status = 0;
    }
    return status;
}
