/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2015, The Linux Foundation. All rights reserved.
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
#include <overlayWriteback.h>
#include <overlayCursor.h>
#include <mdp_version.h>
#include "hwc_utils.h"
#include "hwc_fbupdate.h"
#include "hwc_mdpcomp.h"
#include "hwc_dump_layers.h"
#include "hdmi.h"
#include "hwc_copybit.h"
#include "hwc_ad.h"
#include "profiler.h"
#include "hwc_virtual.h"

using namespace qhwc;
using namespace overlay;

#define VSYNC_DEBUG 0
#define POWER_MODE_DEBUG 1

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

static void setPaddingRound(hwc_context_t *ctx, int numDisplays,
                            hwc_display_contents_1_t** displays) {
    ctx->isPaddingRound = false;
    for(int i = 0; i < numDisplays; i++) {
        hwc_display_contents_1_t *list = displays[i];
        if (LIKELY(list && list->numHwLayers > 0)) {
            if((ctx->mPrevHwLayerCount[i] == 1 or
                ctx->mPrevHwLayerCount[i] == 0) and
               (list->numHwLayers > 1)) {
                /* If the previous cycle for dpy 'i' has 0 AppLayers and the
                 * current cycle has atleast 1 AppLayer, padding round needs
                 * to be invoked in current cycle on all the active displays
                 * to free up the resources.
                 */
                ctx->isPaddingRound = true;
            }
            ctx->mPrevHwLayerCount[i] = (int)list->numHwLayers;
        } else {
            ctx->mPrevHwLayerCount[i] = 0;
        }
    }
}

/* Based on certain conditions, isDMAStateChanging will be set
 * to make this function self-contained */
static void setDMAState(hwc_context_t *ctx, int numDisplays,
                        hwc_display_contents_1_t** displays) {

    ctx->isDMAStateChanging = false;
    if(ctx->mRotMgr->getNumActiveSessions() == 0)
        Overlay::setDMAMode(Overlay::DMA_LINE_MODE);

    for(int dpy = 0; dpy < numDisplays; dpy++) {
        hwc_display_contents_1_t *list = displays[dpy];
        if (LIKELY(list && list->numHwLayers > 0)) {
            for(size_t layerIndex = 0; layerIndex < list->numHwLayers;
                                                  layerIndex++) {
                if(list->hwLayers[layerIndex].compositionType !=
                                            HWC_FRAMEBUFFER_TARGET)
                {
                    hwc_layer_1_t const* layer = &list->hwLayers[layerIndex];
                    private_handle_t *hnd = (private_handle_t *)layer->handle;

                    /* If a layer requires rotation, set the DMA state
                     * to BLOCK_MODE */

                    if (canUseRotator(ctx, dpy) &&
                        (has90Transform(layer) || getRotDownscale(ctx, layer))
                        && isRotationDoable(ctx, hnd)) {
                        if(not (ctx->mOverlay->isDMAMultiplexingSupported() &&
                                          dpy)) {
                            if(ctx->mOverlay->isPipeTypeAttached(
                                             overlay::utils::OV_MDP_PIPE_DMA))
                                ctx->isDMAStateChanging = true;
                        }
                        Overlay::setDMAMode(Overlay::DMA_BLOCK_MODE);
                    }
                }
            }
            if(dpy) {
                /* Uncomment the below code for testing purpose.
                   Assuming the orientation value is in terms of HAL_TRANSFORM,
                   this needs mapping to HAL, if its in different convention */

                /* char value[PROPERTY_VALUE_MAX];
                   property_get("sys.ext_orientation", value, "0");
                   ctx->mExtOrientation = atoi(value);*/

                if(ctx->mExtOrientation || ctx->mBufferMirrorMode) {
                    if(ctx->mOverlay->isPipeTypeAttached(
                                         overlay::utils::OV_MDP_PIPE_DMA)) {
                        ctx->isDMAStateChanging = true;
                    }
                    Overlay::setDMAMode(Overlay::DMA_BLOCK_MODE);
                }
            }
        }
    }
}

static void setNumActiveDisplays(hwc_context_t *ctx, int numDisplays,
                            hwc_display_contents_1_t** displays) {

    ctx->numActiveDisplays = 0;
    for(int i = 0; i < numDisplays; i++) {
        hwc_display_contents_1_t *list = displays[i];
        if (LIKELY(list && list->numHwLayers > 0)) {
            /* For display devices like SSD and screenrecord, we cannot
             * rely on isActive and connected attributes of dpyAttr to
             * determine if the displaydevice is active. Hence in case if
             * the layer-list is non-null and numHwLayers > 0, we assume
             * the display device to be active.
             */
            ctx->numActiveDisplays += 1;
        }
    }
}

static bool validDisplay(int disp) {
    switch(disp) {
        case HWC_DISPLAY_PRIMARY:
        case HWC_DISPLAY_EXTERNAL:
        case HWC_DISPLAY_VIRTUAL:
            return true;
            break;
        default:
            return false;
    }
}

static bool isHotPluggable(hwc_context_t *ctx, int dpy) {
    return ((dpy == HWC_DISPLAY_EXTERNAL) ||
            ((dpy == HWC_DISPLAY_PRIMARY) &&
             ctx->mHDMIDisplay->isHDMIPrimaryDisplay()));
}

static void reset(hwc_context_t *ctx, int numDisplays,
                  hwc_display_contents_1_t** displays) {


    for(int i = 0; i < numDisplays; i++) {
        hwc_display_contents_1_t *list = displays[i];
        // XXX:SurfaceFlinger no longer guarantees that this
        // value is reset on every prepare. However, for the layer
        // cache we need to reset it.
        // We can probably rethink that later on
        if (LIKELY(list && list->numHwLayers > 0)) {
            for(size_t j = 0; j < list->numHwLayers; j++) {
                if(list->hwLayers[j].compositionType != HWC_FRAMEBUFFER_TARGET)
                    list->hwLayers[j].compositionType = HWC_FRAMEBUFFER;
            }

        }

        if(ctx->mMDPComp[i])
            ctx->mMDPComp[i]->reset();
        if(ctx->mFBUpdate[i])
            ctx->mFBUpdate[i]->reset();
        if(ctx->mCopyBit[i])
            ctx->mCopyBit[i]->reset();
        if(ctx->mLayerRotMap[i])
            ctx->mLayerRotMap[i]->reset();
    }

    memset(&(ctx->mPtorInfo), 0, sizeof(ctx->mPtorInfo));
    ctx->mAD->reset();

}

static void scaleDisplayFrame(hwc_display_contents_1_t *list, float xresRatio,
        float yresRatio) {
    for (size_t i = 0; i < list->numHwLayers; i++) {
        hwc_layer_1_t *layer = &list->hwLayers[i];
        hwc_rect_t& displayFrame = layer->displayFrame;
        uint32_t layerWidth = displayFrame.right - displayFrame.left;
        uint32_t layerHeight = displayFrame.bottom - displayFrame.top;
        displayFrame.left = (int)(xresRatio * (float)displayFrame.left);
        displayFrame.top = (int)(yresRatio * (float)displayFrame.top);
        displayFrame.right = (int)((float)displayFrame.left +
                                   (float)layerWidth * xresRatio);
        displayFrame.bottom = (int)((float)displayFrame.top +
                                    (float)layerHeight * yresRatio);
    }
}

static void handleFbScaling(hwc_context_t *ctx, int dpy,
        hwc_display_contents_1_t *list) {
    //We could switch to a config that does not lead to fb scaling, but
    //we need to update older display frames and ratios.
    if (ctx->dpyAttr[dpy].fbScaling or ctx->dpyAttr[dpy].configSwitched) {
        uint32_t xresPanel = ctx->dpyAttr[dpy].xres;
        uint32_t yresPanel = ctx->dpyAttr[dpy].yres;
        uint32_t xresFB = ctx->dpyAttr[dpy].xresFB;
        uint32_t yresFB = ctx->dpyAttr[dpy].yresFB;
        float xresRatio = (float)xresPanel / (float)xresFB;
        float yresRatio = (float)yresPanel / (float)yresFB;
        if(list->flags & HWC_GEOMETRY_CHANGED) {
            //In case of geometry changes f/w resets displays frames w.r.t to
            //FB's dimensions. So any config switch is automatically absorbed.
            scaleDisplayFrame(list, xresRatio, yresRatio);
        } else if (ctx->dpyAttr[dpy].configSwitched) {
            //If there is a primary panel resolution switch without a geometry
            //change we need to scale-back the previous ratio used and then use
            //the current ratio. i.e use current ratio / prev ratio
            scaleDisplayFrame(list,
                    xresRatio / ctx->dpyAttr[dpy].fbWidthScaleRatio,
                    yresRatio / ctx->dpyAttr[dpy].fbHeightScaleRatio);
        }
        ctx->dpyAttr[dpy].configSwitched = false;
        ctx->dpyAttr[dpy].fbWidthScaleRatio = xresRatio;
        ctx->dpyAttr[dpy].fbHeightScaleRatio = yresRatio;
    }
}

static int hwc_prepare_primary(hwc_composer_device_1 *dev,
        hwc_display_contents_1_t *list) {
    ATRACE_CALL();
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    const int dpy = HWC_DISPLAY_PRIMARY;
    bool fbComp = false;
    if (!ctx->mBootAnimCompleted)
        processBootAnimCompleted(ctx);
    if (LIKELY(list && list->numHwLayers > 1) && ctx->dpyAttr[dpy].connected &&
            (ctx->dpyAttr[dpy].isActive ||
             ctx->mHDMIDisplay->isHDMIPrimaryDisplay())
            && !ctx->dpyAttr[dpy].isPause) {

        // When HDMI is primary we should rely on the first valid
        // draw call in order to activate the display
        if (!ctx->dpyAttr[dpy].isActive) {
            // If the cable is connected after HWC initialization and before
            // the UEvent thread is initialized then we will miss the ONLINE
            // event. We need to update the display appropriately when we get
            // the first valid frame.
            int cableConnected = ctx->mHDMIDisplay->getConnectedState();
            if ((cableConnected == 1) && !ctx->dpyAttr[dpy].connected) {
                qhwc::handle_online(ctx, dpy);
            }
            ctx->mHDMIDisplay->activateDisplay();
            ctx->dpyAttr[dpy].isActive = true;
        }

        handleFbScaling(ctx, dpy, list);

        reset_layer_prop(ctx, dpy, (int)list->numHwLayers - 1);
        setListStats(ctx, list, dpy);

        fbComp = (ctx->mMDPComp[dpy]->prepare(ctx, list) < 0);

        if (fbComp) {
            const int fbZ = 0;
            if(not ctx->mFBUpdate[dpy]->prepareAndValidate(ctx, list, fbZ)) {
                ctx->mOverlay->clear(dpy);
                ctx->mLayerRotMap[dpy]->clear();
            }
        }

        if (ctx->mMDP.version < qdutils::MDP_V4_0) {
            if(ctx->mCopyBit[dpy])
                ctx->mCopyBit[dpy]->prepare(ctx, list, dpy);
        }
    }
    return 0;
}

static int hwc_prepare_external(hwc_composer_device_1 *dev,
        hwc_display_contents_1_t *list) {
    ATRACE_CALL();
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    const int dpy = HWC_DISPLAY_EXTERNAL;

    if (LIKELY(list && list->numHwLayers > 1) &&
            ctx->dpyAttr[dpy].isActive &&
            ctx->dpyAttr[dpy].connected) {
        reset_layer_prop(ctx, dpy, (int)list->numHwLayers - 1);
        if(!ctx->dpyAttr[dpy].isPause) {
            ctx->dpyAttr[dpy].isConfiguring = false;
            setListStats(ctx, list, dpy);
            if(ctx->mMDPComp[dpy]->prepare(ctx, list) < 0) {
                const int fbZ = 0;
                if(not ctx->mFBUpdate[dpy]->prepareAndValidate(ctx, list, fbZ))
                {
                    ctx->mOverlay->clear(dpy);
                    ctx->mLayerRotMap[dpy]->clear();
                }
            }
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
    setPaddingRound(ctx, (int)numDisplays, displays);
    setDMAState(ctx, (int)numDisplays, displays);
    setNumActiveDisplays(ctx, (int)numDisplays, displays);
    reset(ctx, (int)numDisplays, displays);

    ctx->mOverlay->configBegin();
    ctx->mRotMgr->configBegin();
    overlay::Writeback::configBegin();

    for (int32_t dpy = ((int32_t)numDisplays-1); dpy >=0 ; dpy--) {
        hwc_display_contents_1_t *list = displays[dpy];
        resetROI(ctx, dpy);
        switch(dpy) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_prepare_primary(dev, list);
                break;
            case HWC_DISPLAY_EXTERNAL:
                ret = hwc_prepare_external(dev, list);
                break;
            case HWC_DISPLAY_VIRTUAL:
                if(ctx->mHWCVirtual)
                    ret = ctx->mHWCVirtual->prepare(dev, list);
                break;
            default:
                ret = -EINVAL;
        }
    }

    ctx->mOverlay->configDone();
    ctx->mRotMgr->configDone();
    overlay::Writeback::configDone();
    // If VD list is deleted, mdp overlay pipe objects and writeback object
    // are deleted as part of configDone functions.
    // Proceed with HWCVirtualVDS object deletion.
    if(ctx->mHWCVirtual)
        ctx->mHWCVirtual->destroy(ctx, numDisplays, displays);

    return ret;
}

static int hwc_eventControl(struct hwc_composer_device_1* dev, int dpy,
                             int event, int enable)
{
    ATRACE_CALL();
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!validDisplay(dpy)) {
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
#ifdef QTI_BSP
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

static int hwc_setCursorPositionAsync(struct hwc_composer_device_1* dev,
        int dpy, int x, int y) {
    int ret = -1;
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    switch(dpy) {
        case HWC_DISPLAY_PRIMARY:
        {
            ATRACE_CALL();
            HWCursor* hwCursor = HWCursor::getInstance();
            ctx->mDrawLock.lock();
            if (hwCursor->isCursorSet() &&
                  hwCursor->setPositionAsync(ctx->dpyAttr[dpy].fd, x, y)) {
                ret = 0;
            }
            ctx->mDrawLock.unlock();
            break;
        }
        default:
            ret = 0;
            break;
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

    if(!validDisplay(dpy)) {
        return -EINVAL;
    }

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
            HWCursor::getInstance()->free(ctx->dpyAttr[dpy].fd);
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
        if(ctx->mHDMIDisplay->isHDMIPrimaryDisplay()) {
            if(ctx->dpyAttr[dpy].connected) {
                // When HDMI is connected as primary we clean up resources
                // and call commit to generate a black frame on the interface.
                // However, we do not call blank since we need the timing
                // generator and HDMI core to remain turned on.
                if((mode == HWC_POWER_MODE_OFF) &&
                        (!Overlay::displayCommit(ctx->dpyAttr[dpy].fd))) {
                    ALOGE("%s: display commit fail for %d", __FUNCTION__, dpy);
                    ret = -1;
                }
            }
        } else {
            if(ioctl(ctx->dpyAttr[dpy].fd, FBIOBLANK, value) < 0 ) {
                ALOGE("%s: ioctl FBIOBLANK failed for Primary with error %s"
                        " value %d", __FUNCTION__, strerror(errno), value);
                return -errno;
            }

            if(mode == HWC_POWER_MODE_NORMAL && !ctx->mHPDEnabled) {
                // Enable HPD here, as during bootup POWER_MODE_NORMAL is set
                // when SF is completely initialized
                ctx->mHDMIDisplay->setHPD(1);
                ctx->mHPDEnabled = true;
            }

            ctx->dpyAttr[dpy].isActive = not(mode == HWC_POWER_MODE_OFF);
        }
        //Deliberate fall through since there is no explicit power mode for
        //virtual displays.
    case HWC_DISPLAY_VIRTUAL:
        if(ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].connected) {
            const int dpy = HWC_DISPLAY_VIRTUAL;
            if(mode == HWC_POWER_MODE_OFF and
                    (not ctx->dpyAttr[dpy].isPause)) {
                if(!Overlay::displayCommit(ctx->dpyAttr[dpy].fd)) {
                    ALOGE("%s: displayCommit failed for virtual", __FUNCTION__);
                    ret = -1;
                }
            }
            ctx->dpyAttr[dpy].isActive = not(mode == HWC_POWER_MODE_OFF);
        }
        break;
    case HWC_DISPLAY_EXTERNAL:
        if(mode == HWC_POWER_MODE_OFF and ctx->dpyAttr[dpy].connected) {
            if(!Overlay::displayCommit(ctx->dpyAttr[dpy].fd)) {
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

    if (!ctx->dpyAttr[HWC_DISPLAY_PRIMARY].isActive) {
        ALOGD ("%s : Display OFF - Skip BLANK & UNBLANK", __FUNCTION__);
        ctx->mPanelResetStatus = false;
        return;
    }

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
        if(ctx->mMDP.hasOverlay) {
            supported |= HWC_DISPLAY_VIRTUAL_BIT;
            if(!(qdutils::MDPVersion::getInstance().is8x26() ||
                        qdutils::MDPVersion::getInstance().is8x16() ||
                        qdutils::MDPVersion::getInstance().is8x39()))
                supported |= HWC_DISPLAY_EXTERNAL_BIT;
        }
        value[0] = supported;
        break;
    case HWC_FORMAT_RB_SWAP:
        value[0] = 1;
        break;
    case HWC_COLOR_FILL:
        value[0] = 1;
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
    if (LIKELY(list) && ctx->dpyAttr[dpy].isActive
            && !ctx->dpyAttr[dpy].isPause) {
        size_t last = list->numHwLayers - 1;
        hwc_layer_1_t *fbLayer = &list->hwLayers[last];
        int fd = -1; //FenceFD from the Copybit(valid in async mode)
        bool copybitDone = false;

        if (ctx->mCopyBit[dpy]) {
            if (ctx->mMDP.version < qdutils::MDP_V4_0)
                copybitDone = ctx->mCopyBit[dpy]->draw(ctx, list, dpy, &fd);
            else
                fd = ctx->mMDPComp[dpy]->drawOverlap(ctx, list);
        }

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
        if(copybitDone && ctx->mMDP.version >= qdutils::MDP_V4_0) {
            hnd = ctx->mCopyBit[dpy]->getCurrentRenderBuffer();
        }

        if(isAbcInUse(ctx) == true) {
            int index = ctx->listStats[dpy].renderBufIndexforABC;
            hwc_layer_1_t *tempLayer = &list->hwLayers[index];
            hnd = (private_handle_t *)tempLayer->handle;
        }

        if(hnd) {
            if (!ctx->mFBUpdate[dpy]->draw(ctx, hnd)) {
                ALOGE("%s: FBUpdate draw failed", __FUNCTION__);
                ret = -1;
            }
        }

        /* When source split is enabled, right ROI will always be NULL since the
         * ROI for the whole panel generated in a single coordinate system will
         * be populuated in left ROI. So leave the right ROI untouched */
        int lSplit = qdutils::MDPVersion::getInstance().isSrcSplit() ? 0
                :getLeftSplit(ctx, dpy);
        qhwc::ovutils::Dim lRoi = qhwc::ovutils::Dim(
            ctx->listStats[dpy].lRoi.left,
            ctx->listStats[dpy].lRoi.top,
            ctx->listStats[dpy].lRoi.right - ctx->listStats[dpy].lRoi.left,
            ctx->listStats[dpy].lRoi.bottom - ctx->listStats[dpy].lRoi.top);

        qhwc::ovutils::Dim rRoi = qhwc::ovutils::Dim(
            ctx->listStats[dpy].rRoi.left - lSplit,
            ctx->listStats[dpy].rRoi.top,
            ctx->listStats[dpy].rRoi.right - ctx->listStats[dpy].rRoi.left,
            ctx->listStats[dpy].rRoi.bottom - ctx->listStats[dpy].rRoi.top);

        if(!Overlay::displayCommit(ctx->dpyAttr[dpy].fd, lRoi, rRoi)) {
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
        size_t last = list->numHwLayers - 1;
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

        private_handle_t *hnd = (private_handle_t *)fbLayer->handle;
        if(copybitDone) {
            hnd = ctx->mCopyBit[dpy]->getCurrentRenderBuffer();
        }

        if(hnd) {
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

static int hwc_set(hwc_composer_device_1 *dev,
                   size_t numDisplays,
                   hwc_display_contents_1_t** displays)
{
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    for (int dpy = 0; dpy < (int)numDisplays; dpy++) {
        hwc_display_contents_1_t* list = displays[dpy];
        switch(dpy) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_set_primary(ctx, list);
                break;
            case HWC_DISPLAY_EXTERNAL:
                ret = hwc_set_external(ctx, list);
                break;
            case HWC_DISPLAY_VIRTUAL:
                if(ctx->mHWCVirtual)
                    ret = ctx->mHWCVirtual->set(ctx, list);
                break;
            default:
                ret = -EINVAL;
        }
    }
    // This is only indicative of how many times SurfaceFlinger posts
    // frames to the display.
    CALC_FPS();
    MDPComp::resetIdleFallBack();
    ctx->mVideoTransFlag = false;
    //Was locked at the beginning of prepare
    ctx->mDrawLock.unlock();

    if(ctx->triggerRefresh) {
        ctx->triggerRefresh = false;
        if(ctx->proc) {
            ctx->proc->invalidate(ctx->proc);
        }
    }

    return ret;
}

int hwc_getDisplayConfigs(struct hwc_composer_device_1* dev, int disp,
        uint32_t* configs, size_t* numConfigs) {
    hwc_context_t* ctx = (hwc_context_t*)(dev);

    Locker::Autolock _l(ctx->mDrawLock);

    if(!validDisplay(disp)) {
        return -EINVAL;
    }

    bool hotPluggable = isHotPluggable(ctx, disp);
    bool isVirtualDisplay = (disp == HWC_DISPLAY_VIRTUAL);
    // If hotpluggable or virtual displays are inactive return error
    if ((hotPluggable || isVirtualDisplay) && !ctx->dpyAttr[disp].connected) {
        ALOGW("%s display (%d) is not connected anymore", __FUNCTION__, disp);
        return -EINVAL;
    }

    if (*numConfigs <= 0) {
        ALOGE("%s Invalid number of configs (%zu)", __FUNCTION__, *numConfigs);
        return -EINVAL;
    }

    switch(disp) {
        case HWC_DISPLAY_PRIMARY:
            if (hotPluggable) {
                ctx->mHDMIDisplay->getDisplayConfigs(configs, numConfigs);
            } else {
                configs[0] = 0;
                *numConfigs = 1;
            }
            break;
        case HWC_DISPLAY_EXTERNAL:
                ctx->mHDMIDisplay->getDisplayConfigs(configs, numConfigs);
            break;
        case HWC_DISPLAY_VIRTUAL:
            configs[0] = 0;
            *numConfigs = 1;
            break;
    }
    return 0;
}

int hwc_getDisplayAttributes(struct hwc_composer_device_1* dev, int disp,
        uint32_t config, const uint32_t* attributes, int32_t* values) {

    hwc_context_t* ctx = (hwc_context_t*)(dev);

    Locker::Autolock _l(ctx->mDrawLock);

    if(!validDisplay(disp)) {
        return -EINVAL;
    }

    bool hotPluggable = isHotPluggable(ctx, disp);
    bool isVirtualDisplay = (disp == HWC_DISPLAY_VIRTUAL);
    // If hotpluggable or virtual displays are inactive return error
    if ((hotPluggable || isVirtualDisplay) && !ctx->dpyAttr[disp].connected) {
        ALOGE("%s display (%d) is inactive", __FUNCTION__, disp);
        return -EINVAL;
    }

    //From HWComposer
    static const uint32_t DISPLAY_ATTRIBUTES[] = {
        HWC_DISPLAY_VSYNC_PERIOD,
        HWC_DISPLAY_WIDTH,
        HWC_DISPLAY_HEIGHT,
        HWC_DISPLAY_DPI_X,
        HWC_DISPLAY_DPI_Y,
        HWC_DISPLAY_NO_ATTRIBUTE,
    };

    const size_t NUM_DISPLAY_ATTRIBUTES = (sizeof(DISPLAY_ATTRIBUTES) /
            sizeof(DISPLAY_ATTRIBUTES)[0]);

    uint32_t xres = 0, yres = 0, refresh = 0, fps = 0;
    int ret = 0;
    if (hotPluggable) {
        ret = ctx->mHDMIDisplay->getAttrForConfig(config, xres,
                    yres, refresh, fps);
        if(ret < 0) {
            ALOGE("%s Error getting attributes for config %d",
                    __FUNCTION__, config);
            return ret;
        }
        if((uint32_t)ctx->mHDMIDisplay->getActiveConfig() == config &&
                ctx->mHDMIDisplay->getMDPScalingMode()) {
            xres = ctx->mHDMIDisplay->getFBWidth();
            yres = ctx->mHDMIDisplay->getFBHeight();
        }
    }

    for (size_t i = 0; i < NUM_DISPLAY_ATTRIBUTES - 1; i++) {
        switch (attributes[i]) {
        case HWC_DISPLAY_VSYNC_PERIOD:
            values[i] =
                    hotPluggable ? refresh : ctx->dpyAttr[disp].vsync_period;
            break;
        case HWC_DISPLAY_WIDTH:
            if (ctx->dpyAttr[disp].fbScaling)
                values[i] = ctx->dpyAttr[disp].xresFB;
            else
                values[i] = hotPluggable ? xres : ctx->dpyAttr[disp].xres;

            ALOGD("%s disp = %d, width = %d",__FUNCTION__, disp,
                    values[i]);
            break;
        case HWC_DISPLAY_HEIGHT:
            if (ctx->dpyAttr[disp].fbScaling)
                values[i] = ctx->dpyAttr[disp].yresFB;
            else
                values[i] = hotPluggable ? yres : ctx->dpyAttr[disp].yres;
            ALOGD("%s disp = %d, height = %d",__FUNCTION__, disp,
                    values[i]);
            break;
        case HWC_DISPLAY_DPI_X:
            values[i] = (int32_t) (ctx->dpyAttr[disp].xdpi*1000.0);
            break;
        case HWC_DISPLAY_DPI_Y:
            values[i] = (int32_t) (ctx->dpyAttr[disp].ydpi*1000.0);
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
    dumpsys_log(aBuf, "  DynRefreshRate=%d\n",
                ctx->dpyAttr[HWC_DISPLAY_PRIMARY].dynRefreshRate);
    for(int dpy = 0; dpy < HWC_NUM_DISPLAY_TYPES; dpy++) {
        if(dpy == HWC_DISPLAY_PRIMARY)
            dumpsys_log(aBuf, "Dpy %d: FB Scale Ratio w %.1f, h %.1f\n", dpy,
                    ctx->dpyAttr[dpy].fbWidthScaleRatio,
                    ctx->dpyAttr[dpy].fbHeightScaleRatio);
        if(ctx->mMDPComp[dpy])
            ctx->mMDPComp[dpy]->dump(aBuf, ctx);
    }
    char ovDump[3072] = {'\0'};
    ctx->mOverlay->getDump(ovDump, 3072);
    dumpsys_log(aBuf, ovDump);
    ovDump[0] = '\0';
    ctx->mRotMgr->getDump(ovDump, 1024);
    dumpsys_log(aBuf, ovDump);
    ovDump[0] = '\0';
    if(Writeback::getDump(ovDump, 512)) {
        dumpsys_log(aBuf, ovDump);
        ovDump[0] = '\0';
    }
    HWCursor::getInstance()->getDump(ovDump, 512);
    dumpsys_log(aBuf, ovDump);
    ovDump[0] = '\0';
    strlcpy(buff, aBuf.string(), buff_len);
}

int hwc_getActiveConfig(struct hwc_composer_device_1* dev, int disp)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);

    Locker::Autolock _l(ctx->mDrawLock);
    if(!validDisplay(disp)) {
        return -EINVAL;
    }

    bool hotPluggable = isHotPluggable(ctx, disp);
    bool isVirtualDisplay = (disp == HWC_DISPLAY_VIRTUAL);
    // If hotpluggable or virtual displays are inactive return error
    if ((hotPluggable || isVirtualDisplay) && !ctx->dpyAttr[disp].connected) {
        ALOGE("%s display (%d) is inactive", __FUNCTION__, disp);
        return -EINVAL;
    }

    // For use cases when primary panel is the default interface we only have
    // the default config (0th index)
    if (!hotPluggable) {
        return 0;
    }

    return ctx->mHDMIDisplay->getActiveConfig();
}

int hwc_setActiveConfig(struct hwc_composer_device_1* dev, int disp, int index)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);

    Locker::Autolock _l(ctx->mDrawLock);
    if(!validDisplay(disp)) {
        return -EINVAL;
    }

    bool hotPluggable = isHotPluggable(ctx, disp);
    bool isVirtualDisplay = (disp == HWC_DISPLAY_VIRTUAL);
    // If hotpluggable or virtual displays are inactive return error
    if ((hotPluggable || isVirtualDisplay) && !ctx->dpyAttr[disp].connected) {
        ALOGE("%s display (%d) is inactive", __FUNCTION__, disp);
        return -EINVAL;
    }

    // For use cases when primary panel is the default interface we only have
    // the default config (0th index)
    if (!hotPluggable) {
        // Primary and virtual supports only the default config (0th index)
        return (index == 0) ? index : -EINVAL;
    }

    return ctx->mHDMIDisplay->setActiveConfig(index);
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
        if(dev == NULL)
            return status;
        memset(dev, 0, sizeof(*dev));

        //Initialize hwc context
        status = initContext(dev);
        if (status < 0) {
            free(dev);
            return status;
        }

        //Setup HWC methods
        dev->device.common.tag          = HARDWARE_DEVICE_TAG;
        dev->device.common.version      = HWC_DEVICE_API_VERSION_1_5;
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
        dev->device.setCursorPositionAsync = hwc_setCursorPositionAsync;
        *device = &dev->device.common;
        status = 0;
    }
    return status;
}
