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
int MDPComp::sMaxPipesPerMixer = MAX_PIPES_PER_MIXER;

MDPComp* MDPComp::getObject(const int& width, const int& rightSplit,
        const int& dpy) {
    if(width > MAX_DISPLAY_DIM || rightSplit) {
        return new MDPCompHighRes(dpy);
    }
    return new MDPCompLowRes(dpy);
}

MDPComp::MDPComp(int dpy):mDpy(dpy){};

void MDPComp::dump(android::String8& buf)
{
    Locker::Autolock _l(mMdpCompLock);
    dumpsys_log(buf,"HWC Map for Dpy: %s \n",
                (mDpy == 0) ? "\"PRIMARY\"" :
                (mDpy == 1) ? "\"EXTERNAL\"" : "\"VIRTUAL\"");
    dumpsys_log(buf,"PREV_FRAME: layerCount:%2d    mdpCount:%2d \
                cacheCount:%2d \n", mCachedFrame.layerCount,
                mCachedFrame.mdpCount, mCachedFrame.cacheCount);
    dumpsys_log(buf,"CURR_FRAME: layerCount:%2d    mdpCount:%2d \
                fbCount:%2d \n", mCurrentFrame.layerCount,
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
                     (mCurrentFrame.needsRedraw ? "GLES" : "CACHE") : "MDP"),
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
        if(!setupBasePipe(ctx)) {
            ALOGE("%s: Failed to setup primary base pipe", __FUNCTION__);
            return false;
        }
    }

    sEnableMixedMode = true;
    if((property_get("debug.mdpcomp.mixedmode.disable", property, NULL) > 0) &&
       (!strncmp(property, "1", PROPERTY_VALUE_MAX ) ||
        (!strncasecmp(property,"true", PROPERTY_VALUE_MAX )))) {
        sEnableMixedMode = false;
    }

    sDebugLogs = false;
    if(property_get("debug.mdpcomp.logs", property, NULL) > 0) {
        if(atoi(property) != 0)
            sDebugLogs = true;
    }

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
            mCachedFrame.hnd[index] = NULL;
        } else {
            if(!mCurrentFrame.needsRedraw)
                layer->compositionType = HWC_OVERLAY;
        }
    }
}

/*
 * Sets up BORDERFILL as default base pipe and detaches RGB0.
 * Framebuffer is always updated using PLAY ioctl.
 */
bool MDPComp::setupBasePipe(hwc_context_t *ctx) {
    const int dpy = HWC_DISPLAY_PRIMARY;
    int fb_stride = ctx->dpyAttr[dpy].stride;
    int fb_width = ctx->dpyAttr[dpy].xres;
    int fb_height = ctx->dpyAttr[dpy].yres;
    int fb_fd = ctx->dpyAttr[dpy].fd;

    mdp_overlay ovInfo;
    msmfb_overlay_data ovData;
    memset(&ovInfo, 0, sizeof(mdp_overlay));
    memset(&ovData, 0, sizeof(msmfb_overlay_data));

    ovInfo.src.format = MDP_RGB_BORDERFILL;
    ovInfo.src.width  = fb_width;
    ovInfo.src.height = fb_height;
    ovInfo.src_rect.w = fb_width;
    ovInfo.src_rect.h = fb_height;
    ovInfo.dst_rect.w = fb_width;
    ovInfo.dst_rect.h = fb_height;
    ovInfo.id = MSMFB_NEW_REQUEST;

    if (ioctl(fb_fd, MSMFB_OVERLAY_SET, &ovInfo) < 0) {
        ALOGE("Failed to call ioctl MSMFB_OVERLAY_SET err=%s",
              strerror(errno));
        return false;
    }

    ovData.id = ovInfo.id;
    if (ioctl(fb_fd, MSMFB_OVERLAY_PLAY, &ovData) < 0) {
        ALOGE("Failed to call ioctl MSMFB_OVERLAY_PLAY err=%s",
              strerror(errno));
        return false;
    }
    return true;
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
    mdpCount = 0;
    cacheCount = 0;
    layerCount = 0;
    fbZ = -1;
}

void MDPComp::LayerCache::cacheAll(hwc_display_contents_1_t* list) {
    const int numAppLayers = list->numHwLayers - 1;
    for(int i = 0; i < numAppLayers; i++) {
        hnd[i] = list->hwLayers[i].handle;
    }
}

void MDPComp::LayerCache::updateCounts(const FrameInfo& curFrame) {
    mdpCount = curFrame.mdpCount;
    cacheCount = curFrame.fbCount;
    layerCount = curFrame.layerCount;
    fbZ = curFrame.fbZ;
}

bool MDPComp::isValidDimension(hwc_context_t *ctx, hwc_layer_1_t *layer) {
    const int dpy = HWC_DISPLAY_PRIMARY;
    private_handle_t *hnd = (private_handle_t *)layer->handle;

    if(!hnd) {
        ALOGE("%s: layer handle is NULL", __FUNCTION__);
        return false;
    }

    int hw_w = ctx->dpyAttr[mDpy].xres;
    int hw_h = ctx->dpyAttr[mDpy].yres;

    hwc_rect_t crop = layer->sourceCrop;
    hwc_rect_t dst = layer->displayFrame;

    if(dst.left < 0 || dst.top < 0 || dst.right > hw_w || dst.bottom > hw_h) {
       hwc_rect_t scissor = {0, 0, hw_w, hw_h };
       qhwc::calculate_crop_rects(crop, dst, scissor, layer->transform);
    }

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

/* Checks for conditions where all the layers marked for MDP comp cannot be
 * bypassed. On such conditions we try to bypass atleast YUV layers */
bool MDPComp::isFullFrameDoable(hwc_context_t *ctx,
                                hwc_display_contents_1_t* list){

    const int numAppLayers = ctx->listStats[mDpy].numAppLayers;

    if(sIdleFallBack) {
        ALOGD_IF(isDebug(), "%s: Idle fallback dpy %d",__FUNCTION__, mDpy);
        return false;
    }

    if(mDpy > HWC_DISPLAY_PRIMARY){
        ALOGD_IF(isDebug(), "%s: Cannot support External display(s)",
                 __FUNCTION__);
        return false;
    }

    if(isSkipPresent(ctx, mDpy)) {
        ALOGD_IF(isDebug(),"%s: SKIP present: %d",
                __FUNCTION__,
                isSkipPresent(ctx, mDpy));
        return false;
    }

    if(ctx->listStats[mDpy].needsAlphaScale
       && ctx->mMDP.version < qdutils::MDSS_V5) {
        ALOGD_IF(isDebug(), "%s: frame needs alpha downscaling",__FUNCTION__);
        return false;
    }

    //MDP composition is not efficient if layer needs rotator.
    for(int i = 0; i < numAppLayers; ++i) {
        // As MDP h/w supports flip operation, use MDP comp only for
        // 180 transforms. Fail for any transform involving 90 (90, 270).
        hwc_layer_1_t* layer = &list->hwLayers[i];
        private_handle_t *hnd = (private_handle_t *)layer->handle;

        if((layer->transform & HWC_TRANSFORM_ROT_90) && !isYuvBuffer(hnd)) {
            ALOGD_IF(isDebug(), "%s: orientation involved",__FUNCTION__);
            return false;
        }

        if(!isValidDimension(ctx,layer)) {
            ALOGD_IF(isDebug(), "%s: Buffer is of invalid width",
                __FUNCTION__);
            return false;
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
    //Setup mCurrentFrame
    mCurrentFrame.mdpCount = mCurrentFrame.layerCount;
    mCurrentFrame.fbCount = 0;
    mCurrentFrame.fbZ = -1;
    memset(&mCurrentFrame.isFBComposed, 0, sizeof(mCurrentFrame.isFBComposed));

    int mdpCount = mCurrentFrame.mdpCount;
    if(mdpCount > sMaxPipesPerMixer) {
        ALOGD_IF(isDebug(), "%s: Exceeds MAX_PIPES_PER_MIXER",__FUNCTION__);
        return false;
    }

    if(!arePipesAvailable(ctx, list)) {
        return false;
    }

    return true;
}

bool MDPComp::partialMDPComp(hwc_context_t *ctx, hwc_display_contents_1_t* list)
{
    int numAppLayers = ctx->listStats[mDpy].numAppLayers;

    if(!sEnableMixedMode) {
        //Mixed mode is disabled. No need to even try caching.
        return false;
    }

    //Setup mCurrentFrame
    mCurrentFrame.reset(numAppLayers);
    updateLayerCache(ctx, list);
    updateYUV(ctx, list);
    batchLayers(); //sets up fbZ also

    int mdpCount = mCurrentFrame.mdpCount;
    if(mdpCount > (sMaxPipesPerMixer - 1)) { // -1 since FB is used
        ALOGD_IF(isDebug(), "%s: Exceeds MAX_PIPES_PER_MIXER",__FUNCTION__);
        return false;
    }

    if(!arePipesAvailable(ctx, list)) {
        return false;
    }

    return true;
}

bool MDPComp::isOnlyVideoDoable(hwc_context_t *ctx,
        hwc_display_contents_1_t* list){
    int numAppLayers = ctx->listStats[mDpy].numAppLayers;
    mCurrentFrame.reset(numAppLayers);
    updateYUV(ctx, list);
    int mdpCount = mCurrentFrame.mdpCount;
    int fbNeeded = int(mCurrentFrame.fbCount != 0);

    if(!isYuvPresent(ctx, mDpy)) {
        return false;
    }

    if(!mdpCount)
        return false;

    if(mdpCount > (sMaxPipesPerMixer - fbNeeded)) {
        ALOGD_IF(isDebug(), "%s: Exceeds MAX_PIPES_PER_MIXER",__FUNCTION__);
        return false;
    }

    if(!arePipesAvailable(ctx, list)) {
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

    if(isSecuring(ctx, layer)) {
        ALOGD_IF(isDebug(), "%s: MDP securing is active", __FUNCTION__);
        return false;
    }

    if(!isValidDimension(ctx, layer)) {
        ALOGD_IF(isDebug(), "%s: Buffer is of invalid width",
            __FUNCTION__);
        return false;
    }

    return true;
}

void  MDPComp::batchLayers() {
    /* Idea is to keep as many contiguous non-updating(cached) layers in FB and
     * send rest of them through MDP. NEVER mark an updating layer for caching.
     * But cached ones can be marked for MDP*/

    int maxBatchStart = -1;
    int maxBatchCount = 0;

    /* All or Nothing is cached. No batching needed */
    if(!mCurrentFrame.fbCount) {
        mCurrentFrame.fbZ = -1;
        return;
    }
    if(!mCurrentFrame.mdpCount) {
        mCurrentFrame.fbZ = 0;
        return;
    }

    /* Search for max number of contiguous (cached) layers */
    int i = 0;
    while (i < mCurrentFrame.layerCount) {
        int count = 0;
        while(mCurrentFrame.isFBComposed[i] && i < mCurrentFrame.layerCount) {
            count++; i++;
        }
        if(count > maxBatchCount) {
            maxBatchCount = count;
            maxBatchStart = i - count;
            mCurrentFrame.fbZ = maxBatchStart;
        }
        if(i < mCurrentFrame.layerCount) i++;
    }

    /* reset rest of the layers for MDP comp */
    for(int i = 0; i < mCurrentFrame.layerCount; i++) {
        if(i != maxBatchStart){
            mCurrentFrame.isFBComposed[i] = false;
        } else {
            i += maxBatchCount;
        }
    }

    mCurrentFrame.fbCount = maxBatchCount;
    mCurrentFrame.mdpCount = mCurrentFrame.layerCount -
            mCurrentFrame.fbCount;

    ALOGD_IF(isDebug(),"%s: cached count: %d",__FUNCTION__,
             mCurrentFrame.fbCount);
}

void MDPComp::updateLayerCache(hwc_context_t* ctx,
                               hwc_display_contents_1_t* list) {

    int numAppLayers = ctx->listStats[mDpy].numAppLayers;
    int numCacheableLayers = 0;

    for(int i = 0; i < numAppLayers; i++) {
        if (mCachedFrame.hnd[i] == list->hwLayers[i].handle) {
            numCacheableLayers++;
            mCurrentFrame.isFBComposed[i] = true;
        } else {
            mCurrentFrame.isFBComposed[i] = false;
            mCachedFrame.hnd[i] = list->hwLayers[i].handle;
        }
    }

    mCurrentFrame.fbCount = numCacheableLayers;
    mCurrentFrame.mdpCount = mCurrentFrame.layerCount -
            mCurrentFrame.fbCount;
    ALOGD_IF(isDebug(),"%s: cached count: %d",__FUNCTION__, numCacheableLayers);
}

void MDPComp::updateYUV(hwc_context_t* ctx, hwc_display_contents_1_t* list) {

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
                mCurrentFrame.isFBComposed[nYuvIndex] = false;
                mCurrentFrame.fbCount--;
            }
        }
    }

    mCurrentFrame.mdpCount = mCurrentFrame.layerCount -
            mCurrentFrame.fbCount;
    ALOGD_IF(isDebug(),"%s: cached count: %d",__FUNCTION__,
             mCurrentFrame.fbCount);
}

bool MDPComp::programMDP(hwc_context_t *ctx, hwc_display_contents_1_t* list) {
    if(!allocLayerPipes(ctx, list)) {
        ALOGD_IF(isDebug(), "%s: Unable to allocate MDP pipes", __FUNCTION__);
        return false;
    }

    bool fbBatch = false;
    for (int index = 0, mdpNextZOrder = 0; index < mCurrentFrame.layerCount;
            index++) {
        if(!mCurrentFrame.isFBComposed[index]) {
            int mdpIndex = mCurrentFrame.layerToMDP[index];
            hwc_layer_1_t* layer = &list->hwLayers[index];

            MdpPipeInfo* cur_pipe = mCurrentFrame.mdpToLayer[mdpIndex].pipeInfo;
            cur_pipe->zOrder = mdpNextZOrder++;

            if(configure(ctx, layer, mCurrentFrame.mdpToLayer[mdpIndex]) != 0 ){
                ALOGD_IF(isDebug(), "%s: Failed to configure overlay for \
                         layer %d",__FUNCTION__, index);
                return false;
            }
        } else if(fbBatch == false) {
                mdpNextZOrder++;
                fbBatch = true;
        }
    }

    return true;
}

bool MDPComp::programYUV(hwc_context_t *ctx, hwc_display_contents_1_t* list) {
    if(!allocLayerPipes(ctx, list)) {
        ALOGD_IF(isDebug(), "%s: Unable to allocate MDP pipes", __FUNCTION__);
        return false;
    }
    //If we are in this block, it means we have yuv + rgb layers both
    int mdpIdx = 0;
    for (int index = 0; index < mCurrentFrame.layerCount; index++) {
        if(!mCurrentFrame.isFBComposed[index]) {
            hwc_layer_1_t* layer = &list->hwLayers[index];
            int mdpIndex = mCurrentFrame.layerToMDP[index];
            MdpPipeInfo* cur_pipe =
                    mCurrentFrame.mdpToLayer[mdpIndex].pipeInfo;
            cur_pipe->zOrder = mdpIdx++;

            if(configure(ctx, layer,
                        mCurrentFrame.mdpToLayer[mdpIndex]) != 0 ){
                ALOGD_IF(isDebug(), "%s: Failed to configure overlay for \
                        layer %d",__FUNCTION__, index);
                return false;
            }
        }
    }
    return true;
}

int MDPComp::prepare(hwc_context_t *ctx, hwc_display_contents_1_t* list) {
    const int numLayers = ctx->listStats[mDpy].numAppLayers;

    { //LOCK SCOPE BEGIN
        Locker::Autolock _l(mMdpCompLock);

        //reset old data
        mCurrentFrame.reset(numLayers);

        //number of app layers exceeds MAX_NUM_APP_LAYERS fall back to GPU
        //do not cache the information for next draw cycle.
        if(numLayers > MAX_NUM_APP_LAYERS) {
            mCachedFrame.updateCounts(mCurrentFrame);
            ALOGD_IF(isDebug(), "%s: Number of App layers exceeded the limit ",
                                    __FUNCTION__);
            return -1;
        }

        //Hard conditions, if not met, cannot do MDP comp
        if(!isFrameDoable(ctx)) {
            ALOGD_IF( isDebug(),"%s: MDP Comp not possible for this frame",
                                    __FUNCTION__);
            reset(numLayers, list);
            return -1;
        }

        //Check whether layers marked for MDP Composition is actually doable.
        if(isFullFrameDoable(ctx, list)){
            mCurrentFrame.map();
            //Configure framebuffer first if applicable
            if(mCurrentFrame.fbZ >= 0) {
                if(!ctx->mFBUpdate[mDpy]->prepare(ctx, list,
                        mCurrentFrame.fbZ)) {
                    ALOGE("%s configure framebuffer failed", __func__);
                    reset(numLayers, list);
                    return -1;
                }
            }
            //Acquire and Program MDP pipes
            if(!programMDP(ctx, list)) {
                reset(numLayers, list);
                return -1;
            } else { //Success
                //Any change in composition types needs an FB refresh
                mCurrentFrame.needsRedraw = false;
                if(mCurrentFrame.fbCount &&
                        ((mCurrentFrame.mdpCount != mCachedFrame.mdpCount) ||
                        (mCurrentFrame.fbCount != mCachedFrame.cacheCount) ||
                        (mCurrentFrame.fbZ != mCachedFrame.fbZ) ||
                        (!mCurrentFrame.mdpCount) ||
                        (list->flags & HWC_GEOMETRY_CHANGED) ||
                        isSkipPresent(ctx, mDpy) ||
                        (mDpy > HWC_DISPLAY_PRIMARY))) {
                    mCurrentFrame.needsRedraw = true;
                }
            }
        } else if(isOnlyVideoDoable(ctx, list)) {
            //All layers marked for MDP comp cannot be bypassed.
            //Try to compose atleast YUV layers through MDP comp and let
            //all the RGB layers compose in FB
            //Destination over
            mCurrentFrame.fbZ = -1;
            if(mCurrentFrame.fbCount)
                mCurrentFrame.fbZ = mCurrentFrame.mdpCount;

            mCurrentFrame.map();

            //Configure framebuffer first if applicable
            if(mCurrentFrame.fbZ >= 0) {
                if(!ctx->mFBUpdate[mDpy]->prepare(ctx, list, mCurrentFrame.fbZ)) {
                    ALOGE("%s configure framebuffer failed", __func__);
                    reset(numLayers, list);
                    return -1;
                }
            }
            if(!programYUV(ctx, list)) {
                reset(numLayers, list);
                return -1;
            }
        } else {
            reset(numLayers, list);
            return -1;
        }

        //UpdateLayerFlags
        setMDPCompLayerFlags(ctx, list);
        mCachedFrame.updateCounts(mCurrentFrame);

    } //LOCK SCOPE END. dump also need this lock.
    // unlock it before calling dump function to avoid deadlock
    if(isDebug()) {
        ALOGD("GEOMETRY change: %d", (list->flags & HWC_GEOMETRY_CHANGED));
        android::String8 sDump("");
        dump(sDump);
        ALOGE("%s",sDump.string());
    }

    return 0;
}

//=============MDPCompLowRes===================================================

/*
 * Configures pipe(s) for MDP composition
 */
int MDPCompLowRes::configure(hwc_context_t *ctx, hwc_layer_1_t *layer,
                             PipeLayerPair& PipeLayerPair) {
    MdpPipeInfoLowRes& mdp_info =
        *(static_cast<MdpPipeInfoLowRes*>(PipeLayerPair.pipeInfo));
    eMdpFlags mdpFlags = OV_MDP_BACKEND_COMPOSITION;
    eZorder zOrder = static_cast<eZorder>(mdp_info.zOrder);
    eIsFg isFg = IS_FG_OFF;
    eDest dest = mdp_info.index;

    ALOGD_IF(isDebug(),"%s: configuring: layer: %p z_order: %d dest_pipe: %d",
             __FUNCTION__, layer, zOrder, dest);

    return configureLowRes(ctx, layer, mDpy, mdpFlags, zOrder, isFg, dest,
                           &PipeLayerPair.rot);
}

bool MDPCompLowRes::arePipesAvailable(hwc_context_t *ctx,
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

    return true;
}

bool MDPCompLowRes::allocLayerPipes(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    for(int index = 0; index < mCurrentFrame.layerCount; index++) {

        if(mCurrentFrame.isFBComposed[index]) continue;

        hwc_layer_1_t* layer = &list->hwLayers[index];
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        int mdpIndex = mCurrentFrame.layerToMDP[index];
        PipeLayerPair& info = mCurrentFrame.mdpToLayer[mdpIndex];
        info.pipeInfo = new MdpPipeInfoLowRes;
        info.rot = NULL;
        MdpPipeInfoLowRes& pipe_info = *(MdpPipeInfoLowRes*)info.pipeInfo;
        ePipeType type = MDPCOMP_OV_ANY;

        if(isYuvBuffer(hnd)) {
            type = MDPCOMP_OV_VG;
        } else if(!qhwc::needsScaling(ctx, layer, mDpy)
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

bool MDPCompLowRes::draw(hwc_context_t *ctx, hwc_display_contents_1_t* list) {

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

    Locker::Autolock _l(mMdpCompLock);

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

        int mdpIndex = mCurrentFrame.layerToMDP[i];

        MdpPipeInfoLowRes& pipe_info =
            *(MdpPipeInfoLowRes*)mCurrentFrame.mdpToLayer[mdpIndex].pipeInfo;
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

        if(ctx->mAD->isModeOn()) {
            if(ctx->mAD->draw(ctx, fd, offset)) {
                fd = ctx->mAD->getDstFd(ctx);
                offset = ctx->mAD->getDstOffset(ctx);
            }
        }

        Rotator *rot = mCurrentFrame.mdpToLayer[mdpIndex].rot;
        if(rot) {
            if(!rot->queueBuffer(fd, offset))
                return false;
            fd = rot->getDstMemId();
            offset = rot->getDstOffset();
        }

        if (!ov.queueBuffer(fd, offset, dest)) {
            ALOGE("%s: queueBuffer failed for display:%d ", __FUNCTION__, mDpy);
            return false;
        }

        layerProp[i].mFlags &= ~HWC_MDPCOMP;
    }
    return true;
}

//=============MDPCompHighRes===================================================

int MDPCompHighRes::pipesNeeded(hwc_context_t *ctx,
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

bool MDPCompHighRes::arePipesAvailable(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    overlay::Overlay& ov = *ctx->mOverlay;

    for(int i = 0; i < Overlay::MIXER_MAX; i++) {
        int numPipesNeeded = pipesNeeded(ctx, list, i);
        int availPipes = ov.availablePipes(mDpy, i);

        //Reserve pipe(s)for FB
        if(mCurrentFrame.fbCount)
            availPipes -= 1;

        if(numPipesNeeded > availPipes) {
            ALOGD_IF(isDebug(), "%s: Insufficient pipes for "
                     "dpy %d mixer %d needed %d, avail %d",
                     __FUNCTION__, mDpy, i, numPipesNeeded, availPipes);
            return false;
        }
    }
    return true;
}

bool MDPCompHighRes::acquireMDPPipes(hwc_context_t *ctx, hwc_layer_1_t* layer,
        MdpPipeInfoHighRes& pipe_info,
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

bool MDPCompHighRes::allocLayerPipes(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    for(int index = 0 ; index < mCurrentFrame.layerCount; index++) {

        if(mCurrentFrame.isFBComposed[index]) continue;

        hwc_layer_1_t* layer = &list->hwLayers[index];
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        int mdpIndex = mCurrentFrame.layerToMDP[index];
        PipeLayerPair& info = mCurrentFrame.mdpToLayer[mdpIndex];
        info.pipeInfo = new MdpPipeInfoHighRes;
        info.rot = NULL;
        MdpPipeInfoHighRes& pipe_info = *(MdpPipeInfoHighRes*)info.pipeInfo;
        ePipeType type = MDPCOMP_OV_ANY;

        if(isYuvBuffer(hnd)) {
            type = MDPCOMP_OV_VG;
        } else if(!qhwc::needsScaling(ctx, layer, mDpy)
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

/*
 * Configures pipe(s) for MDP composition
 */
int MDPCompHighRes::configure(hwc_context_t *ctx, hwc_layer_1_t *layer,
        PipeLayerPair& PipeLayerPair) {
    MdpPipeInfoHighRes& mdp_info =
        *(static_cast<MdpPipeInfoHighRes*>(PipeLayerPair.pipeInfo));
    eZorder zOrder = static_cast<eZorder>(mdp_info.zOrder);
    eIsFg isFg = IS_FG_OFF;
    eMdpFlags mdpFlagsL = OV_MDP_BACKEND_COMPOSITION;
    eDest lDest = mdp_info.lIndex;
    eDest rDest = mdp_info.rIndex;

    ALOGD_IF(isDebug(),"%s: configuring: layer: %p z_order: %d dest_pipeL: %d"
             "dest_pipeR: %d",__FUNCTION__, layer, zOrder, lDest, rDest);

    return configureHighRes(ctx, layer, mDpy, mdpFlagsL, zOrder, isFg, lDest,
                            rDest, &PipeLayerPair.rot);
}

bool MDPCompHighRes::draw(hwc_context_t *ctx, hwc_display_contents_1_t* list) {

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

    Locker::Autolock _l(mMdpCompLock);

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

        MdpPipeInfoHighRes& pipe_info =
            *(MdpPipeInfoHighRes*)mCurrentFrame.mdpToLayer[mdpIndex].pipeInfo;
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
                ALOGE("%s: queueBuffer failed for left mixer", __FUNCTION__);
                return false;
            }
        }

        //************* play right mixer **********
        if(indexR != ovutils::OV_INVALID) {
            ovutils::eDest destR = (ovutils::eDest)indexR;
            ALOGD_IF(isDebug(),"%s: MDP Comp: Drawing layer: %p hnd: %p \
                     using  pipe: %d", __FUNCTION__, layer, hnd, indexR );
            if (!ov.queueBuffer(fd, offset, destR)) {
                ALOGE("%s: queueBuffer failed for right mixer", __FUNCTION__);
                return false;
            }
        }

        layerProp[i].mFlags &= ~HWC_MDPCOMP;
    }

    return true;
}
}; //namespace

