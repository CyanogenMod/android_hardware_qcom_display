/*
 * Copyright (C) 2012-2014, The Linux Foundation. All rights reserved.
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
bool MDPComp::sHandleTimeout = false;
bool MDPComp::sDebugLogs = false;
bool MDPComp::sEnabled = false;
bool MDPComp::sEnableMixedMode = true;
bool MDPComp::sEnablePartialFrameUpdate = false;
int MDPComp::sMaxPipesPerMixer = MAX_PIPES_PER_MIXER;
bool MDPComp::sEnable4k2kYUVSplit = false;
bool MDPComp::sSrcSplitEnabled = false;
MDPComp* MDPComp::getObject(hwc_context_t *ctx, const int& dpy) {

    if(isDisplaySplit(ctx, dpy)) {
        if(qdutils::MDPVersion::getInstance().isSrcSplit()) {
            sSrcSplitEnabled = true;
            return new MDPCompSrcSplit(dpy);
        }
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

    if(property_get("persist.hwc.partialupdate", property, NULL) > 0) {
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

void MDPComp::reset(hwc_context_t *ctx) {
    const int numLayers = ctx->listStats[mDpy].numAppLayers;
    mCurrentFrame.reset(numLayers);
    ctx->mOverlay->clear(mDpy);
    ctx->mLayerRotMap[mDpy]->clear();
}

void MDPComp::timeout_handler(void *udata) {
    struct hwc_context_t* ctx = (struct hwc_context_t*)(udata);

    if(!ctx) {
        ALOGE("%s: received empty data in timer callback", __FUNCTION__);
        return;
    }
    Locker::Autolock _l(ctx->mDrawLock);
    // Handle timeout event only if the previous composition is MDP or MIXED.
    if(!sHandleTimeout) {
        ALOGD_IF(isDebug(), "%s:Do not handle this timeout", __FUNCTION__);
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

void MDPComp::setRedraw(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    mCurrentFrame.needsRedraw = false;
    if(!mCachedFrame.isSameFrame(mCurrentFrame, list) ||
            (list->flags & HWC_GEOMETRY_CHANGED) ||
            isSkipPresent(ctx, mDpy)) {
        mCurrentFrame.needsRedraw = true;
    }
}

MDPComp::FrameInfo::FrameInfo() {
    memset(&mdpToLayer, 0, sizeof(mdpToLayer));
    reset(0);
}

void MDPComp::FrameInfo::reset(const int& numLayers) {
    for(int i = 0 ; i < MAX_PIPES_PER_MIXER; i++ ) {
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
    fbZ = -1;
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

    hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);
    hwc_rect_t dst = layer->displayFrame;
    int crop_w = crop.right - crop.left;
    int crop_h = crop.bottom - crop.top;
    int dst_w = dst.right - dst.left;
    int dst_h = dst.bottom - dst.top;
    float w_scale = ((float)crop_w / (float)dst_w);
    float h_scale = ((float)crop_h / (float)dst_h);

    /* Workaround for MDP HW limitation in DSI command mode panels where
     * FPS will not go beyond 30 if buffers on RGB pipes are of width or height
     * less than 5 pixels
     * There also is a HW limilation in MDP, minimum block size is 2x2
     * Fallback to GPU if height is less than 2.
     */
    if((crop_w < 5)||(crop_h < 5))
        return false;

    if((w_scale > 1.0f) || (h_scale > 1.0f)) {
        const uint32_t maxMDPDownscale =
            qdutils::MDPVersion::getInstance().getMaxMDPDownscale();
        const float w_dscale = w_scale;
        const float h_dscale = h_scale;

        if(ctx->mMDP.version >= qdutils::MDSS_V5) {

            if(!qdutils::MDPVersion::getInstance().supportsDecimation()) {
                /* On targets that doesnt support Decimation (eg.,8x26)
                 * maximum downscale support is overlay pipe downscale.
                 */
                if(crop_w > MAX_DISPLAY_DIM || w_dscale > maxMDPDownscale ||
                        h_dscale > maxMDPDownscale)
                    return false;
            } else {
                // Decimation on macrotile format layers is not supported.
                if(isTileRendered(hnd)) {
                    /* MDP can read maximum MAX_DISPLAY_DIM width.
                     * Bail out if
                     *      1. Src crop > MAX_DISPLAY_DIM on nonsplit MDPComp
                     *      2. exceeds maximum downscale limit
                     */
                    if(((crop_w > MAX_DISPLAY_DIM) && !sSrcSplitEnabled) ||
                            w_dscale > maxMDPDownscale ||
                            h_dscale > maxMDPDownscale) {
                        return false;
                    }
                } else if(w_dscale > 64 || h_dscale > 64)
                    return false;
            }
        } else { //A-family
            if(w_dscale > maxMDPDownscale || h_dscale > maxMDPDownscale)
                return false;
        }
    }

    if((w_scale < 1.0f) || (h_scale < 1.0f)) {
        const uint32_t upscale =
            qdutils::MDPVersion::getInstance().getMaxMDPUpscale();
        const float w_uscale = 1.0f / w_scale;
        const float h_uscale = 1.0f / h_scale;

        if(w_uscale > upscale || h_uscale > upscale)
            return false;
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

    if(!isEnabled()) {
        ALOGD_IF(isDebug(),"%s: MDP Comp. not enabled.", __FUNCTION__);
        ret = false;
    } else if(qdutils::MDPVersion::getInstance().is8x26() &&
            ctx->mVideoTransFlag &&
            isSecondaryConnected(ctx)) {
        //1 Padding round to shift pipes across mixers
        ALOGD_IF(isDebug(),"%s: MDP Comp. video transition padding round",
                __FUNCTION__);
        ret = false;
    } else if(isSecondaryConfiguring(ctx)) {
        ALOGD_IF( isDebug(),"%s: External Display connection is pending",
                  __FUNCTION__);
        ret = false;
    } else if(ctx->isPaddingRound) {
        ALOGD_IF(isDebug(), "%s: padding round invoked for dpy %d",
                 __FUNCTION__,mDpy);
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
            continue;
        }

        const hwc_layer_1_t* layer =  &list->hwLayers[i];

        hwc_rect_t dstRect = layer->displayFrame;
        hwc_rect_t srcRect = integerizeSourceCrop(layer->sourceCropf);

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
            if((res_w != dst_w || res_h != dst_h) && needsScaling (layer)) {
                ALOGI("%s: Resetting ROI due to scaling", __FUNCTION__);
                memset(&mCurrentFrame.drop, 0, sizeof(mCurrentFrame.drop));
                mCurrentFrame.dropCount = 0;
                return false;
            }

            /* deduct any opaque region from visibleRect */
            if (layer->blending == HWC_BLENDING_NONE)
                visibleRect = deductRect(visibleRect, res);
        }
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
bool MDPComp::tryFullFrame(hwc_context_t *ctx,
                                hwc_display_contents_1_t* list){

    const int numAppLayers = ctx->listStats[mDpy].numAppLayers;
    int priDispW = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;

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

    if(mDpy > HWC_DISPLAY_PRIMARY && (priDispW > MAX_DISPLAY_DIM) &&
                              (ctx->dpyAttr[mDpy].xres < MAX_DISPLAY_DIM)) {
        // Disable MDP comp on Secondary when the primary is highres panel and
        // the secondary is a normal 1080p, because, MDP comp on secondary under
        // in such usecase, decimation gets used for downscale and there will be
        // a quality mismatch when there will be a fallback to GPU comp
        ALOGD_IF(isDebug(), "%s: Disable MDP Compositon for Secondary Disp",
              __FUNCTION__);
        return false;
    }

    // check for action safe flag and downscale mode which requires scaling.
    if(ctx->dpyAttr[mDpy].mActionSafePresent
            || ctx->dpyAttr[mDpy].mDownScaleMode) {
        ALOGD_IF(isDebug(), "%s: Scaling needed for this frame",__FUNCTION__);
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
    memcpy(&mCurrentFrame.isFBComposed, &mCurrentFrame.drop,
           sizeof(mCurrentFrame.isFBComposed));
    mCurrentFrame.mdpCount = mCurrentFrame.layerCount - mCurrentFrame.fbCount -
        mCurrentFrame.dropCount;

    if(sEnable4k2kYUVSplit){
        adjustForSourceSplit(ctx, list);
    }

    if(!postHeuristicsHandling(ctx, list)) {
        ALOGD_IF(isDebug(), "post heuristic handling failed");
        reset(ctx);
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
        ret =   loadBasedComp(ctx, list) or
                cacheBasedComp(ctx, list);
    } else {
        ret =   cacheBasedComp(ctx, list) or
                loadBasedComp(ctx, list);
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
                reset(ctx);
                return false;
            }
        }
    }

    updateYUV(ctx, list, false /*secure only*/);
    bool ret = markLayersForCaching(ctx, list); //sets up fbZ also
    if(!ret) {
        ALOGD_IF(isDebug(),"%s: batching failed, dpy %d",__FUNCTION__, mDpy);
        reset(ctx);
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
        reset(ctx);
        return false;
    }

    if(!postHeuristicsHandling(ctx, list)) {
        ALOGD_IF(isDebug(), "post heuristic handling failed");
        reset(ctx);
        return false;
    }

    return true;
}

bool MDPComp::loadBasedComp(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    if(not isLoadBasedCompDoable(ctx)) {
        return false;
    }

    const int numAppLayers = ctx->listStats[mDpy].numAppLayers;
    const int numNonDroppedLayers = numAppLayers - mCurrentFrame.dropCount;
    const int stagesForMDP = min(sMaxPipesPerMixer,
            ctx->mOverlay->availablePipes(mDpy, Overlay::MIXER_DEFAULT));

    int mdpBatchSize = stagesForMDP - 1; //1 stage for FB
    int fbBatchSize = numNonDroppedLayers - mdpBatchSize;
    int lastMDPSupportedIndex = numAppLayers;
    int dropCount = 0;

    //Find the minimum MDP batch size
    for(int i = 0; i < numAppLayers;i++) {
        if(mCurrentFrame.drop[i]) {
            dropCount++;
            continue;
        }
        hwc_layer_1_t* layer = &list->hwLayers[i];
        if(not isSupportedForMDPComp(ctx, layer)) {
            lastMDPSupportedIndex = i;
            mdpBatchSize = min(i - dropCount, stagesForMDP - 1);
            fbBatchSize = numNonDroppedLayers - mdpBatchSize;
            break;
        }
    }

    ALOGD_IF(isDebug(), "%s:Before optimizing fbBatch, mdpbatch %d, fbbatch %d "
            "dropped %d", __FUNCTION__, mdpBatchSize, fbBatchSize,
            mCurrentFrame.dropCount);

    //Start at a point where the fb batch should at least have 2 layers, for
    //this mode to be justified.
    while(fbBatchSize < 2) {
        ++fbBatchSize;
        --mdpBatchSize;
    }

    //If there are no layers for MDP, this mode doesnt make sense.
    if(mdpBatchSize < 1) {
        ALOGD_IF(isDebug(), "%s: No MDP layers after optimizing for fbBatch",
                __FUNCTION__);
        return false;
    }

    mCurrentFrame.reset(numAppLayers);

    //Try with successively smaller mdp batch sizes until we succeed or reach 1
    while(mdpBatchSize > 0) {
        //Mark layers for MDP comp
        int mdpBatchLeft = mdpBatchSize;
        for(int i = 0; i < lastMDPSupportedIndex and mdpBatchLeft; i++) {
            if(mCurrentFrame.drop[i]) {
                continue;
            }
            mCurrentFrame.isFBComposed[i] = false;
            --mdpBatchLeft;
        }

        mCurrentFrame.fbZ = mdpBatchSize;
        mCurrentFrame.fbCount = fbBatchSize;
        mCurrentFrame.mdpCount = mdpBatchSize;

        ALOGD_IF(isDebug(), "%s:Trying with: mdpbatch %d fbbatch %d dropped %d",
                __FUNCTION__, mdpBatchSize, fbBatchSize,
                mCurrentFrame.dropCount);

        if(postHeuristicsHandling(ctx, list)) {
            ALOGD_IF(isDebug(), "%s: Postheuristics handling succeeded",
                    __FUNCTION__);
            return true;
        }

        reset(ctx);
        --mdpBatchSize;
        ++fbBatchSize;
    }

    return false;
}

bool MDPComp::isLoadBasedCompDoable(hwc_context_t *ctx) {
    if(mDpy or isSecurePresent(ctx, mDpy) or
            isYuvPresent(ctx, mDpy)) {
        return false;
    }
    return true;
}

bool MDPComp::tryVideoOnly(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    const bool secureOnly = true;
    return videoOnlyComp(ctx, list, not secureOnly) or
            videoOnlyComp(ctx, list, secureOnly);
}

bool MDPComp::videoOnlyComp(hwc_context_t *ctx,
        hwc_display_contents_1_t* list, bool secureOnly) {
    int numAppLayers = ctx->listStats[mDpy].numAppLayers;

    mCurrentFrame.reset(numAppLayers);
    updateYUV(ctx, list, secureOnly);
    int mdpCount = mCurrentFrame.mdpCount;

    if(!isYuvPresent(ctx, mDpy) or (mdpCount == 0)) {
        reset(ctx);
        return false;
    }

    /* Bail out if we are processing only secured video layers
     * and we dont have any */
    if(!isSecurePresent(ctx, mDpy) && secureOnly){
        reset(ctx);
        return false;
    }

    if(mCurrentFrame.fbCount)
        mCurrentFrame.fbZ = mCurrentFrame.mdpCount;

    if(sEnable4k2kYUVSplit){
        adjustForSourceSplit(ctx, list);
    }

    if(!postHeuristicsHandling(ctx, list)) {
        ALOGD_IF(isDebug(), "post heuristic handling failed");
        reset(ctx);
        return false;
    }

    return true;
}

/* Checks for conditions where YUV layers cannot be bypassed */
bool MDPComp::isYUVDoable(hwc_context_t* ctx, hwc_layer_1_t* layer) {
    if(isSkipLayer(layer)) {
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
    int droppedLayerCt = 0;
    while (i < mCurrentFrame.layerCount) {
        int batchCount = 0;
        int batchStart = i;
        int batchEnd = i;
        /* Adjust batch Z order with the dropped layers so far */
        int fbZ = batchStart - droppedLayerCt;
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
                    droppedLayerCt++;
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

    /* Nothing is cached. No batching needed */
    if(mCurrentFrame.fbCount == 0) {
        return true;
    }

    /* No MDP comp layers, try to use other comp modes */
    if(mCurrentFrame.mdpCount == 0) {
        return false;
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

hwc_rect_t MDPComp::getUpdatingFBRect(hwc_context_t *ctx,
        hwc_display_contents_1_t* list){
    hwc_rect_t fbRect = (struct hwc_rect){0, 0, 0, 0};
    hwc_layer_1_t *fbLayer = &list->hwLayers[mCurrentFrame.layerCount];

    /* Update only the region of FB needed for composition */
    for(int i = 0; i < mCurrentFrame.layerCount; i++ ) {
        if(mCurrentFrame.isFBComposed[i] && !mCurrentFrame.drop[i]) {
            hwc_layer_1_t* layer = &list->hwLayers[i];
            hwc_rect_t dst = layer->displayFrame;
            fbRect = getUnion(fbRect, dst);
        }
    }
    return fbRect;
}

bool MDPComp::postHeuristicsHandling(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {

    //Capability checks
    if(!resourceCheck(ctx, list)) {
        ALOGD_IF(isDebug(), "%s: resource check failed", __FUNCTION__);
        return false;
    }

    //Limitations checks
    if(!hwLimitationsCheck(ctx, list)) {
        ALOGD_IF(isDebug(), "%s: HW limitations",__FUNCTION__);
        return false;
    }

    //Configure framebuffer first if applicable
    if(mCurrentFrame.fbZ >= 0) {
        hwc_rect_t fbRect = getUpdatingFBRect(ctx, list);
        if(!ctx->mFBUpdate[mDpy]->prepare(ctx, list, fbRect, mCurrentFrame.fbZ))
        {
            ALOGD_IF(isDebug(), "%s configure framebuffer failed",
                    __FUNCTION__);
            return false;
        }
    }

    mCurrentFrame.map();

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

    if(!ctx->mOverlay->validateAndSet(mDpy, ctx->dpyAttr[mDpy].fd)) {
        ALOGD_IF(isDebug(), "%s: Failed to validate and set overlay for dpy %d"
                ,__FUNCTION__, mDpy);
        return false;
    }

    setRedraw(ctx, list);
    return true;
}

bool MDPComp::resourceCheck(hwc_context_t *ctx,
        hwc_display_contents_1_t *list) {
    const bool fbUsed = mCurrentFrame.fbCount;
    if(mCurrentFrame.mdpCount > sMaxPipesPerMixer - fbUsed) {
        ALOGD_IF(isDebug(), "%s: Exceeds MAX_PIPES_PER_MIXER",__FUNCTION__);
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

    //Do not cache the information for next draw cycle.
    if(numLayers > MAX_NUM_APP_LAYERS or (!numLayers)) {
        ALOGI("%s: Unsupported layer count for mdp composition",
                __FUNCTION__);
        mCachedFrame.reset();
        return -1;
    }

    //reset old data
    mCurrentFrame.reset(numLayers);
    memset(&mCurrentFrame.drop, 0, sizeof(mCurrentFrame.drop));
    mCurrentFrame.dropCount = 0;

    // Detect the start of animation and fall back to GPU only once to cache
    // all the layers in FB and display FB content untill animation completes.
    if(ctx->listStats[mDpy].isDisplayAnimating) {
        mCurrentFrame.needsRedraw = false;
        if(ctx->mAnimationState[mDpy] == ANIMATION_STOPPED) {
            mCurrentFrame.needsRedraw = true;
            ctx->mAnimationState[mDpy] = ANIMATION_STARTED;
        }
        setMDPCompLayerFlags(ctx, list);
        mCachedFrame.updateCounts(mCurrentFrame);
        ret = -1;
        return ret;
    } else {
        ctx->mAnimationState[mDpy] = ANIMATION_STOPPED;
    }

    //Hard conditions, if not met, cannot do MDP comp
    if(isFrameDoable(ctx)) {
        generateROI(ctx, list);

        if(tryFullFrame(ctx, list) || tryVideoOnly(ctx, list)) {
            setMDPCompLayerFlags(ctx, list);
        } else {
            reset(ctx);
            memset(&mCurrentFrame.drop, 0, sizeof(mCurrentFrame.drop));
            mCurrentFrame.dropCount = 0;
            ret = -1;
        }
    } else {
        ALOGD_IF( isDebug(),"%s: MDP Comp not possible for this frame",
                __FUNCTION__);
        ret = -1;
    }

    if(isDebug()) {
        ALOGD("GEOMETRY change: %d",
                (list->flags & HWC_GEOMETRY_CHANGED));
        android::String8 sDump("");
        dump(sDump);
        ALOGD("%s",sDump.string());
    }

    mCachedFrame.cacheAll(list);
    mCachedFrame.updateCounts(mCurrentFrame);
    return ret;
}

bool MDPComp::allocSplitVGPipesfor4k2k(hwc_context_t *ctx, int index) {

    bool bRet = true;
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
//=============MDPCompNonSplit==================================================

void MDPCompNonSplit::adjustForSourceSplit(hwc_context_t *ctx,
        hwc_display_contents_1_t*) {
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

bool MDPCompNonSplit::allocLayerPipes(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    for(int index = 0; index < mCurrentFrame.layerCount; index++) {

        if(mCurrentFrame.isFBComposed[index]) continue;

        hwc_layer_1_t* layer = &list->hwLayers[index];
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(is4kx2kYuvBuffer(hnd) && sEnable4k2kYUVSplit){
            if(allocSplitVGPipesfor4k2k(ctx, index)){
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
        } else if(qdutils::MDPVersion::getInstance().is8x26() &&
                (ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres > 1024)) {
            if(qhwc::needsScaling(layer))
                type = MDPCOMP_OV_RGB;
        } else if(!qhwc::needsScaling(layer)
            && Overlay::getDMAMode() != Overlay::DMA_BLOCK_MODE
            && ctx->mMDP.version >= qdutils::MDSS_V5) {
            type = MDPCOMP_OV_DMA;
        } else if(qhwc::needsScaling(layer) &&
                !(ctx->listStats[mDpy].yuvCount) &&
                ! qdutils::MDPVersion::getInstance().isRGBScalarSupported()){
            type = MDPCOMP_OV_VG;
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

    // Set the Handle timeout to true for MDP or MIXED composition.
    if(idleInvalidator && !sIdleFallBack && mCurrentFrame.mdpCount) {
        sHandleTimeout = true;
    }

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

bool MDPCompSplit::acquireMDPPipes(hwc_context_t *ctx, hwc_layer_1_t* layer,
        MdpPipeInfoSplit& pipe_info,
        ePipeType type) {
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
                if(allocSplitVGPipesfor4k2k(ctx, index)){
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

    // Set the Handle timeout to true for MDP or MIXED composition.
    if(idleInvalidator && !sIdleFallBack && mCurrentFrame.mdpCount) {
        sHandleTimeout = true;
    }

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
                    fd = ctx->mAD->getDstFd();
                    offset = ctx->mAD->getDstOffset();
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

//================MDPCompSrcSplit==============================================
bool MDPCompSrcSplit::acquireMDPPipes(hwc_context_t *ctx, hwc_layer_1_t* layer,
        MdpPipeInfoSplit& pipe_info, ePipeType /*type*/) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    hwc_rect_t dst = layer->displayFrame;
    hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);
    pipe_info.lIndex = ovutils::OV_INVALID;
    pipe_info.rIndex = ovutils::OV_INVALID;

    //If 2 pipes are staged on a single stage of a mixer, then the left pipe
    //should have a higher priority than the right one. Pipe priorities are
    //starting with VG0, VG1 ... , RGB0 ..., DMA1
    //TODO Currently we acquire VG pipes for left side and RGB/DMA for right to
    //make sure pipe priorities are satisfied. A better way is to have priority
    //as part of overlay object and acquire any 2 pipes. Assign the higher
    //priority one to left side and lower to right side.

    //1 pipe by default for a layer
    pipe_info.lIndex = getMdpPipe(ctx, MDPCOMP_OV_VG, Overlay::MIXER_DEFAULT);
    if(pipe_info.lIndex == ovutils::OV_INVALID) {
        if(isYuvBuffer(hnd)) {
            return false;
        }
        pipe_info.lIndex = getMdpPipe(ctx, MDPCOMP_OV_ANY,
                Overlay::MIXER_DEFAULT);
        if(pipe_info.lIndex == ovutils::OV_INVALID) {
            return false;
        }
    }

    //If layer's crop width or dest width > 2048, use 2 pipes
    if((dst.right - dst.left) > qdutils::MAX_DISPLAY_DIM or
            (crop.right - crop.left) > qdutils::MAX_DISPLAY_DIM) {
        ePipeType rightType = isYuvBuffer(hnd) ?
                MDPCOMP_OV_VG : MDPCOMP_OV_ANY;
        pipe_info.rIndex = getMdpPipe(ctx, rightType, Overlay::MIXER_DEFAULT);
        if(pipe_info.rIndex == ovutils::OV_INVALID) {
            return false;
        }
    }

    return true;
}

bool MDPCompSrcSplit::allocLayerPipes(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    for(int index = 0 ; index < mCurrentFrame.layerCount; index++) {
        if(mCurrentFrame.isFBComposed[index]) continue;
        hwc_layer_1_t* layer = &list->hwLayers[index];
        int mdpIndex = mCurrentFrame.layerToMDP[index];
        PipeLayerPair& info = mCurrentFrame.mdpToLayer[mdpIndex];
        info.pipeInfo = new MdpPipeInfoSplit;
        info.rot = NULL;
        MdpPipeInfoSplit& pipe_info = *(MdpPipeInfoSplit*)info.pipeInfo;

        ePipeType type = MDPCOMP_OV_ANY;
        if(!acquireMDPPipes(ctx, layer, pipe_info, type)) {
            ALOGD_IF(isDebug(), "%s: Unable to get pipe for type = %d",
                    __FUNCTION__, (int) type);
            return false;
        }
    }
    return true;
}

int MDPCompSrcSplit::configure(hwc_context_t *ctx, hwc_layer_1_t *layer,
        PipeLayerPair& PipeLayerPair) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(!hnd) {
        ALOGE("%s: layer handle is NULL", __FUNCTION__);
        return -1;
    }
    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
    MdpPipeInfoSplit& mdp_info =
        *(static_cast<MdpPipeInfoSplit*>(PipeLayerPair.pipeInfo));
    Rotator **rot = &PipeLayerPair.rot;
    eZorder z = static_cast<eZorder>(mdp_info.zOrder);
    eIsFg isFg = IS_FG_OFF;
    eDest lDest = mdp_info.lIndex;
    eDest rDest = mdp_info.rIndex;
    hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);
    hwc_rect_t dst = layer->displayFrame;
    int transform = layer->transform;
    eTransform orient = static_cast<eTransform>(transform);
    const int downscale = 0;
    int rotFlags = ROT_FLAGS_NONE;
    uint32_t format = ovutils::getMdpFormat(hnd->format, isTileRendered(hnd));
    Whf whf(getWidth(hnd), getHeight(hnd), format, hnd->size);

    ALOGD_IF(isDebug(),"%s: configuring: layer: %p z_order: %d dest_pipeL: %d"
             "dest_pipeR: %d",__FUNCTION__, layer, z, lDest, rDest);

    // Handle R/B swap
    if (layer->flags & HWC_FORMAT_RB_SWAP) {
        if (hnd->format == HAL_PIXEL_FORMAT_RGBA_8888)
            whf.format = getMdpFormat(HAL_PIXEL_FORMAT_BGRA_8888);
        else if (hnd->format == HAL_PIXEL_FORMAT_RGBX_8888)
            whf.format = getMdpFormat(HAL_PIXEL_FORMAT_BGRX_8888);
    }

    eMdpFlags mdpFlagsL = OV_MDP_BACKEND_COMPOSITION;
    setMdpFlags(layer, mdpFlagsL, 0, transform);
    eMdpFlags mdpFlagsR = mdpFlagsL;

    if(lDest != OV_INVALID && rDest != OV_INVALID) {
        //Enable overfetch
        setMdpFlags(mdpFlagsL, OV_MDSS_MDP_DUAL_PIPE);
    }

    if(isYuvBuffer(hnd) && (transform & HWC_TRANSFORM_ROT_90)) {
        (*rot) = ctx->mRotMgr->getNext();
        if((*rot) == NULL) return -1;
        //Configure rotator for pre-rotation
        if(configRotator(*rot, whf, crop, mdpFlagsL, orient, downscale) < 0) {
            ALOGE("%s: configRotator failed!", __FUNCTION__);
            return -1;
        }
        ctx->mLayerRotMap[mDpy]->add(layer, *rot);
        whf.format = (*rot)->getDstFormat();
        updateSource(orient, whf, crop);
        rotFlags |= ROT_PREROTATED;
    }

    //If 2 pipes being used, divide layer into half, crop and dst
    hwc_rect_t cropL = crop;
    hwc_rect_t cropR = crop;
    hwc_rect_t dstL = dst;
    hwc_rect_t dstR = dst;
    if(lDest != OV_INVALID && rDest != OV_INVALID) {
        cropL.right = (crop.right + crop.left) / 2;
        cropR.left = cropL.right;
        sanitizeSourceCrop(cropL, cropR, hnd);

        //Swap crops on H flip since 2 pipes are being used
        if((orient & OVERLAY_TRANSFORM_FLIP_H) && (*rot) == NULL) {
            hwc_rect_t tmp = cropL;
            cropL = cropR;
            cropR = tmp;
        }

        dstL.right = (dst.right + dst.left) / 2;
        dstR.left = dstL.right;
    }

    //For the mdp, since either we are pre-rotating or MDP does flips
    orient = OVERLAY_TRANSFORM_0;
    transform = 0;

    //configure left pipe
    if(lDest != OV_INVALID) {
        PipeArgs pargL(mdpFlagsL, whf, z, isFg,
                static_cast<eRotFlags>(rotFlags), layer->planeAlpha,
                (ovutils::eBlending) getBlending(layer->blending));

        if(configMdp(ctx->mOverlay, pargL, orient,
                    cropL, dstL, metadata, lDest) < 0) {
            ALOGE("%s: commit failed for left mixer config", __FUNCTION__);
            return -1;
        }
    }

    //configure right pipe
    if(rDest != OV_INVALID) {
        PipeArgs pargR(mdpFlagsR, whf, z, isFg,
                static_cast<eRotFlags>(rotFlags),
                layer->planeAlpha,
                (ovutils::eBlending) getBlending(layer->blending));
        if(configMdp(ctx->mOverlay, pargR, orient,
                    cropR, dstR, metadata, rDest) < 0) {
            ALOGE("%s: commit failed for right mixer config", __FUNCTION__);
            return -1;
        }
    }

    return 0;
}

}; //namespace

