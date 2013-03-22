/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are
 * retained for attribution purposes only.
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

#define DEBUG_FBUPDATE 0
#include <gralloc_priv.h>
#include "hwc_fbupdate.h"
#include "hwc_video.h"

namespace qhwc {

namespace ovutils = overlay::utils;

IFBUpdate* IFBUpdate::getObject(const int& width, const int& dpy) {
    if(width > MAX_DISPLAY_DIM) {
        return new FBUpdateHighRes(dpy);
    }
    return new FBUpdateLowRes(dpy);
}

inline void IFBUpdate::reset() {
    mModeOn = false;
}

bool IFBUpdate::needFbUpdate(hwc_context_t *ctx,
        const hwc_display_contents_1_t *list, int dpy) {
    // if Video Overlay is on and and YUV layers are passed through overlay
    // , no need to configure FB layer.
    if(ctx->mVidOv[dpy]->isModeOn() &&
        (ctx->listStats[dpy].yuvCount == ctx->listStats[dpy].numAppLayers))
        return false;

    return true;
}
//================= Low res====================================
FBUpdateLowRes::FBUpdateLowRes(const int& dpy): IFBUpdate(dpy) {}

inline void FBUpdateLowRes::reset() {
    IFBUpdate::reset();
    mDest = ovutils::OV_INVALID;
}

bool FBUpdateLowRes::prepare(hwc_context_t *ctx, hwc_display_contents_1 *list)
{
    if(!ctx->mMDP.hasOverlay) {
        ALOGD_IF(DEBUG_FBUPDATE, "%s, this hw doesnt support overlays",
                __FUNCTION__);
       return false;
    }
    mModeOn = configure(ctx, list);
    ALOGD_IF(DEBUG_FBUPDATE, "%s, mModeOn = %d", __FUNCTION__, mModeOn);
    return mModeOn;
}

// Configure
bool FBUpdateLowRes::configure(hwc_context_t *ctx,
                               hwc_display_contents_1 *list)
{
    bool ret = false;
    hwc_layer_1_t *layer = &list->hwLayers[list->numHwLayers - 1];
    if (LIKELY(ctx->mOverlay)) {
        // When Video overlay is in use and there are no UI layers to
        // be composed to FB , no need to configure FbUpdate.
        if(!needFbUpdate(ctx, list, mDpy))
            return false;

        overlay::Overlay& ov = *(ctx->mOverlay);
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        ovutils::Whf info(hnd->width, hnd->height,
                ovutils::getMdpFormat(hnd->format), hnd->size);

        //Request an RGB pipe
        ovutils::eDest dest = ov.nextPipe(ovutils::OV_MDP_PIPE_RGB, mDpy);
        if(dest == ovutils::OV_INVALID) { //None available
            return false;
        }

        mDest = dest;

        ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
        // If any of the layers has pre-multiplied alpha, set Pre multiplied
        // Flag as the compositied output is alpha pre-multiplied.
        if(ctx->listStats[mDpy].preMultipliedAlpha == true)
               ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_BLEND_FG_PREMULT);

        ovutils::eZorder z_order =
              ctx->mVidOv[mDpy]->isModeOn()?ovutils::ZORDER_1:ovutils::ZORDER_0;
        ovutils::eIsFg is_fg =
           ctx->mVidOv[mDpy]->isModeOn()? ovutils::IS_FG_OFF:ovutils::IS_FG_SET;

        ovutils::PipeArgs parg(mdpFlags,
                info,
                z_order,
                is_fg,
                ovutils::ROT_FLAGS_NONE);
        ov.setSource(parg, dest);

        hwc_rect_t sourceCrop;
        getNonWormholeRegion(list, sourceCrop);
        // x,y,w,h
        ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
                sourceCrop.right - sourceCrop.left,
                sourceCrop.bottom - sourceCrop.top);
        ov.setCrop(dcrop, dest);

        int transform = layer->transform;
        ovutils::eTransform orient =
                static_cast<ovutils::eTransform>(transform);
        ov.setTransform(orient, dest);

        hwc_rect_t displayFrame = sourceCrop;
        ovutils::Dim dpos(displayFrame.left,
                displayFrame.top,
                displayFrame.right - displayFrame.left,
                displayFrame.bottom - displayFrame.top);
        // Calculate the actionsafe dimensions for External(dpy = 1 or 2)
        if(mDpy)
            getActionSafePosition(ctx, mDpy, dpos.x, dpos.y, dpos.w, dpos.h);
        ov.setPosition(dpos, dest);

        ret = true;
        if (!ov.commit(dest)) {
            ALOGE("%s: commit fails", __FUNCTION__);
            ret = false;
        }
    }
    return ret;
}

bool FBUpdateLowRes::draw(hwc_context_t *ctx, private_handle_t *hnd)
{
    if(!mModeOn) {
        return true;
    }
    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eDest dest = mDest;
    if (!ov.queueBuffer(hnd->fd, hnd->offset, dest)) {
        ALOGE("%s: queueBuffer failed for FBUpdate", __FUNCTION__);
        ret = false;
    }
    return ret;
}

//================= High res====================================
FBUpdateHighRes::FBUpdateHighRes(const int& dpy): IFBUpdate(dpy) {}

inline void FBUpdateHighRes::reset() {
    IFBUpdate::reset();
    mDestLeft = ovutils::OV_INVALID;
    mDestRight = ovutils::OV_INVALID;
}

bool FBUpdateHighRes::prepare(hwc_context_t *ctx, hwc_display_contents_1 *list)
{
    if(!ctx->mMDP.hasOverlay) {
        ALOGD_IF(DEBUG_FBUPDATE, "%s, this hw doesnt support overlays",
                __FUNCTION__);
       return false;
    }
    ALOGD_IF(DEBUG_FBUPDATE, "%s, mModeOn = %d", __FUNCTION__, mModeOn);
    mModeOn = configure(ctx, list);
    return mModeOn;
}

// Configure
bool FBUpdateHighRes::configure(hwc_context_t *ctx,
                                hwc_display_contents_1 *list)
{
    bool ret = false;
    hwc_layer_1_t *layer = &list->hwLayers[list->numHwLayers - 1];
    if (LIKELY(ctx->mOverlay)) {
        // When Video overlay is in use and there are no UI layers to
        // be composed to FB , no need to configure FbUpdate.
        if(!needFbUpdate(ctx, list, mDpy))
            return false;

        overlay::Overlay& ov = *(ctx->mOverlay);
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        ovutils::Whf info(hnd->width, hnd->height,
                ovutils::getMdpFormat(hnd->format), hnd->size);

        //Request left RGB pipe
        ovutils::eDest destL = ov.nextPipe(ovutils::OV_MDP_PIPE_RGB, mDpy);
        if(destL == ovutils::OV_INVALID) { //None available
            return false;
        }
        //Request right RGB pipe
        ovutils::eDest destR = ov.nextPipe(ovutils::OV_MDP_PIPE_RGB, mDpy);
        if(destR == ovutils::OV_INVALID) { //None available
            return false;
        }

        mDestLeft = destL;
        mDestRight = destR;

        ovutils::eMdpFlags mdpFlagsL = ovutils::OV_MDP_FLAGS_NONE;
        //If any layer has pre-multiplied alpha, set Pre multiplied
        //Flag as the compositied output is alpha pre-multiplied.
        if(ctx->listStats[mDpy].preMultipliedAlpha == true)
            ovutils::setMdpFlags(mdpFlagsL, ovutils::OV_MDP_BLEND_FG_PREMULT);

        ovutils::eZorder z_order =
              ctx->mVidOv[mDpy]->isModeOn()?ovutils::ZORDER_1:ovutils::ZORDER_0;
        ovutils::eIsFg is_fg =
           ctx->mVidOv[mDpy]->isModeOn()? ovutils::IS_FG_OFF:ovutils::IS_FG_SET;

        ovutils::PipeArgs pargL(mdpFlagsL,
                info,
                z_order,
                is_fg,
                ovutils::ROT_FLAGS_NONE);
        ov.setSource(pargL, destL);

        ovutils::eMdpFlags mdpFlagsR = mdpFlagsL;
        ovutils::setMdpFlags(mdpFlagsR, ovutils::OV_MDSS_MDP_RIGHT_MIXER);
        ovutils::PipeArgs pargR(mdpFlagsR,
                info,
                z_order,
                is_fg,
                ovutils::ROT_FLAGS_NONE);
        ov.setSource(pargR, destR);

        hwc_rect_t sourceCrop;
        getNonWormholeRegion(list, sourceCrop);
        ovutils::Dim dcropL(sourceCrop.left, sourceCrop.top,
                (sourceCrop.right - sourceCrop.left) / 2,
                sourceCrop.bottom - sourceCrop.top);
        ovutils::Dim dcropR(
                sourceCrop.left + (sourceCrop.right - sourceCrop.left) / 2,
                sourceCrop.top,
                (sourceCrop.right - sourceCrop.left) / 2,
                sourceCrop.bottom - sourceCrop.top);
        ov.setCrop(dcropL, destL);
        ov.setCrop(dcropR, destR);

        int transform = layer->transform;
        ovutils::eTransform orient =
                static_cast<ovutils::eTransform>(transform);
        ov.setTransform(orient, destL);
        ov.setTransform(orient, destR);

        hwc_rect_t displayFrame = sourceCrop;
        //For FB left, top will always be 0
        //That should also be the case if using 2 mixers for single display
        ovutils::Dim dposL(displayFrame.left,
                displayFrame.top,
                (displayFrame.right - displayFrame.left) / 2,
                displayFrame.bottom - displayFrame.top);
        ov.setPosition(dposL, destL);
        ovutils::Dim dposR(0,
                displayFrame.top,
                (displayFrame.right - displayFrame.left) / 2,
                displayFrame.bottom - displayFrame.top);
        ov.setPosition(dposR, destR);

        ret = true;
        if (!ov.commit(destL)) {
            ALOGE("%s: commit fails for left", __FUNCTION__);
            ret = false;
        }
        if (!ov.commit(destR)) {
            ALOGE("%s: commit fails for right", __FUNCTION__);
            ret = false;
        }
    }
    return ret;
}

bool FBUpdateHighRes::draw(hwc_context_t *ctx, private_handle_t *hnd)
{
    if(!mModeOn) {
        return true;
    }
    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eDest destL = mDestLeft;
    ovutils::eDest destR = mDestRight;
    if (!ov.queueBuffer(hnd->fd, hnd->offset, destL)) {
        ALOGE("%s: queue failed for left of dpy = %d",
                __FUNCTION__, mDpy);
        ret = false;
    }
    if (!ov.queueBuffer(hnd->fd, hnd->offset, destR)) {
        ALOGE("%s: queue failed for right of dpy = %d",
                __FUNCTION__, mDpy);
        ret = false;
    }
    return ret;
}

//---------------------------------------------------------------------
}; //namespace qhwc
