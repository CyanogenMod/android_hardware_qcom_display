/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are retained for
 * attribution purposes only
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

#define VIDEO_DEBUG 0
#include <overlay.h>
#include "hwc_video.h"
#include "hwc_utils.h"
#include "qdMetaData.h"
#include "mdp_version.h"
#include <overlayRotator.h>

using overlay::Rotator;

namespace qhwc {

namespace ovutils = overlay::utils;

//===========IVideoOverlay=========================
IVideoOverlay* IVideoOverlay::getObject(const int& width, const int& dpy) {
    if(width > MAX_DISPLAY_DIM) {
        return new VideoOverlayHighRes(dpy);
    }
    return new VideoOverlayLowRes(dpy);
}

//===========VideoOverlayLowRes=========================

VideoOverlayLowRes::VideoOverlayLowRes(const int& dpy): IVideoOverlay(dpy) {}

//Cache stats, figure out the state, config overlay
bool VideoOverlayLowRes::prepare(hwc_context_t *ctx,
        hwc_display_contents_1_t *list) {

    if(ctx->listStats[mDpy].yuvCount > 1)
        return false;

    int yuvIndex =  ctx->listStats[mDpy].yuvIndices[0];
    int hw_w = ctx->dpyAttr[mDpy].xres;
    mModeOn = false;

    if(hw_w > MAX_DISPLAY_DIM) {
       ALOGD_IF(VIDEO_DEBUG,"%s, \
                      Cannot use video path for High Res Panels", __FUNCTION__);
       return false;
    }

    if(!ctx->mMDP.hasOverlay) {
       ALOGD_IF(VIDEO_DEBUG,"%s, this hw doesnt support overlay", __FUNCTION__);
       return false;
    }

    if(isSecuring(ctx)) {
       ALOGD_IF(VIDEO_DEBUG,"%s: MDP Secure is active", __FUNCTION__);
       return false;
    }

    if(yuvIndex == -1 || ctx->listStats[mDpy].yuvCount != 1) {
        return false;
    }

    //index guaranteed to be not -1 at this point
    hwc_layer_1_t *layer = &list->hwLayers[yuvIndex];
    if (isSecureModePolicy(ctx->mMDP.version)) {
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(ctx->mSecureMode) {
            if (! isSecureBuffer(hnd)) {
                ALOGD_IF(VIDEO_DEBUG, "%s: Handle non-secure video layer"
                         "during secure playback gracefully", __FUNCTION__);
                return false;
            }
        } else {
            if (isSecureBuffer(hnd)) {
                ALOGD_IF(VIDEO_DEBUG, "%s: Handle secure video layer"
                         "during non-secure playback gracefully", __FUNCTION__);
                return false;
            }
        }
    }

    if((layer->transform & HWC_TRANSFORM_ROT_90) && ctx->mDMAInUse) {
        ctx->mDMAInUse = false;
        ALOGD_IF(VIDEO_DEBUG, "%s: Rotator not available since \
                  DMA Pipe(s) are in use",__FUNCTION__);
        return false;
    }

    if(configure(ctx, layer)) {
        markFlags(layer);
        mModeOn = true;
    }

    return mModeOn;
}

void VideoOverlayLowRes::markFlags(hwc_layer_1_t *layer) {
    if(layer) {
        layer->compositionType = HWC_OVERLAY;
        layer->hints |= HWC_HINT_CLEAR_FB;
    }
}

bool VideoOverlayLowRes::configure(hwc_context_t *ctx,
        hwc_layer_1_t *layer) {

    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height,
            ovutils::getMdpFormat(hnd->format), hnd->size);

    //Request a VG pipe
    ovutils::eDest dest = ov.nextPipe(ovutils::OV_MDP_PIPE_VG, mDpy);
    if(dest == ovutils::OV_INVALID) { //None available
        return false;
    }

    mDest = dest;
    ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
    ovutils::eZorder zOrder = ovutils::ZORDER_0;
    ovutils::eIsFg isFg = ovutils::IS_FG_OFF;
    if (ctx->listStats[mDpy].numAppLayers == 1) {
        isFg = ovutils::IS_FG_SET;
    }

    return (configureLowRes(ctx, layer, mDpy, mdpFlags, zOrder, isFg, dest,
            &mRot) == 0 );
}

bool VideoOverlayLowRes::draw(hwc_context_t *ctx,
        hwc_display_contents_1_t *list) {
    if(!mModeOn) {
        return true;
    }

    int yuvIndex = ctx->listStats[mDpy].yuvIndices[0];
    if(yuvIndex == -1) {
        return true;
    }

    private_handle_t *hnd = (private_handle_t *)
            list->hwLayers[yuvIndex].handle;

    overlay::Overlay& ov = *(ctx->mOverlay);
    int fd = hnd->fd;
    uint32_t offset = hnd->offset;
    Rotator *rot = mRot;

    if(rot) {
        if(!rot->queueBuffer(fd, offset))
            return false;
        fd = rot->getDstMemId();
        offset = rot->getDstOffset();
    }

    if (!ov.queueBuffer(fd, offset, mDest)) {
        ALOGE("%s: queueBuffer failed for dpy=%d", __FUNCTION__, mDpy);
        return false;
    }

    return true;
}

bool VideoOverlayLowRes::isModeOn() {
    return mModeOn;
}

//===========VideoOverlayHighRes=========================

VideoOverlayHighRes::VideoOverlayHighRes(const int& dpy): IVideoOverlay(dpy) {}

//Cache stats, figure out the state, config overlay
bool VideoOverlayHighRes::prepare(hwc_context_t *ctx,
        hwc_display_contents_1_t *list) {

    int yuvIndex =  ctx->listStats[mDpy].yuvIndices[0];
    int hw_w = ctx->dpyAttr[mDpy].xres;
    mModeOn = false;

    if(!ctx->mMDP.hasOverlay) {
       ALOGD_IF(VIDEO_DEBUG,"%s, this hw doesnt support overlay", __FUNCTION__);
       return false;
    }

    if(yuvIndex == -1 || ctx->listStats[mDpy].yuvCount != 1) {
        return false;
    }

    //index guaranteed to be not -1 at this point
    hwc_layer_1_t *layer = &list->hwLayers[yuvIndex];
    if(configure(ctx, layer)) {
        markFlags(layer);
        mModeOn = true;
    }

    return mModeOn;
}

void VideoOverlayHighRes::markFlags(hwc_layer_1_t *layer) {
    if(layer) {
        layer->compositionType = HWC_OVERLAY;
        layer->hints |= HWC_HINT_CLEAR_FB;
    }
}

bool VideoOverlayHighRes::configure(hwc_context_t *ctx,
        hwc_layer_1_t *layer) {

    int hw_w = ctx->dpyAttr[mDpy].xres;
    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height,
            ovutils::getMdpFormat(hnd->format), hnd->size);

    //Request a VG pipe
    mDestL = ovutils::OV_INVALID;
    mDestR = ovutils::OV_INVALID;
    hwc_rect_t dst = layer->displayFrame;
    if(dst.left > hw_w/2) {
        mDestR = ov.nextPipe(ovutils::OV_MDP_PIPE_VG, mDpy);
        if(mDestR == ovutils::OV_INVALID)
            return false;
    } else if (dst.right <= hw_w/2) {
        mDestL = ov.nextPipe(ovutils::OV_MDP_PIPE_VG, mDpy);
        if(mDestL == ovutils::OV_INVALID)
            return false;
    } else {
        mDestL = ov.nextPipe(ovutils::OV_MDP_PIPE_VG, mDpy);
        mDestR = ov.nextPipe(ovutils::OV_MDP_PIPE_VG, mDpy);
        if(mDestL == ovutils::OV_INVALID ||
                mDestR == ovutils::OV_INVALID)
            return false;
    }

    ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
    ovutils::eZorder zOrder = ovutils::ZORDER_0;
    ovutils::eIsFg isFg = ovutils::IS_FG_OFF;
    if (ctx->listStats[mDpy].numAppLayers == 1) {
        isFg = ovutils::IS_FG_SET;
    }

    return (configureHighRes(ctx, layer, mDpy, mdpFlags, zOrder, isFg, mDestL,
            mDestR, &mRot) == 0 );
}

bool VideoOverlayHighRes::draw(hwc_context_t *ctx,
        hwc_display_contents_1_t *list) {
    if(!mModeOn) {
        return true;
    }

    int yuvIndex = ctx->listStats[mDpy].yuvIndices[0];
    if(yuvIndex == -1) {
        return true;
    }

    private_handle_t *hnd = (private_handle_t *)
            list->hwLayers[yuvIndex].handle;

    overlay::Overlay& ov = *(ctx->mOverlay);
    int fd = hnd->fd;
    uint32_t offset = hnd->offset;
    Rotator *rot = mRot;

    if(rot) {
        if(!rot->queueBuffer(fd, offset))
            return false;
        fd = rot->getDstMemId();
        offset = rot->getDstOffset();
    }

    if(mDestL != ovutils::OV_INVALID) {
        if (!ov.queueBuffer(fd, offset, mDestL)) {
            ALOGE("%s: queueBuffer failed for dpy=%d's left mixer",
                    __FUNCTION__, mDpy);
            return false;
        }
    }

    if(mDestR != ovutils::OV_INVALID) {
        if (!ov.queueBuffer(fd, offset, mDestR)) {
            ALOGE("%s: queueBuffer failed for dpy=%d's right mixer"
                    , __FUNCTION__, mDpy);
            return false;
        }
    }

    return true;
}

bool VideoOverlayHighRes::isModeOn() {
    return mModeOn;
}

}; //namespace qhwc
