/*
 * Copyright (C) 2012-2013, The Linux Foundation. All rights reserved.
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

#include <math.h>
#include "hwc_mdpcomp.h"
#include <sys/ioctl.h>
#include "external.h"
#include "virtual.h"
#include "qdMetaData.h"
#include "mdp_version.h"
#include "hwc_fbupdate.h"
#include "hwc_ad.h"
#include <overlayRotator.h>

using namespace overlay;
using namespace qdutils;
using namespace overlay::utils;
namespace ovutils = overlay::utils;

namespace qhwc {

//==============MDPComp========================================================

IdleInvalidator *MDPComp::idleInvalidator = NULL;
bool MDPComp::sIdleFallBack = false;
bool MDPComp::sDebugLogs = false;
bool MDPComp::sEnabled = false;
bool MDPComp::sEnableMixedMode = true;
bool MDPComp::sEnablePartialFrameUpdate = false;
int MDPComp::sMaxPipesPerMixer = MAX_PIPES_PER_MIXER;
double MDPComp::sMaxBw = 0.0;
double MDPComp::sBwClaimed = 0.0;
bool MDPComp::sEnable4k2kYUVSplit = false;

MDPComp* MDPComp::getObject(hwc_context_t *ctx, const int& dpy) {
    if(isDisplaySplit(ctx, dpy)) {
        return new MDPCompSplit(dpy);
    }
    return new MDPCompNonSplit(dpy);
}

MDPComp::MDPComp(int dpy):mDpy(dpy){};

void MDPComp::dump(android::String8& buf)
{
    if(mCurrentFrame.layerCount > MAX_NUM_APP_LAYERS)
        return;

    dumpsys_log(buf,"HWC Map for Dpy: %s \n",
                (mDpy == 0) ? "\"PRIMARY\"" :
                (mDpy == 1) ? "\"EXTERNAL\"" : "\"VIRTUAL\"");
    dumpsys_log(buf,"CURR_FRAME: layerCount:%2d mdpCount:%2d "
                "fbCount:%2d \n", mCurrentFrame.layerCount,
                mCurrentFrame.mdpCount, mCurrentFrame.fbCount);
    dumpsys_log(buf,"needsFBRedraw:%3s  pipesUsed:%2d  MaxPipesPerMixer: %d \n",
                (mCurrentFrame.needsRedraw? "YES" : "NO"),
                mCurrentFrame.mdpCount, sMaxPipesPerMixer);
    dumpsys_log(buf," ---------------------------------------------  \n");
    dumpsys_log(buf," listIdx | cached? | mdpIndex | comptype  |  Z  \n");
    dumpsys_log(buf," ---------------------------------------------  \n");
    for(int index = 0; index < mCurrentFrame.layerCount; index++ )
        dumpsys_log(buf," %7d | %7s | %8d | %9s | %2d \n",
                    index,
                    (mCurrentFrame.isFBComposed[index] ? "YES" : "NO"),
                     mCurrentFrame.layerToMDP[index],
                    (mCurrentFrame.isFBComposed[index] ?
                    (mCurrentFrame.drop[index] ? "DROP" :
                    (mCurrentFrame.needsRedraw ? "GLES" : "CACHE")) : "MDP"),
                    (mCurrentFrame.isFBComposed[index] ? mCurrentFrame.fbZ :
    mCurrentFrame.mdpToLayer[mCurrentFrame.layerToMDP[index]].pipeInfo->zOrder));
    dumpsys_log(buf,"\n");
}

bool MDPComp::init(hwc_context_t *ctx) {

    if(!ctx) {
        ALOGE("%s: Invalid hwc context!!",__FUNCTION__);
        return false;
    }

    char property[PROPERTY_VALUE_MAX];

    sEnabled = false;
    if((property_get("persist.hwc.mdpcomp.enable", property, NULL) > 0) &&
       (!strncmp(property, "1", PROPERTY_VALUE_MAX ) ||
        (!strncasecmp(property,"true", PROPERTY_VALUE_MAX )))) {
        sEnabled = true;
    }

    sEnableMixedMode = true;
    if((property_get("debug.mdpcomp.mixedmode.disable", property, NULL) > 0) &&
       (!strncmp(property, "1", PROPERTY_VALUE_MAX ) ||
        (!strncasecmp(property,"true", PROPERTY_VALUE_MAX )))) {
        sEnableMixedMode = false;
    }

    if(property_get("debug.mdpcomp.logs", property, NULL) > 0) {
        if(atoi(property) != 0)
            sDebugLogs = true;
    }

    if(property_get("persist.hwc.partialupdate.enable", property, NULL) > 0) {
        if((atoi(property) != 0) && ctx->mMDP.panel == MIPI_CMD_PANEL &&
           qdutils::MDPVersion::getInstance().is8x74v2())
            sEnablePartialFrameUpdate = true;
    }
    ALOGE_IF(isDebug(), "%s: Partial Update applicable?: %d",__FUNCTION__,
                                                    sEnablePartialFrameUpdate);

    sMaxPipesPerMixer = MAX_PIPES_PER_MIXER;
    if(property_get("debug.mdpcomp.maxpermixer", property, "-1") > 0) {
        int val = atoi(property);
        if(val >= 0)
            sMaxPipesPerMixer = min(val, MAX_PIPES_PER_MIXER);
    }

    if(ctx->mMDP.panel != MIPI_CMD_PANEL) {
        // Idle invalidation is not necessary on command mode panels
        long idle_timeout = DEFAULT_IDLE_TIME;
        if(property_get("debug.mdpcomp.idletime", property, NULL) > 0) {
            if(atoi(property) != 0)
                idle_timeout = atoi(property);
        }

        //create Idle Invalidator only when not disabled through property
        if(idle_timeout != -1)
            idleInvalidator = IdleInvalidator::getInstance();

        if(idleInvalidator == NULL) {
            ALOGE("%s: failed to instantiate idleInvalidator object",
                  __FUNCTION__);
        } else {
            idleInvalidator->init(timeout_handler, ctx, idle_timeout);
        }
    }

    if((property_get("debug.mdpcomp.4k2kSplit", property, "0") > 0) &&
            (!strncmp(property, "1", PROPERTY_VALUE_MAX ) ||
             (!strncasecmp(property,"true", PROPERTY_VALUE_MAX )))) {
        sEnable4k2kYUVSplit = true;
    }
    return true;
}

void MDPComp::reset(const int& numLayers, hwc_display_contents_1_t* list) {
    mCurrentFrame.reset(numLayers);
    mCachedFrame.cacheAll(list);
    mCachedFrame.updateCounts(mCurrentFrame);
}

void MDPComp::timeout_handler(void *udata) {
    struct hwc_context_t* ctx = (struct hwc_context_t*)(udata);

    if(!ctx) {
        ALOGE("%s: received empty data in timer callback", __FUNCTION__);
        return;
    }

    if(!ctx->proc) {
        ALOGE("%s: HWC proc not registered", __FUNCTION__);
        return;
    }
    sIdleFallBack = true;
    /* Trigger SF to redraw the current frame */
    ctx->proc->invalidate(ctx->proc);
}

void MDPComp::setMDPCompLayerFlags(hwc_context_t *ctx,
                                   hwc_display_contents_1_t* list) {
    LayerProp *layerProp = ctx->layerProp[mDpy];

    for(int index = 0; index < ctx->listStats[mDpy].numAppLayers; index++) {
        hwc_layer_1_t* layer = &(list->hwLayers[index]);
        if(!mCurrentFrame.isFBComposed[index]) {
            layerProp[index].mFlags |= HWC_MDPCOMP;
            layer->compositionType = HWC_OVERLAY;
            layer->hints |= HWC_HINT_CLEAR_FB;
        } else {
            /* Drop the layer when its already present in FB OR when it lies
             * outside frame's ROI */
            if(!mCurrentFrame.needsRedraw || mCurrentFrame.drop[index]) {
                layer->compositionType = HWC_OVERLAY;
            }
        }
    }
}

MDPComp::FrameInfo::FrameInfo() {
    reset(0);
}

void MDPComp::FrameInfo::reset(const int& numLayers) {
    for(int i = 0 ; i < MAX_PIPES_PER_MIXER && numLayers; i++ ) {
        if(mdpToLayer[i].pipeInfo) {
            delete mdpToLayer[i].pipeInfo;
            mdpToLayer[i].pipeInfo = NULL;
            //We dont own the rotator
            mdpToLayer[i].rot = NULL;
        }
    }

    memset(&mdpToLayer, 0, sizeof(mdpToLayer));
    memset(&layerToMDP, -1, sizeof(layerToMDP));
    memset(&isFBComposed, 1, sizeof(isFBComposed));

    layerCount = numLayers;
    fbCount = numLayers;
    mdpCount = 0;
    needsRedraw = true;
    fbZ = 0;
}

void MDPComp::FrameInfo::map() {
    // populate layer and MDP maps
    int mdpIdx = 0;
    for(int idx = 0; idx < layerCount; idx++) {
        if(!isFBComposed[idx]) {
            mdpToLayer[mdpIdx].listIndex = idx;
            layerToMDP[idx] = mdpIdx++;
        }
    }
}

MDPComp::LayerCache::LayerCache() {
    reset();
}

void MDPComp::LayerCache::reset() {
    memset(&hnd, 0, sizeof(hnd));
    memset(&isFBComposed, true, sizeof(isFBComposed));
    memset(&drop, false, sizeof(drop));
    layerCount = 0;
}

void MDPComp::LayerCache::cacheAll(hwc_display_contents_1_t* list) {
    const int numAppLayers = list->numHwLayers - 1;
    for(int i = 0; i < numAppLayers; i++) {
        hnd[i] = list->hwLayers[i].handle;
    }
}

void MDPComp::LayerCache::updateCounts(const FrameInfo& curFrame) {
    layerCount = curFrame.layerCount;
    memcpy(&isFBComposed, &curFrame.isFBComposed, sizeof(isFBComposed));
    memcpy(&drop, &curFrame.drop, sizeof(drop));
}

bool MDPComp::LayerCache::isSameFrame(const FrameInfo& curFrame,
                                      hwc_display_contents_1_t* list) {
    if(layerCount != curFrame.layerCount)
        return false;
    for(int i = 0; i < curFrame.layerCount; i++) {
        if((curFrame.isFBComposed[i] != isFBComposed[i]) ||
                (curFrame.drop[i] != drop[i])) {
            return false;
        }
        if(curFrame.isFBComposed[i] &&
           (hnd[i] != list->hwLayers[i].handle)){
            return false;
        }
    }
    return true;
}

bool MDPComp::isSupportedForMDPComp(hwc_context_t *ctx, hwc_layer_1_t* layer) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if((not isYuvBuffer(hnd) and has90Transform(layer)) or
        (not isValidDimension(ctx,layer))
        //More conditions here, SKIP, sRGB+Blend etc
        ) {
        return false;
    }
    return true;
}

bool MDPComp::isValidDimension(hwc_context_t *ctx, hwc_layer_1_t *layer) {
    const int dpy = HWC_DISPLAY_PRIMARY;
    private_handle_t *hnd = (private_handle_t *)layer->handle;

    if(!hnd) {
        if (layer->flags & HWC_COLOR_FILL) {
            // Color layer
            return true;
        }
        ALOGE("%s: layer handle is NULL", __FUNCTION__);
        return false;
    }

    //XXX: Investigate doing this with pixel phase on MDSS
    if(!isSecureBuffer(hnd) && isNonIntegralSourceCrop(layer->sourceCropf))
        return false;

    int hw_w = ctx->dpyAttr[mDpy].xres;
    int hw_h = ctx->dpyAttr[mDpy].yres;

    hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);
    hwc_rect_t dst = layer->displayFrame;
    int crop_w = crop.right - crop.left;
    int crop_h = crop.bottom - crop.top;
    int dst_w = dst.right - dst.left;
    int dst_h = dst.bottom - dst.top;
    float w_dscale = ceilf((float)crop_w / (float)dst_w);
    float h_dscale = ceilf((float)crop_h / (float)dst_h);

    /* Workaround for MDP HW limitation in DSI command mode panels where
     * FPS will not go beyond 30 if buffers on RGB pipes are of width or height
     * less than 5 pixels
     * There also is a HW limilation in MDP, minimum block size is 2x2
     * Fallback to GPU if height is less than 2.
     */
    if((crop_w < 5)||(crop_h < 5))
        return false;

    if((w_dscale > 1.0f) || (h_dscale > 1.0f)) {
        const uint32_t downscale =
            qdutils::MDPVersion::getInstance().getMaxMDPDownscale();
        if(ctx->mMDP.version >= qdutils::MDSS_V5) {
            /* Workaround for downscales larger than 4x.
             * Will be removed once decimator block is enabled for MDSS
             */
            if(!qdutils::MDPVersion::getInstance().supportsDecimation()) {
                if(crop_w > MAX_DISPLAY_DIM || w_dscale > downscale ||
                   h_dscale > downscale)
                    return false;
            } else {
                if(w_dscale > 64 || h_dscale > 64)
                    return false;
            }
        } else { //A-family
            if(w_dscale > downscale || h_dscale > downscale)
                return false;
        }
    }

    return true;
}

ovutils::eDest MDPComp::getMdpPipe(hwc_context_t *ctx, ePipeType type,
        int mixer) {
    overlay::Overlay& ov = *ctx->mOverlay;
    ovutils::eDest mdp_pipe = ovutils::OV_INVALID;

    switch(type) {
    case MDPCOMP_OV_DMA:
        mdp_pipe = ov.nextPipe(ovutils::OV_MDP_PIPE_DMA, mDpy, mixer);
        if(mdp_pipe != ovutils::OV_INVALID) {
            return mdp_pipe;
        }
    case MDPCOMP_OV_ANY:
    case MDPCOMP_OV_RGB:
        mdp_pipe = ov.nextPipe(ovutils::OV_MDP_PIPE_RGB, mDpy, mixer);
        if(mdp_pipe != ovutils::OV_INVALID) {
            return mdp_pipe;
        }

        if(type == MDPCOMP_OV_RGB) {
            //Requested only for RGB pipe
            break;
        }
    case  MDPCOMP_OV_VG:
        return ov.nextPipe(ovutils::OV_MDP_PIPE_VG, mDpy, mixer);
    default:
        ALOGE("%s: Invalid pipe type",__FUNCTION__);
        return ovutils::OV_INVALID;
    };
    return ovutils::OV_INVALID;
}

bool MDPComp::isFrameDoable(hwc_context_t *ctx) {
    bool ret = true;
    const int numAppLayers = ctx->listStats[mDpy].numAppLayers;

    if(!isEnabled()) {
        ALOGD_IF(isDebug(),"%s: MDP Comp. not enabled.", __FUNCTION__);
        ret = false;
    } else if(qdutils::MDPVersion::getInstance().is8x26() &&
            ctx->mVideoTransFlag && ctx->mVirtualDisplay->isConnected()) {
        //1 Padding round to shift pipes across mixers
        ALOGD_IF(isDebug(),"%s: MDP Comp. video transition padding round",
                __FUNCTION__);
        ret = false;
    } else if(ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].isConfiguring ||
              ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].isConfiguring) {
        ALOGD_IF( isDebug(),"%s: External Display connection is pending",
                  __FUNCTION__);
        ret = false;
    } else if(ctx->isPaddingRound) {
        ctx->isPaddingRound = false;
        ALOGD_IF(isDebug(), "%s: padding round",__FUNCTION__);
        ret = false;
    }
    return ret;
}

/*
 * 1) Identify layers that are not visible in the updating ROI and drop them
 * from composition.
 * 2) If we have a scaling layers which needs cropping against generated ROI.
 * Reset ROI to full resolution.
 */
bool MDPComp::validateAndApplyROI(hwc_context_t *ctx,
                               hwc_display_contents_1_t* list, hwc_rect_t roi) {
    int numAppLayers = ctx->listStats[mDpy].numAppLayers;

    if(!isValidRect(roi))
        return false;

    hwc_rect_t visibleRect = roi;

    for(int i = numAppLayers - 1; i >= 0; i--){

        if(!isValidRect(visibleRect)) {
            mCurrentFrame.drop[i] = true;
            mCurrentFrame.dropCount++;
        }

        const hwc_layer_1_t* layer =  &list->hwLayers[i];

        hwc_rect_t dstRect = layer->displayFrame;
        hwc_rect_t srcRect = integerizeSourceCrop(layer->sourceCropf);
        int transform = layer->transform;

        hwc_rect_t res  = getIntersection(visibleRect, dstRect);

        int res_w = res.right - res.left;
        int res_h = res.bottom - res.top;
        int dst_w = dstRect.right - dstRect.left;
        int dst_h = dstRect.bottom - dstRect.top;

        if(!isValidRect(res)) {
            mCurrentFrame.drop[i] = true;
            mCurrentFrame.dropCount++;
        }else {
            /* Reset frame ROI when any layer which needs scaling also needs ROI
             * cropping */
            if((res_w != dst_w || res_h != dst_h) &&
                    needsScaling (layer)) {
                ALOGI("%s: Resetting ROI due to scaling", __FUNCTION__);
                memset(&mCurrentFrame.drop, 0, sizeof(mCurrentFrame.drop));
                mCurrentFrame.dropCount = 0;
                return false;
            }
        }

        if (layer->blending == HWC_BLENDING_NONE)
            visibleRect = deductRect(visibleRect, res);
    }
    return true;
}

void MDPComp::generateROI(hwc_context_t *ctx, hwc_display_contents_1_t* list) {
    int numAppLayers = ctx->listStats[mDpy].numAppLayers;

    if(!sEnablePartialFrameUpdate) {
        return;
    }

    if(mDpy || isDisplaySplit(ctx, mDpy)){
        ALOGE_IF(isDebug(), "%s: ROI not supported for"
                 "the (1) external / virtual display's (2) dual DSI displays",
                 __FUNCTION__);
        return;
    }

    if(isSkipPresent(ctx, mDpy))
        return;

    if(list->flags & HWC_GEOMETRY_CHANGED)
        return;

    struct hwc_rect roi = (struct hwc_rect){0, 0, 0, 0};
    for(int index = 0; index < numAppLayers; index++ ) {
        if ((mCachedFrame.hnd[index] != list->hwLayers[index].handle) ||
            isYuvBuffer((private_handle_t *)list->hwLayers[index].handle)) {
            hwc_rect_t dstRect = list->hwLayers[index].displayFrame;
            hwc_rect_t srcRect = integerizeSourceCrop(
                                        list->hwLayers[index].sourceCropf);
            int transform = list->hwLayers[index].transform;

            /* Intersect against display boundaries */
            roi = getUnion(roi, dstRect);
        }
    }

    if(!validateAndApplyROI(ctx, list, roi)){
        roi = (struct hwc_rect) {0, 0,
                    (int)ctx->dpyAttr[mDpy].xres, (int)ctx->dpyAttr[mDpy].yres};
    }

    ctx->listStats[mDpy].roi.x = roi.left;
    ctx->listStats[mDpy].roi.y = roi.top;
    ctx->listStats[mDpy].roi.w = roi.right - roi.left;
    ctx->listStats[mDpy].roi.h = roi.bottom - roi.top;

    ALOGD_IF(isDebug(),"%s: generated ROI: [%d, %d, %d, %d]", __FUNCTION__,
                               roi.left, roi.top, roi.right, roi.bottom);
}

/* Checks for conditions where all the layers marked for MDP comp cannot be
 * bypassed. On such conditions we try to bypass atleast YUV layers */
bool MDPComp::isFullFrameDoable(hwc_context_t *ctx,
                                hwc_display_contents_1_t* list){

    const int numAppLayers = ctx->listStats[mDpy].numAppLayers;

    if(sIdleFallBack && !ctx->listStats[mDpy].secureUI) {
        ALOGD_IF(isDebug(), "%s: Idle fallback dpy %d",__FUNCTION__, mDpy);
        return false;
    }

    if(isSkipPresent(ctx, mDpy)) {
        ALOGD_IF(isDebug(),"%s: SKIP present: %d",
                __FUNCTION__,
                isSkipPresent(ctx, mDpy));
        return false;
    }

    for(int i = 0; i < numAppLayers; ++i) {
        hwc_layer_1_t* layer = &list->hwLayers[i];
        private_handle_t *hnd = (private_handle_t *)layer->handle;

        if(isYuvBuffer(hnd) && has90Transform(layer)) {
            if(!canUseRotator(ctx, mDpy)) {
                ALOGD_IF(isDebug(), "%s: Can't use rotator for dpy %d",
                        __FUNCTION__, mDpy);
                return false;
            }
        }

        //For 8x26 with panel width>1k, if RGB layer needs HFLIP fail mdp comp
        // may not need it if Gfx pre-rotation can handle all flips & rotations
        if(qdutils::MDPVersion::getInstance().is8x26() &&
                                (ctx->dpyAttr[mDpy].xres > 1024) &&
                                (layer->transform & HWC_TRANSFORM_FLIP_H) &&
                                (!isYuvBuffer(hnd)))
                   return false;
    }

    if(ctx->mAD->isDoable()) {
        return false;
    }

    //If all above hard conditions are met we can do full or partial MDP comp.
    bool ret = false;
    if(fullMDPComp(ctx, list)) {
        ret = true;
    } else if(partialMDPComp(ctx, list)) {
        ret = true;
    }

    if(!hwLimitationsCheck(ctx, list)) {
        ALOGD_IF(isDebug(), "%s: HW limitations",__FUNCTION__);
        return false;
    }

    return ret;
}

bool MDPComp::fullMDPComp(hwc_context_t *ctx, hwc_display_contents_1_t* list) {
    //Will benefit presentation / secondary-only layer.
    if((mDpy > HWC_DISPLAY_PRIMARY) &&
            (list->numHwLayers - 1) > MAX_SEC_LAYERS) {
        ALOGD_IF(isDebug(), "%s: Exceeds max secondary pipes",__FUNCTION__);
        return false;
    }

    const int numAppLayers = ctx->listStats[mDpy].numAppLayers;
    for(int i = 0; i < numAppLayers; i++) {
        hwc_layer_1_t* layer = &list->hwLayers[i];
        if(not isSupportedForMDPComp(ctx, layer)) {
            ALOGD_IF(isDebug(), "%s: Unsupported layer in list",__FUNCTION__);
            return false;
        }

        //For 8x26, if there is only one layer which needs scale for secondary
        //while no scale for primary display, DMA pipe is occupied by primary.
        //If need to fall back to GLES composition, virtual display lacks DMA
        //pipe and error is reported.
        if(qdutils::MDPVersion::getInstance().is8x26() &&
                                mDpy >= HWC_DISPLAY_EXTERNAL &&
                                qhwc::needsScaling(layer))
            return false;
    }
    mCurrentFrame.fbCount = 0;
    mCurrentFrame.fbZ = -1;
    memcpy(&mCurrentFrame.isFBComposed, &mCurrentFrame.drop,
           sizeof(mCurrentFrame.isFBComposed));
    mCurrentFrame.mdpCount = mCurrentFrame.layerCount - mCurrentFrame.fbCount -
        mCurrentFrame.dropCount;

    if(sEnable4k2kYUVSplit){
        adjustForSourceSplit(ctx, list);
    }

    if(!resourceCheck(ctx, list)) {
        ALOGD_IF(isDebug(), "%s: resource check failed", __FUNCTION__);
        return false;
    }

    return true;
}

bool MDPComp::partialMDPComp(hwc_context_t *ctx, hwc_display_contents_1_t* list)
{
    if(!sEnableMixedMode) {
        //Mixed mode is disabled. No need to even try caching.
        return false;
    }

    bool ret = false;
    if(list->flags & HWC_GEOMETRY_CHANGED) { //Try load based first
        ret =   loadBasedCompPreferGPU(ctx, list) or
                loadBasedCompPreferMDP(ctx, list) or
                cacheBasedComp(ctx, list);
    } else {
        ret =   cacheBasedComp(ctx, list) or
                loadBasedCompPreferGPU(ctx, list) or
                loadBasedCompPreferMDP(ctx, list);
    }

    return ret;
}

bool MDPComp::cacheBasedComp(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    int numAppLayers = ctx->listStats[mDpy].numAppLayers;
    mCurrentFrame.reset(numAppLayers);
    updateLayerCache(ctx, list);

    //If an MDP marked layer is unsupported cannot do partial MDP Comp
    for(int i = 0; i < numAppLayers; i++) {
        if(!mCurrentFrame.isFBComposed[i]) {
            hwc_layer_1_t* layer = &list->hwLayers[i];
            if(not isSupportedForMDPComp(ctx, layer)) {
                ALOGD_IF(isDebug(), "%s: Unsupported layer in list",
                        __FUNCTION__);
                return false;
            }
        }
    }

    updateYUV(ctx, list, false /*secure only*/);
    bool ret = markLayersForCaching(ctx, list); //sets up fbZ also
    if(!ret) {
        ALOGD_IF(isDebug(),"%s: batching failed, dpy %d",__FUNCTION__, mDpy);
        return false;
    }

    int mdpCount = mCurrentFrame.mdpCount;

    if(sEnable4k2kYUVSplit){
        adjustForSourceSplit(ctx, list);
    }

    //Will benefit cases where a video has non-updating background.
    if((mDpy > HWC_DISPLAY_PRIMARY) and
            (mdpCount > MAX_SEC_LAYERS)) {
        ALOGD_IF(isDebug(), "%s: Exceeds max secondary pipes",__FUNCTION__);
        return false;
    }

    if(!resourceCheck(ctx, list)) {
        ALOGD_IF(isDebug(), "%s: resource check failed", __FUNCTION__);
        return false;
    }

    return true;
}

bool MDPComp::loadBasedCompPreferGPU(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    if(not isLoadBasedCompDoable(ctx, list)) {
        return false;
    }

    int numAppLayers = ctx->listStats[mDpy].numAppLayers;
    mCurrentFrame.reset(numAppLayers);

    int stagesForMDP = min(sMaxPipesPerMixer, ctx->mOverlay->availablePipes(
            mDpy, Overlay::MIXER_DEFAULT));
    //If MDP has X possible stages, it can take X layers.
    const int batchSize = numAppLayers - (stagesForMDP - 1); //1 for FB

    if(batchSize <= 0) {
        ALOGD_IF(isDebug(), "%s: Not attempting", __FUNCTION__);
        return false;
    }

    int minBatchStart = -1;
    size_t minBatchPixelCount = SIZE_MAX;

    for(int i = 0; i <= numAppLayers - batchSize; i++) {
        uint32_t batchPixelCount = 0;
        for(int j = i; j < i + batchSize; j++) {
            hwc_layer_1_t* layer = &list->hwLayers[j];
            hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);
            batchPixelCount += (crop.right - crop.left) *
                    (crop.bottom - crop.top);
        }

        if(batchPixelCount < minBatchPixelCount) {
            minBatchPixelCount = batchPixelCount;
            minBatchStart = i;
        }
    }

    if(minBatchStart < 0) {
        ALOGD_IF(isDebug(), "%s: No batch found batchSize %d numAppLayers %d",
                __FUNCTION__, batchSize, numAppLayers);
        return false;
    }

    for(int i = 0; i < numAppLayers; i++) {
        if(i < minBatchStart || i >= minBatchStart + batchSize) {
            hwc_layer_1_t* layer = &list->hwLayers[i];
            if(not isSupportedForMDPComp(ctx, layer)) {
                ALOGD_IF(isDebug(), "%s: MDP unsupported layer found at %d",
                        __FUNCTION__, i);
                return false;
            }
            mCurrentFrame.isFBComposed[i] = false;
        }
    }

    mCurrentFrame.fbZ = minBatchStart;
    mCurrentFrame.fbCount = batchSize;
    mCurrentFrame.mdpCount = mCurrentFrame.layerCount - batchSize;

    if(sEnable4k2kYUVSplit){
        adjustForSourceSplit(ctx, list);
    }

    if(!resourceCheck(ctx, list)) {
        ALOGD_IF(isDebug(), "%s: resource check failed", __FUNCTION__);
        return false;
    }

    ALOGD_IF(isDebug(), "%s: fbZ %d batchSize %d",
                __FUNCTION__, mCurrentFrame.fbZ, batchSize);
    return true;
}

bool MDPComp::loadBasedCompPreferMDP(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    if(not isLoadBasedCompDoable(ctx, list)) {
        return false;
    }

    const int numAppLayers = ctx->listStats[mDpy].numAppLayers;
    mCurrentFrame.reset(numAppLayers);

    //Full screen is from ib perspective, not actual full screen
    const int bpp = 4;
    double panelRefRate =
                1000000000.0 / ctx->dpyAttr[mDpy].vsync_period;

    double bwLeft = sMaxBw - sBwClaimed;

    const int fullScreenLayers = bwLeft * 1000000000 / (ctx->dpyAttr[mDpy].xres
            * ctx->dpyAttr[mDpy].yres * bpp * panelRefRate);

    const int fbBatchSize = numAppLayers - (fullScreenLayers - 1);
    //If batch size is not at least 2, we aren't really preferring MDP, since
    //only 1 layer going to GPU could actually translate into an entire FB
    //needed to be fetched by MDP, thus needing more b/w rather than less.
    if(fbBatchSize < 2 || fbBatchSize > numAppLayers) {
        ALOGD_IF(isDebug(), "%s: Not attempting", __FUNCTION__);
        return false;
    }

    //Top-most layers constitute FB batch
    const int fbBatchStart = numAppLayers - fbBatchSize;

    //Bottom-most layers constitute MDP batch
    for(int i = 0; i < fbBatchStart; i++) {
        hwc_layer_1_t* layer = &list->hwLayers[i];
        if(not isSupportedForMDPComp(ctx, layer)) {
            ALOGD_IF(isDebug(), "%s: MDP unsupported layer found at %d",
                    __FUNCTION__, i);
            return false;
        }
        mCurrentFrame.isFBComposed[i] = false;
    }

    mCurrentFrame.fbZ = fbBatchStart;
    mCurrentFrame.fbCount = fbBatchSize;
    mCurrentFrame.mdpCount = mCurrentFrame.layerCount - fbBatchSize;

    if(sEnable4k2kYUVSplit){
        adjustForSourceSplit(ctx, list);
    }

    if(!resourceCheck(ctx, list)) {
        ALOGD_IF(isDebug(), "%s: resource check failed", __FUNCTION__);
        return false;
    }

    ALOGD_IF(isDebug(), "%s: FB Z %d, num app layers %d, MDP Batch Size %d",
                __FUNCTION__, mCurrentFrame.fbZ, numAppLayers,
                numAppLayers - fbBatchSize);

    return true;
}

bool MDPComp::isLoadBasedCompDoable(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    if(mDpy or isSecurePresent(ctx, mDpy)) {
        return false;
    }
    return true;
}

bool MDPComp::isOnlyVideoDoable(hwc_context_t *ctx,
        hwc_display_contents_1_t* list, bool secureOnly) {
    int numAppLayers = ctx->listStats[mDpy].numAppLayers;

    mCurrentFrame.reset(numAppLayers);
    updateYUV(ctx, list, secureOnly);
    int mdpCount = mCurrentFrame.mdpCount;

    if(!isYuvPresent(ctx, mDpy)) {
        return false;
    }

    /* Bail out if we are processing only secured video layers
     * and we dont have any */
    if(!isSecurePresent(ctx, mDpy) && secureOnly){
        return false;
    }

    if(!mdpCount)
        return false;

    if(!resourceCheck(ctx, list)) {
        ALOGD_IF(isDebug(), "%s: resource check failed", __FUNCTION__);
        return false;
    }

    return true;
}

/* Checks for conditions where YUV layers cannot be bypassed */
bool MDPComp::isYUVDoable(hwc_context_t* ctx, hwc_layer_1_t* layer) {
    bool extAnimBlockFeature = mDpy && ctx->listStats[mDpy].isDisplayAnimating;

    if(isSkipLayer(layer) && !extAnimBlockFeature) {
        ALOGD_IF(isDebug(), "%s: Video marked SKIP dpy %d", __FUNCTION__, mDpy);
        return false;
    }

    if(layer->transform & HWC_TRANSFORM_ROT_90 && !canUseRotator(ctx,mDpy)) {
        ALOGD_IF(isDebug(), "%s: no free DMA pipe",__FUNCTION__);
        return false;
    }

    if(isSecuring(ctx, layer)) {
        ALOGD_IF(isDebug(), "%s: MDP securing is active", __FUNCTION__);
        return false;
    }

    if(!isValidDimension(ctx, layer)) {
        ALOGD_IF(isDebug(), "%s: Buffer is of invalid width",
            __FUNCTION__);
        return false;
    }

    if(layer->planeAlpha < 0xFF) {
        ALOGD_IF(isDebug(), "%s: Cannot handle YUV layer with plane alpha\
                 in video only mode",
                 __FUNCTION__);
        return false;
    }

    return true;
}

/* starts at fromIndex and check for each layer to find
 * if it it has overlapping with any Updating layer above it in zorder
 * till the end of the batch. returns true if it finds any intersection */
bool MDPComp::canPushBatchToTop(const hwc_display_contents_1_t* list,
        int fromIndex, int toIndex) {
    for(int i = fromIndex; i < toIndex; i++) {
        if(mCurrentFrame.isFBComposed[i] && !mCurrentFrame.drop[i]) {
            if(intersectingUpdatingLayers(list, i+1, toIndex, i)) {
                return false;
            }
        }
    }
    return true;
}

/* Checks if given layer at targetLayerIndex has any
 * intersection with all the updating layers in beween
 * fromIndex and toIndex. Returns true if it finds intersectiion */
bool MDPComp::intersectingUpdatingLayers(const hwc_display_contents_1_t* list,
        int fromIndex, int toIndex, int targetLayerIndex) {
    for(int i = fromIndex; i <= toIndex; i++) {
        if(!mCurrentFrame.isFBComposed[i]) {
            if(areLayersIntersecting(&list->hwLayers[i],
                        &list->hwLayers[targetLayerIndex]))  {
                return true;
            }
        }
    }
    return false;
}

int MDPComp::getBatch(hwc_display_contents_1_t* list,
        int& maxBatchStart, int& maxBatchEnd,
        int& maxBatchCount) {
    int i = 0;
    int fbZOrder =-1;
    while (i < mCurrentFrame.layerCount) {
        int batchCount = 0;
        int batchStart = i;
        int batchEnd = i;
        int fbZ = batchStart;
        int firstZReverseIndex = -1;
        int updatingLayersAbove = 0;//Updating layer count in middle of batch
        while(i < mCurrentFrame.layerCount) {
            if(!mCurrentFrame.isFBComposed[i]) {
                if(!batchCount) {
                    i++;
                    break;
                }
                updatingLayersAbove++;
                i++;
                continue;
            } else {
                if(mCurrentFrame.drop[i]) {
                    i++;
                    continue;
                } else if(updatingLayersAbove <= 0) {
                    batchCount++;
                    batchEnd = i;
                    i++;
                    continue;
                } else { //Layer is FBComposed, not a drop & updatingLayer > 0

                    // We have a valid updating layer already. If layer-i not
                    // have overlapping with all updating layers in between
                    // batch-start and i, then we can add layer i to batch.
                    if(!intersectingUpdatingLayers(list, batchStart, i-1, i)) {
                        batchCount++;
                        batchEnd = i;
                        i++;
                        continue;
                    } else if(canPushBatchToTop(list, batchStart, i)) {
                        //If All the non-updating layers with in this batch
                        //does not have intersection with the updating layers
                        //above in z-order, then we can safely move the batch to
                        //higher z-order. Increment fbZ as it is moving up.
                        if( firstZReverseIndex < 0) {
                            firstZReverseIndex = i;
                        }
                        batchCount++;
                        batchEnd = i;
                        fbZ += updatingLayersAbove;
                        i++;
                        updatingLayersAbove = 0;
                        continue;
                    } else {
                        //both failed.start the loop again from here.
                        if(firstZReverseIndex >= 0) {
                            i = firstZReverseIndex;
                        }
                        break;
                    }
                }
            }
        }
        if(batchCount > maxBatchCount) {
            maxBatchCount = batchCount;
            maxBatchStart = batchStart;
            maxBatchEnd = batchEnd;
            fbZOrder = fbZ;
        }
    }
    return fbZOrder;
}

bool  MDPComp::markLayersForCaching(hwc_context_t* ctx,
        hwc_display_contents_1_t* list) {
    /* Idea is to keep as many non-updating(cached) layers in FB and
     * send rest of them through MDP. This is done in 2 steps.
     *   1. Find the maximum contiguous batch of non-updating layers.
     *   2. See if we can improve this batch size for caching by adding
     *      opaque layers around the batch, if they don't have
     *      any overlapping with the updating layers in between.
     * NEVER mark an updating layer for caching.
     * But cached ones can be marked for MDP */

    int maxBatchStart = -1;
    int maxBatchEnd = -1;
    int maxBatchCount = 0;
    int fbZ = -1;

    /* All or Nothing is cached. No batching needed */
    if(!mCurrentFrame.fbCount) {
        mCurrentFrame.fbZ = -1;
        return true;
    }
    if(!mCurrentFrame.mdpCount) {
        mCurrentFrame.fbZ = 0;
        return true;
    }

    fbZ = getBatch(list, maxBatchStart, maxBatchEnd, maxBatchCount);

    /* reset rest of the layers lying inside ROI for MDP comp */
    for(int i = 0; i < mCurrentFrame.layerCount; i++) {
        hwc_layer_1_t* layer = &list->hwLayers[i];
        if((i < maxBatchStart || i > maxBatchEnd) &&
                mCurrentFrame.isFBComposed[i]){
            if(!mCurrentFrame.drop[i]){
                //If an unsupported layer is being attempted to
                //be pulled out we should fail
                if(not isSupportedForMDPComp(ctx, layer)) {
                    return false;
                }
                mCurrentFrame.isFBComposed[i] = false;
            }
        }
    }

    // update the frame data
    mCurrentFrame.fbZ = fbZ;
    mCurrentFrame.fbCount = maxBatchCount;
    mCurrentFrame.mdpCount = mCurrentFrame.layerCount -
            mCurrentFrame.fbCount - mCurrentFrame.dropCount;

    ALOGD_IF(isDebug(),"%s: cached count: %d",__FUNCTION__,
            mCurrentFrame.fbCount);

    return true;
}

void MDPComp::updateLayerCache(hwc_context_t* ctx,
        hwc_display_contents_1_t* list) {
    int numAppLayers = ctx->listStats[mDpy].numAppLayers;
    int fbCount = 0;

    for(int i = 0; i < numAppLayers; i++) {
        hwc_layer_1_t* layer = &list->hwLayers[i];
        if (mCachedFrame.hnd[i] == list->hwLayers[i].handle) {
            if(!mCurrentFrame.drop[i])
                fbCount++;
            mCurrentFrame.isFBComposed[i] = true;
        } else {
            mCurrentFrame.isFBComposed[i] = false;
        }
    }

    mCurrentFrame.fbCount = fbCount;
    mCurrentFrame.mdpCount = mCurrentFrame.layerCount - mCurrentFrame.fbCount
                                                    - mCurrentFrame.dropCount;

    ALOGD_IF(isDebug(),"%s: MDP count: %d FB count %d drop count: %d"
             ,__FUNCTION__, mCurrentFrame.mdpCount, mCurrentFrame.fbCount,
            mCurrentFrame.dropCount);
}

void MDPComp::updateYUV(hwc_context_t* ctx, hwc_display_contents_1_t* list,
        bool secureOnly) {
    int nYuvCount = ctx->listStats[mDpy].yuvCount;
    if(!nYuvCount && mDpy) {
        //Reset "No animation on external display" related  parameters.
        ctx->mPrevCropVideo.left = ctx->mPrevCropVideo.top =
            ctx->mPrevCropVideo.right = ctx->mPrevCropVideo.bottom = 0;
        ctx->mPrevDestVideo.left = ctx->mPrevDestVideo.top =
            ctx->mPrevDestVideo.right = ctx->mPrevDestVideo.bottom = 0;
        ctx->mPrevTransformVideo = 0;
        return;
     }
    for(int index = 0;index < nYuvCount; index++){
        int nYuvIndex = ctx->listStats[mDpy].yuvIndices[index];
        hwc_layer_1_t* layer = &list->hwLayers[nYuvIndex];

        if(!isYUVDoable(ctx, layer)) {
            if(!mCurrentFrame.isFBComposed[nYuvIndex]) {
                mCurrentFrame.isFBComposed[nYuvIndex] = true;
                mCurrentFrame.fbCount++;
            }
        } else {
            if(mCurrentFrame.isFBComposed[nYuvIndex]) {
                private_handle_t *hnd = (private_handle_t *)layer->handle;
                if(!secureOnly || isSecureBuffer(hnd)) {
                    mCurrentFrame.isFBComposed[nYuvIndex] = false;
                    mCurrentFrame.fbCount--;
                }
            }
        }
    }

    mCurrentFrame.mdpCount = mCurrentFrame.layerCount -
            mCurrentFrame.fbCount - mCurrentFrame.dropCount;
    ALOGD_IF(isDebug(),"%s: fb count: %d",__FUNCTION__,
             mCurrentFrame.fbCount);
}

bool MDPComp::programMDP(hwc_context_t *ctx, hwc_display_contents_1_t* list) {
    //Configure framebuffer first if applicable
    if(mCurrentFrame.fbZ >= 0) {
        if(!ctx->mFBUpdate[mDpy]->prepare(ctx, list, mCurrentFrame.fbZ)) {
            ALOGD_IF(isDebug(), "%s configure framebuffer failed",
                    __FUNCTION__);
            return false;
        }
    }

    if(!allocLayerPipes(ctx, list)) {
        ALOGD_IF(isDebug(), "%s: Unable to allocate MDP pipes", __FUNCTION__);
        return false;
    }

    for (int index = 0, mdpNextZOrder = 0; index < mCurrentFrame.layerCount;
            index++) {
        if(!mCurrentFrame.isFBComposed[index]) {
            int mdpIndex = mCurrentFrame.layerToMDP[index];
            hwc_layer_1_t* layer = &list->hwLayers[index];

            //Leave fbZ for framebuffer. CACHE/GLES layers go here.
            if(mdpNextZOrder == mCurrentFrame.fbZ) {
                mdpNextZOrder++;
            }
            MdpPipeInfo* cur_pipe = mCurrentFrame.mdpToLayer[mdpIndex].pipeInfo;
            cur_pipe->zOrder = mdpNextZOrder++;

            private_handle_t *hnd = (private_handle_t *)layer->handle;
            if(is4kx2kYuvBuffer(hnd) && sEnable4k2kYUVSplit){
                if(configure4k2kYuv(ctx, layer,
                            mCurrentFrame.mdpToLayer[mdpIndex])
                        != 0 ){
                    ALOGD_IF(isDebug(), "%s: Failed to configure split pipes \
                            for layer %d",__FUNCTION__, index);
                    return false;
                }
                else{
                    mdpNextZOrder++;
                }
                continue;
            }
            if(configure(ctx, layer, mCurrentFrame.mdpToLayer[mdpIndex]) != 0 ){
                ALOGD_IF(isDebug(), "%s: Failed to configure overlay for \
                        layer %d",__FUNCTION__, index);
                return false;
            }
        }
    }

    return true;
}

bool MDPComp::resourceCheck(hwc_context_t *ctx,
        hwc_display_contents_1_t *list) {
    const bool fbUsed = mCurrentFrame.fbCount;
    if(mCurrentFrame.mdpCount > sMaxPipesPerMixer - fbUsed) {
        ALOGD_IF(isDebug(), "%s: Exceeds MAX_PIPES_PER_MIXER",__FUNCTION__);
        return false;
    }

    if(!arePipesAvailable(ctx, list)) {
        return false;
    }

    double size = calcMDPBytesRead(ctx, list);
    if(!bandwidthCheck(ctx, size)) {
        ALOGD_IF(isDebug(), "%s: Exceeds bandwidth",__FUNCTION__);
        return false;
    }

    return true;
}

double MDPComp::calcMDPBytesRead(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    double size = 0;
    const double GIG = 1000000000.0;

    //Skip for targets where no device tree value for bw is supplied
    if(sMaxBw <= 0.0) {
        return 0.0;
    }

    for (uint32_t i = 0; i < list->numHwLayers - 1; i++) {
        if(!mCurrentFrame.isFBComposed[i]) {
            hwc_layer_1_t* layer = &list->hwLayers[i];
            private_handle_t *hnd = (private_handle_t *)layer->handle;
            if (hnd) {
                hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);
                hwc_rect_t dst = layer->displayFrame;
                float bpp = ((float)hnd->size) / (hnd->width * hnd->height);
                size += (bpp * (crop.right - crop.left) *
                        (crop.bottom - crop.top) *
                        ctx->dpyAttr[mDpy].yres / (dst.bottom - dst.top)) /
                        GIG;
            }
        }
    }

    if(mCurrentFrame.fbCount) {
        hwc_layer_1_t* layer = &list->hwLayers[list->numHwLayers - 1];
        int tempw, temph;
        size += (getBufferSizeAndDimensions(
                    layer->displayFrame.right - layer->displayFrame.left,
                    layer->displayFrame.bottom - layer->displayFrame.top,
                    HAL_PIXEL_FORMAT_RGBA_8888,
                    tempw, temph)) / GIG;
    }

    return size;
}

bool MDPComp::bandwidthCheck(hwc_context_t *ctx, const double& size) {
    //Skip for targets where no device tree value for bw is supplied
    if(sMaxBw <= 0.0) {
        return true;
    }

    double panelRefRate =
            1000000000.0 / ctx->dpyAttr[mDpy].vsync_period;
    if((size * panelRefRate) > (sMaxBw - sBwClaimed)) {
        return false;
    }
    return true;
}

bool MDPComp::hwLimitationsCheck(hwc_context_t* ctx,
        hwc_display_contents_1_t* list) {

    //A-family hw limitation:
    //If a layer need alpha scaling, MDP can not support.
    if(ctx->mMDP.version < qdutils::MDSS_V5) {
        for(int i = 0; i < mCurrentFrame.layerCount; ++i) {
            if(!mCurrentFrame.isFBComposed[i] &&
                    isAlphaScaled( &list->hwLayers[i])) {
                ALOGD_IF(isDebug(), "%s:frame needs alphaScaling",__FUNCTION__);
                return false;
            }
        }
    }

    // On 8x26 & 8974 hw, we have a limitation of downscaling+blending.
    //If multiple layers requires downscaling and also they are overlapping
    //fall back to GPU since MDSS can not handle it.
    if(qdutils::MDPVersion::getInstance().is8x74v2() ||
            qdutils::MDPVersion::getInstance().is8x26()) {
        for(int i = 0; i < mCurrentFrame.layerCount-1; ++i) {
            hwc_layer_1_t* botLayer = &list->hwLayers[i];
            if(!mCurrentFrame.isFBComposed[i] &&
                    isDownscaleRequired(botLayer)) {
                //if layer-i is marked for MDP and needs downscaling
                //check if any MDP layer on top of i & overlaps with layer-i
                for(int j = i+1; j < mCurrentFrame.layerCount; ++j) {
                    hwc_layer_1_t* topLayer = &list->hwLayers[j];
                    if(!mCurrentFrame.isFBComposed[j] &&
                            isDownscaleRequired(topLayer)) {
                        hwc_rect_t r = getIntersection(botLayer->displayFrame,
                                topLayer->displayFrame);
                        if(isValidRect(r))
                            return false;
                    }
                }
            }
        }
    }
    return true;
}

int MDPComp::prepare(hwc_context_t *ctx, hwc_display_contents_1_t* list) {
    int ret = 0;
    const int numLayers = ctx->listStats[mDpy].numAppLayers;
    MDPVersion& mdpVersion = qdutils::MDPVersion::getInstance();

    //reset old data
    mCurrentFrame.reset(numLayers);
    memset(&mCurrentFrame.drop, 0, sizeof(mCurrentFrame.drop));
    mCurrentFrame.dropCount = 0;

    //number of app layers exceeds MAX_NUM_APP_LAYERS fall back to GPU
    //do not cache the information for next draw cycle.
    if(numLayers > MAX_NUM_APP_LAYERS) {
        mCachedFrame.updateCounts(mCurrentFrame);
        ALOGI("%s: Number of App layers exceeded the limit ",
        __FUNCTION__);
        ret = -1;
        return ret;
    }

    //Hard conditions, if not met, cannot do MDP comp
    if(!isFrameDoable(ctx)) {
        ALOGD_IF( isDebug(),"%s: MDP Comp not possible for this frame",
                __FUNCTION__);
        reset(numLayers, list);
        ret = -1;
        goto exit;
    }

    generateROI(ctx, list);

    //Convert from kbps to gbps
    sMaxBw = mdpVersion.getHighBw() / 1000000.0;
    if (ctx->mExtDisplay->isConnected() || ctx->mMDP.panel != MIPI_CMD_PANEL) {
        sMaxBw = mdpVersion.getLowBw() / 1000000.0;
    }

    //Check whether layers marked for MDP Composition is actually doable.
    if(isFullFrameDoable(ctx, list)) {
        mCurrentFrame.map();
        //Acquire and Program MDP pipes
        if(!programMDP(ctx, list)) {
            reset(numLayers, list);
            ctx->mOverlay->clear(mDpy);
            ctx->mLayerRotMap[mDpy]->clear();
            ret = -1;
            goto exit;
        } else { //Success
            //Any change in composition types needs an FB refresh
            mCurrentFrame.needsRedraw = false;
            if(!mCachedFrame.isSameFrame(mCurrentFrame, list) ||
                     (list->flags & HWC_GEOMETRY_CHANGED) ||
                     isSkipPresent(ctx, mDpy)) {
                mCurrentFrame.needsRedraw = true;
            }
        }
    } else if(isOnlyVideoDoable(ctx, list, false /*secure only*/) ||
            isOnlyVideoDoable(ctx, list, true /*secure only*/)) {
        //All layers marked for MDP comp cannot be bypassed.
        //Try to compose atleast YUV layers through MDP comp and let
        //all the RGB layers compose in FB
        //Destination over

        mCurrentFrame.fbZ = -1;
        if(mCurrentFrame.fbCount)
            mCurrentFrame.fbZ = mCurrentFrame.mdpCount;

        if(sEnable4k2kYUVSplit){
            adjustForSourceSplit(ctx, list);
        }

        mCurrentFrame.map();

        if(!programMDP(ctx, list)) {
            reset(numLayers, list);
            ctx->mOverlay->clear(mDpy);
            ctx->mLayerRotMap[mDpy]->clear();
            ret = -1;
            goto exit;
        }
    } else {
        reset(numLayers, list);
        memset(&mCurrentFrame.drop, 0, sizeof(mCurrentFrame.drop));
        mCurrentFrame.dropCount = 0;
        ret = -1;
        goto exit;
    }
    //UpdateLayerFlags
    setMDPCompLayerFlags(ctx, list);
    mCachedFrame.cacheAll(list);
    mCachedFrame.updateCounts(mCurrentFrame);

    // unlock it before calling dump function to avoid deadlock
    if(isDebug()) {
        ALOGD("GEOMETRY change: %d", (list->flags & HWC_GEOMETRY_CHANGED));
        android::String8 sDump("");
        dump(sDump);
        ALOGE("%s",sDump.string());
    }

exit:
    double panelRefRate =
            1000000000.0 / ctx->dpyAttr[mDpy].vsync_period;
    sBwClaimed += calcMDPBytesRead(ctx, list) * panelRefRate;
    return ret;
}

bool MDPComp::allocSplitVGPipesfor4k2k(hwc_context_t *ctx,
        hwc_display_contents_1_t* list, int index) {

    bool bRet = true;
    hwc_layer_1_t* layer = &list->hwLayers[index];
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    int mdpIndex = mCurrentFrame.layerToMDP[index];
    PipeLayerPair& info = mCurrentFrame.mdpToLayer[mdpIndex];
    info.pipeInfo = new MdpYUVPipeInfo;
    info.rot = NULL;
    MdpYUVPipeInfo& pipe_info = *(MdpYUVPipeInfo*)info.pipeInfo;
    ePipeType type =  MDPCOMP_OV_VG;

    pipe_info.lIndex = ovutils::OV_INVALID;
    pipe_info.rIndex = ovutils::OV_INVALID;

    pipe_info.lIndex = getMdpPipe(ctx, type, Overlay::MIXER_DEFAULT);
    if(pipe_info.lIndex == ovutils::OV_INVALID){
        bRet = false;
        ALOGD_IF(isDebug(),"%s: allocating first VG pipe failed",
                __FUNCTION__);
    }
    pipe_info.rIndex = getMdpPipe(ctx, type, Overlay::MIXER_DEFAULT);
    if(pipe_info.rIndex == ovutils::OV_INVALID){
        bRet = false;
        ALOGD_IF(isDebug(),"%s: allocating second VG pipe failed",
                __FUNCTION__);
    }
    return bRet;
}
//=============MDPCompNonSplit===================================================

void MDPCompNonSplit::adjustForSourceSplit(hwc_context_t *ctx,
         hwc_display_contents_1_t* list){
    //As we split 4kx2k yuv layer and program to 2 VG pipes
    //(if available) increase mdpcount accordingly
    mCurrentFrame.mdpCount += ctx->listStats[mDpy].yuv4k2kCount;

    //If 4k2k Yuv layer split is possible,  and if
    //fbz is above 4k2k layer, increment fb zorder by 1
    //as we split 4k2k layer and increment zorder for right half
    //of the layer
    if(mCurrentFrame.fbZ >= 0) {
        int n4k2kYuvCount = ctx->listStats[mDpy].yuv4k2kCount;
        for(int index = 0; index < n4k2kYuvCount; index++){
            int n4k2kYuvIndex =
                    ctx->listStats[mDpy].yuv4k2kIndices[index];
            if(mCurrentFrame.fbZ > n4k2kYuvIndex){
                mCurrentFrame.fbZ += 1;
            }
        }
    }
}

/*
 * Configures pipe(s) for MDP composition
 */
int MDPCompNonSplit::configure(hwc_context_t *ctx, hwc_layer_1_t *layer,
                             PipeLayerPair& PipeLayerPair) {
    MdpPipeInfoNonSplit& mdp_info =
        *(static_cast<MdpPipeInfoNonSplit*>(PipeLayerPair.pipeInfo));
    eMdpFlags mdpFlags = OV_MDP_BACKEND_COMPOSITION;
    eZorder zOrder = static_cast<eZorder>(mdp_info.zOrder);
    eIsFg isFg = IS_FG_OFF;
    eDest dest = mdp_info.index;

    ALOGD_IF(isDebug(),"%s: configuring: layer: %p z_order: %d dest_pipe: %d",
             __FUNCTION__, layer, zOrder, dest);

    return configureNonSplit(ctx, layer, mDpy, mdpFlags, zOrder, isFg, dest,
                           &PipeLayerPair.rot);
}

bool MDPCompNonSplit::arePipesAvailable(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    overlay::Overlay& ov = *ctx->mOverlay;
    int numPipesNeeded = mCurrentFrame.mdpCount;
    int availPipes = ov.availablePipes(mDpy, Overlay::MIXER_DEFAULT);

    //Reserve pipe for FB
    if(mCurrentFrame.fbCount)
        availPipes -= 1;

    if(numPipesNeeded > availPipes) {
        ALOGD_IF(isDebug(), "%s: Insufficient pipes, dpy %d needed %d, avail %d",
                __FUNCTION__, mDpy, numPipesNeeded, availPipes);
        return false;
    }

    if(not areVGPipesAvailable(ctx, list)) {
        return false;
    }

    return true;
}

bool MDPCompNonSplit::areVGPipesAvailable(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    overlay::Overlay& ov = *ctx->mOverlay;
    int pipesNeeded = 0;
    for(int i = 0; i < mCurrentFrame.layerCount; ++i) {
        if(!mCurrentFrame.isFBComposed[i]) {
            hwc_layer_1_t* layer = &list->hwLayers[i];
            hwc_rect_t dst = layer->displayFrame;
            private_handle_t *hnd = (private_handle_t *)layer->handle;
            if(is4kx2kYuvBuffer(hnd) && sEnable4k2kYUVSplit){
                pipesNeeded = pipesNeeded + 2;
            }
            else if(isYuvBuffer(hnd)) {
                pipesNeeded++;
            }
        }
    }

    int availableVGPipes = ov.availablePipes(mDpy, ovutils::OV_MDP_PIPE_VG);
    if(pipesNeeded > availableVGPipes) {
        ALOGD_IF(isDebug(), "%s: Insufficient VG pipes for video layers"
                "dpy %d needed %d, avail %d",
                __FUNCTION__, mDpy, pipesNeeded, availableVGPipes);
        return false;
    }

    return true;
}

bool MDPCompNonSplit::allocLayerPipes(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    for(int index = 0; index < mCurrentFrame.layerCount; index++) {

        if(mCurrentFrame.isFBComposed[index]) continue;

        hwc_layer_1_t* layer = &list->hwLayers[index];
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(is4kx2kYuvBuffer(hnd) && sEnable4k2kYUVSplit){
            if(allocSplitVGPipesfor4k2k(ctx, list, index)){
                continue;
            }
        }

        int mdpIndex = mCurrentFrame.layerToMDP[index];
        PipeLayerPair& info = mCurrentFrame.mdpToLayer[mdpIndex];
        info.pipeInfo = new MdpPipeInfoNonSplit;
        info.rot = NULL;
        MdpPipeInfoNonSplit& pipe_info = *(MdpPipeInfoNonSplit*)info.pipeInfo;
        ePipeType type = MDPCOMP_OV_ANY;

        if(isYuvBuffer(hnd)) {
            type = MDPCOMP_OV_VG;
        } else if(!qhwc::needsScaling(layer)
            && Overlay::getDMAMode() != Overlay::DMA_BLOCK_MODE
            && ctx->mMDP.version >= qdutils::MDSS_V5) {
            type = MDPCOMP_OV_DMA;
        }

        pipe_info.index = getMdpPipe(ctx, type, Overlay::MIXER_DEFAULT);
        if(pipe_info.index == ovutils::OV_INVALID) {
            ALOGD_IF(isDebug(), "%s: Unable to get pipe type = %d",
                __FUNCTION__, (int) type);
            return false;
        }
    }
    return true;
}

int MDPCompNonSplit::configure4k2kYuv(hwc_context_t *ctx, hwc_layer_1_t *layer,
        PipeLayerPair& PipeLayerPair) {
    MdpYUVPipeInfo& mdp_info =
            *(static_cast<MdpYUVPipeInfo*>(PipeLayerPair.pipeInfo));
    eZorder zOrder = static_cast<eZorder>(mdp_info.zOrder);
    eIsFg isFg = IS_FG_OFF;
    eMdpFlags mdpFlagsL = OV_MDP_BACKEND_COMPOSITION;
    eDest lDest = mdp_info.lIndex;
    eDest rDest = mdp_info.rIndex;

    return configureSourceSplit(ctx, layer, mDpy, mdpFlagsL, zOrder, isFg,
            lDest, rDest, &PipeLayerPair.rot);
}

bool MDPCompNonSplit::draw(hwc_context_t *ctx, hwc_display_contents_1_t* list) {

    if(!isEnabled()) {
        ALOGD_IF(isDebug(),"%s: MDP Comp not configured", __FUNCTION__);
        return true;
    }

    if(!ctx || !list) {
        ALOGE("%s: invalid contxt or list",__FUNCTION__);
        return false;
    }

    if(ctx->listStats[mDpy].numAppLayers > MAX_NUM_APP_LAYERS) {
        ALOGD_IF(isDebug(),"%s: Exceeding max layer count", __FUNCTION__);
        return true;
    }

    /* reset Invalidator */
    if(idleInvalidator && !sIdleFallBack && mCurrentFrame.mdpCount)
        idleInvalidator->markForSleep();

    overlay::Overlay& ov = *ctx->mOverlay;
    LayerProp *layerProp = ctx->layerProp[mDpy];

    int numHwLayers = ctx->listStats[mDpy].numAppLayers;
    for(int i = 0; i < numHwLayers && mCurrentFrame.mdpCount; i++ )
    {
        if(mCurrentFrame.isFBComposed[i]) continue;

        hwc_layer_1_t *layer = &list->hwLayers[i];
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(!hnd) {
            if (!(layer->flags & HWC_COLOR_FILL)) {
                ALOGE("%s handle null", __FUNCTION__);
                return false;
            }
            // No PLAY for Color layer
            layerProp[i].mFlags &= ~HWC_MDPCOMP;
            continue;
        }

        int mdpIndex = mCurrentFrame.layerToMDP[i];

        if(is4kx2kYuvBuffer(hnd) && sEnable4k2kYUVSplit)
        {
            MdpYUVPipeInfo& pipe_info =
                *(MdpYUVPipeInfo*)mCurrentFrame.mdpToLayer[mdpIndex].pipeInfo;
            Rotator *rot = mCurrentFrame.mdpToLayer[mdpIndex].rot;
            ovutils::eDest indexL = pipe_info.lIndex;
            ovutils::eDest indexR = pipe_info.rIndex;
            int fd = hnd->fd;
            uint32_t offset = hnd->offset;
            if(rot) {
                rot->queueBuffer(fd, offset);
                fd = rot->getDstMemId();
                offset = rot->getDstOffset();
            }
            if(indexL != ovutils::OV_INVALID) {
                ovutils::eDest destL = (ovutils::eDest)indexL;
                ALOGD_IF(isDebug(),"%s: MDP Comp: Drawing layer: %p hnd: %p \
                        using  pipe: %d", __FUNCTION__, layer, hnd, indexL );
                if (!ov.queueBuffer(fd, offset, destL)) {
                    ALOGE("%s: queueBuffer failed for display:%d",
                            __FUNCTION__, mDpy);
                    return false;
                }
            }

            if(indexR != ovutils::OV_INVALID) {
                ovutils::eDest destR = (ovutils::eDest)indexR;
                ALOGD_IF(isDebug(),"%s: MDP Comp: Drawing layer: %p hnd: %p \
                        using  pipe: %d", __FUNCTION__, layer, hnd, indexR );
                if (!ov.queueBuffer(fd, offset, destR)) {
                    ALOGE("%s: queueBuffer failed for display:%d",
                            __FUNCTION__, mDpy);
                    return false;
                }
            }
        }
        else{
            MdpPipeInfoNonSplit& pipe_info =
            *(MdpPipeInfoNonSplit*)mCurrentFrame.mdpToLayer[mdpIndex].pipeInfo;
            ovutils::eDest dest = pipe_info.index;
            if(dest == ovutils::OV_INVALID) {
                ALOGE("%s: Invalid pipe index (%d)", __FUNCTION__, dest);
                return false;
            }

            if(!(layerProp[i].mFlags & HWC_MDPCOMP)) {
                continue;
            }

            ALOGD_IF(isDebug(),"%s: MDP Comp: Drawing layer: %p hnd: %p \
                    using  pipe: %d", __FUNCTION__, layer,
                    hnd, dest );

            int fd = hnd->fd;
            uint32_t offset = hnd->offset;

            Rotator *rot = mCurrentFrame.mdpToLayer[mdpIndex].rot;
            if(rot) {
                if(!rot->queueBuffer(fd, offset))
                    return false;
                fd = rot->getDstMemId();
                offset = rot->getDstOffset();
            }

            if (!ov.queueBuffer(fd, offset, dest)) {
                ALOGE("%s: queueBuffer failed for display:%d ",
                        __FUNCTION__, mDpy);
                return false;
            }
        }

        layerProp[i].mFlags &= ~HWC_MDPCOMP;
    }
    return true;
}

//=============MDPCompSplit===================================================

void MDPCompSplit::adjustForSourceSplit(hwc_context_t *ctx,
         hwc_display_contents_1_t* list){
    //if 4kx2k yuv layer is totally present in either in left half
    //or right half then try splitting the yuv layer to avoid decimation
    int n4k2kYuvCount = ctx->listStats[mDpy].yuv4k2kCount;
    const int lSplit = getLeftSplit(ctx, mDpy);
    for(int index = 0; index < n4k2kYuvCount; index++){
        int n4k2kYuvIndex = ctx->listStats[mDpy].yuv4k2kIndices[index];
        hwc_layer_1_t* layer = &list->hwLayers[n4k2kYuvIndex];
        hwc_rect_t dst = layer->displayFrame;
        if((dst.left > lSplit) || (dst.right < lSplit)) {
            mCurrentFrame.mdpCount += 1;
        }
        if(mCurrentFrame.fbZ > n4k2kYuvIndex){
            mCurrentFrame.fbZ += 1;
        }
    }
}

int MDPCompSplit::pipesNeeded(hwc_context_t *ctx,
        hwc_display_contents_1_t* list,
        int mixer) {
    int pipesNeeded = 0;
    const int xres = ctx->dpyAttr[mDpy].xres;

    const int lSplit = getLeftSplit(ctx, mDpy);

    for(int i = 0; i < mCurrentFrame.layerCount; ++i) {
        if(!mCurrentFrame.isFBComposed[i]) {
            hwc_layer_1_t* layer = &list->hwLayers[i];
            hwc_rect_t dst = layer->displayFrame;
            if(mixer == Overlay::MIXER_LEFT && dst.left < lSplit) {
                pipesNeeded++;
            } else if(mixer == Overlay::MIXER_RIGHT && dst.right > lSplit) {
                pipesNeeded++;
            }
        }
    }
    return pipesNeeded;
}

bool MDPCompSplit::arePipesAvailable(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    overlay::Overlay& ov = *ctx->mOverlay;
    int totalPipesNeeded = 0;

    for(int i = 0; i < Overlay::MIXER_MAX; i++) {
        int numPipesNeeded = pipesNeeded(ctx, list, i);
        int availPipes = ov.availablePipes(mDpy, i);

        //Reserve pipe(s)for FB
        if(mCurrentFrame.fbCount)
            numPipesNeeded += 1;

        totalPipesNeeded += numPipesNeeded;

        //Per mixer check.
        if(numPipesNeeded > availPipes) {
            ALOGD_IF(isDebug(), "%s: Insufficient pipes for "
                     "dpy %d mixer %d needed %d, avail %d",
                     __FUNCTION__, mDpy, i, numPipesNeeded, availPipes);
            return false;
        }
    }

    //Per display check, since unused pipes can get counted twice.
    int totalPipesAvailable = ov.availablePipes(mDpy);
    if(totalPipesNeeded > totalPipesAvailable) {
        ALOGD_IF(isDebug(), "%s: Insufficient pipes for "
                "dpy %d needed %d, avail %d",
                __FUNCTION__, mDpy, totalPipesNeeded, totalPipesAvailable);
        return false;
    }

    if(not areVGPipesAvailable(ctx, list)) {
        return false;
    }

    return true;
}

bool MDPCompSplit::areVGPipesAvailable(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    overlay::Overlay& ov = *ctx->mOverlay;
    int pipesNeeded = 0;
    const int lSplit = getLeftSplit(ctx, mDpy);
    for(int i = 0; i < mCurrentFrame.layerCount; ++i) {
        if(!mCurrentFrame.isFBComposed[i]) {
            hwc_layer_1_t* layer = &list->hwLayers[i];
            hwc_rect_t dst = layer->displayFrame;
            private_handle_t *hnd = (private_handle_t *)layer->handle;
            if(is4kx2kYuvBuffer(hnd) && sEnable4k2kYUVSplit){
                if((dst.left > lSplit)||(dst.right < lSplit)){
                    pipesNeeded = pipesNeeded + 2;
                    continue;
                }
            }
            if(isYuvBuffer(hnd)) {
                if(dst.left < lSplit) {
                    pipesNeeded++;
                }
                if(dst.right > lSplit) {
                    pipesNeeded++;
                }
            }
        }
    }

    int availableVGPipes = ov.availablePipes(mDpy, ovutils::OV_MDP_PIPE_VG);
    if(pipesNeeded > availableVGPipes) {
        ALOGD_IF(isDebug(), "%s: Insufficient VG pipes for video layers"
                "dpy %d needed %d, avail %d",
                __FUNCTION__, mDpy, pipesNeeded, availableVGPipes);
        return false;
    }

    return true;
}

bool MDPCompSplit::acquireMDPPipes(hwc_context_t *ctx, hwc_layer_1_t* layer,
        MdpPipeInfoSplit& pipe_info,
        ePipeType type) {
    const int xres = ctx->dpyAttr[mDpy].xres;
    const int lSplit = getLeftSplit(ctx, mDpy);

    hwc_rect_t dst = layer->displayFrame;
    pipe_info.lIndex = ovutils::OV_INVALID;
    pipe_info.rIndex = ovutils::OV_INVALID;

    if (dst.left < lSplit) {
        pipe_info.lIndex = getMdpPipe(ctx, type, Overlay::MIXER_LEFT);
        if(pipe_info.lIndex == ovutils::OV_INVALID)
            return false;
    }

    if(dst.right > lSplit) {
        pipe_info.rIndex = getMdpPipe(ctx, type, Overlay::MIXER_RIGHT);
        if(pipe_info.rIndex == ovutils::OV_INVALID)
            return false;
    }

    return true;
}

bool MDPCompSplit::allocLayerPipes(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    for(int index = 0 ; index < mCurrentFrame.layerCount; index++) {

        if(mCurrentFrame.isFBComposed[index]) continue;

        hwc_layer_1_t* layer = &list->hwLayers[index];
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        hwc_rect_t dst = layer->displayFrame;
        const int lSplit = getLeftSplit(ctx, mDpy);
        if(is4kx2kYuvBuffer(hnd) && sEnable4k2kYUVSplit){
            if((dst.left > lSplit)||(dst.right < lSplit)){
                if(allocSplitVGPipesfor4k2k(ctx, list, index)){
                    continue;
                }
            }
        }
        int mdpIndex = mCurrentFrame.layerToMDP[index];
        PipeLayerPair& info = mCurrentFrame.mdpToLayer[mdpIndex];
        info.pipeInfo = new MdpPipeInfoSplit;
        info.rot = NULL;
        MdpPipeInfoSplit& pipe_info = *(MdpPipeInfoSplit*)info.pipeInfo;
        ePipeType type = MDPCOMP_OV_ANY;

        if(isYuvBuffer(hnd)) {
            type = MDPCOMP_OV_VG;
        } else if(!qhwc::needsScalingWithSplit(ctx, layer, mDpy)
            && Overlay::getDMAMode() != Overlay::DMA_BLOCK_MODE
            && ctx->mMDP.version >= qdutils::MDSS_V5) {
            type = MDPCOMP_OV_DMA;
        }

        if(!acquireMDPPipes(ctx, layer, pipe_info, type)) {
            ALOGD_IF(isDebug(), "%s: Unable to get pipe for type = %d",
                    __FUNCTION__, (int) type);
            return false;
        }
    }
    return true;
}

int MDPCompSplit::configure4k2kYuv(hwc_context_t *ctx, hwc_layer_1_t *layer,
        PipeLayerPair& PipeLayerPair) {
    const int lSplit = getLeftSplit(ctx, mDpy);
    hwc_rect_t dst = layer->displayFrame;
    if((dst.left > lSplit)||(dst.right < lSplit)){
        MdpYUVPipeInfo& mdp_info =
                *(static_cast<MdpYUVPipeInfo*>(PipeLayerPair.pipeInfo));
        eZorder zOrder = static_cast<eZorder>(mdp_info.zOrder);
        eIsFg isFg = IS_FG_OFF;
        eMdpFlags mdpFlagsL = OV_MDP_BACKEND_COMPOSITION;
        eDest lDest = mdp_info.lIndex;
        eDest rDest = mdp_info.rIndex;

        return configureSourceSplit(ctx, layer, mDpy, mdpFlagsL, zOrder, isFg,
                lDest, rDest, &PipeLayerPair.rot);
    }
    else{
        return configure(ctx, layer, PipeLayerPair);
    }
}

/*
 * Configures pipe(s) for MDP composition
 */
int MDPCompSplit::configure(hwc_context_t *ctx, hwc_layer_1_t *layer,
        PipeLayerPair& PipeLayerPair) {
    MdpPipeInfoSplit& mdp_info =
        *(static_cast<MdpPipeInfoSplit*>(PipeLayerPair.pipeInfo));
    eZorder zOrder = static_cast<eZorder>(mdp_info.zOrder);
    eIsFg isFg = IS_FG_OFF;
    eMdpFlags mdpFlagsL = OV_MDP_BACKEND_COMPOSITION;
    eDest lDest = mdp_info.lIndex;
    eDest rDest = mdp_info.rIndex;

    ALOGD_IF(isDebug(),"%s: configuring: layer: %p z_order: %d dest_pipeL: %d"
             "dest_pipeR: %d",__FUNCTION__, layer, zOrder, lDest, rDest);

    return configureSplit(ctx, layer, mDpy, mdpFlagsL, zOrder, isFg, lDest,
                            rDest, &PipeLayerPair.rot);
}

bool MDPCompSplit::draw(hwc_context_t *ctx, hwc_display_contents_1_t* list) {

    if(!isEnabled()) {
        ALOGD_IF(isDebug(),"%s: MDP Comp not configured", __FUNCTION__);
        return true;
    }

    if(!ctx || !list) {
        ALOGE("%s: invalid contxt or list",__FUNCTION__);
        return false;
    }

    if(ctx->listStats[mDpy].numAppLayers > MAX_NUM_APP_LAYERS) {
        ALOGD_IF(isDebug(),"%s: Exceeding max layer count", __FUNCTION__);
        return true;
    }

    /* reset Invalidator */
    if(idleInvalidator && !sIdleFallBack && mCurrentFrame.mdpCount)
        idleInvalidator->markForSleep();

    overlay::Overlay& ov = *ctx->mOverlay;
    LayerProp *layerProp = ctx->layerProp[mDpy];

    int numHwLayers = ctx->listStats[mDpy].numAppLayers;
    for(int i = 0; i < numHwLayers && mCurrentFrame.mdpCount; i++ )
    {
        if(mCurrentFrame.isFBComposed[i]) continue;

        hwc_layer_1_t *layer = &list->hwLayers[i];
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(!hnd) {
            ALOGE("%s handle null", __FUNCTION__);
            return false;
        }

        if(!(layerProp[i].mFlags & HWC_MDPCOMP)) {
            continue;
        }

        int mdpIndex = mCurrentFrame.layerToMDP[i];

        if(is4kx2kYuvBuffer(hnd) && sEnable4k2kYUVSplit)
        {
            MdpYUVPipeInfo& pipe_info =
                *(MdpYUVPipeInfo*)mCurrentFrame.mdpToLayer[mdpIndex].pipeInfo;
            Rotator *rot = mCurrentFrame.mdpToLayer[mdpIndex].rot;
            ovutils::eDest indexL = pipe_info.lIndex;
            ovutils::eDest indexR = pipe_info.rIndex;
            int fd = hnd->fd;
            uint32_t offset = hnd->offset;
            if(rot) {
                rot->queueBuffer(fd, offset);
                fd = rot->getDstMemId();
                offset = rot->getDstOffset();
            }
            if(indexL != ovutils::OV_INVALID) {
                ovutils::eDest destL = (ovutils::eDest)indexL;
                ALOGD_IF(isDebug(),"%s: MDP Comp: Drawing layer: %p hnd: %p \
                        using  pipe: %d", __FUNCTION__, layer, hnd, indexL );
                if (!ov.queueBuffer(fd, offset, destL)) {
                    ALOGE("%s: queueBuffer failed for display:%d",
                            __FUNCTION__, mDpy);
                    return false;
                }
            }

            if(indexR != ovutils::OV_INVALID) {
                ovutils::eDest destR = (ovutils::eDest)indexR;
                ALOGD_IF(isDebug(),"%s: MDP Comp: Drawing layer: %p hnd: %p \
                        using  pipe: %d", __FUNCTION__, layer, hnd, indexR );
                if (!ov.queueBuffer(fd, offset, destR)) {
                    ALOGE("%s: queueBuffer failed for display:%d",
                            __FUNCTION__, mDpy);
                    return false;
                }
            }
        }
        else{
            MdpPipeInfoSplit& pipe_info =
                *(MdpPipeInfoSplit*)mCurrentFrame.mdpToLayer[mdpIndex].pipeInfo;
            Rotator *rot = mCurrentFrame.mdpToLayer[mdpIndex].rot;

            ovutils::eDest indexL = pipe_info.lIndex;
            ovutils::eDest indexR = pipe_info.rIndex;

            int fd = hnd->fd;
            int offset = hnd->offset;

            if(ctx->mAD->isModeOn()) {
                if(ctx->mAD->draw(ctx, fd, offset)) {
                    fd = ctx->mAD->getDstFd(ctx);
                    offset = ctx->mAD->getDstOffset(ctx);
                }
            }

            if(rot) {
                rot->queueBuffer(fd, offset);
                fd = rot->getDstMemId();
                offset = rot->getDstOffset();
            }

            //************* play left mixer **********
            if(indexL != ovutils::OV_INVALID) {
                ovutils::eDest destL = (ovutils::eDest)indexL;
                ALOGD_IF(isDebug(),"%s: MDP Comp: Drawing layer: %p hnd: %p \
                        using  pipe: %d", __FUNCTION__, layer, hnd, indexL );
                if (!ov.queueBuffer(fd, offset, destL)) {
                    ALOGE("%s: queueBuffer failed for left mixer",
                            __FUNCTION__);
                    return false;
                }
            }

            //************* play right mixer **********
            if(indexR != ovutils::OV_INVALID) {
                ovutils::eDest destR = (ovutils::eDest)indexR;
                ALOGD_IF(isDebug(),"%s: MDP Comp: Drawing layer: %p hnd: %p \
                        using  pipe: %d", __FUNCTION__, layer, hnd, indexR );
                if (!ov.queueBuffer(fd, offset, destR)) {
                    ALOGE("%s: queueBuffer failed for right mixer",
                            __FUNCTION__);
                    return false;
                }
            }
        }

        layerProp[i].mFlags &= ~HWC_MDPCOMP;
    }

    return true;
}
}; //namespace

