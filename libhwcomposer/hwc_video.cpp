/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
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

namespace qhwc {

namespace ovutils = overlay::utils;

//Static Members
bool VideoOverlay::sIsModeOn[] = {false};
ovutils::eDest VideoOverlay::sDest[] = {ovutils::OV_INVALID};

//Cache stats, figure out the state, config overlay
bool VideoOverlay::prepare(hwc_context_t *ctx, hwc_display_contents_1_t *list,
        int dpy) {

    int yuvIndex =  ctx->listStats[dpy].yuvIndex;
    sIsModeOn[dpy] = false;

    if(!ctx->mMDP.hasOverlay) {
       ALOGD_IF(VIDEO_DEBUG,"%s, this hw doesnt support overlay", __FUNCTION__);
       return false;
    }

    if(isSecuring(ctx)) {
       ALOGD_IF(VIDEO_DEBUG,"%s: MDP Secure is active", __FUNCTION__);
       return false;
    }

    if(yuvIndex == -1 || ctx->listStats[dpy].yuvCount != 1) {
        return false;
    }

    //index guaranteed to be not -1 at this point
    hwc_layer_1_t *layer = &list->hwLayers[yuvIndex];

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
    if(configure(ctx, dpy, layer)) {
        markFlags(layer);
        sIsModeOn[dpy] = true;
    }

    return sIsModeOn[dpy];
}

void VideoOverlay::markFlags(hwc_layer_1_t *layer) {
    if(layer) {
        layer->compositionType = HWC_OVERLAY;
        layer->hints |= HWC_HINT_CLEAR_FB;
    }
}

bool VideoOverlay::configure(hwc_context_t *ctx, int dpy,
        hwc_layer_1_t *layer) {
    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

    //Request a VG pipe
    ovutils::eDest dest = ov.nextPipe(ovutils::OV_MDP_PIPE_VG, dpy);
    if(dest == ovutils::OV_INVALID) { //None available
        return false;
    }

    sDest[dpy] = dest;

    ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
    if (isSecureBuffer(hnd)) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
    }

    if(layer->blending == HWC_BLENDING_PREMULT) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_BLEND_FG_PREMULT);
    }

    ovutils::eIsFg isFgFlag = ovutils::IS_FG_OFF;
    if (ctx->listStats[dpy].numAppLayers == 1) {
        isFgFlag = ovutils::IS_FG_SET;
    }

    ovutils::PipeArgs parg(mdpFlags,
            info,
            ovutils::ZORDER_1,
            isFgFlag,
            ovutils::ROT_FLAG_DISABLED);

    ov.setSource(parg, dest);

    int transform = layer->transform;
    ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(transform);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    hwc_rect_t displayFrame = layer->displayFrame;

    //Calculate the rect for primary based on whether the supplied position
    //is within or outside bounds.
    const int fbWidth = ctx->dpyAttr[dpy].xres;
    const int fbHeight = ctx->dpyAttr[dpy].yres;

    if( displayFrame.left < 0 ||
            displayFrame.top < 0 ||
            displayFrame.right > fbWidth ||
            displayFrame.bottom > fbHeight) {
        calculate_crop_rects(sourceCrop, displayFrame, fbWidth, fbHeight,
                transform);
    }

    // source crop x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);
    //Only for Primary
    ov.setCrop(dcrop, dest);

    ov.setTransform(orient, dest);

    // position x,y,w,h
    ovutils::Dim dpos(displayFrame.left,
            displayFrame.top,
            displayFrame.right - displayFrame.left,
            displayFrame.bottom - displayFrame.top);
    ov.setPosition(dpos, dest);

    if (!ov.commit(dest)) {
        ALOGE("%s: commit fails", __FUNCTION__);
        return false;
    }
    return true;
}

bool VideoOverlay::draw(hwc_context_t *ctx, hwc_display_contents_1_t *list,
        int dpy)
{
    if(!sIsModeOn[dpy]) {
        return true;
    }

    int yuvIndex = ctx->listStats[dpy].yuvIndex;
    if(yuvIndex == -1) {
        return true;
    }

    private_handle_t *hnd = (private_handle_t *)
            list->hwLayers[yuvIndex].handle;

    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);

    if (!ov.queueBuffer(hnd->fd, hnd->offset,
                sDest[dpy])) {
        ALOGE("%s: queueBuffer failed for dpy=%d", __FUNCTION__, dpy);
        ret = false;
    }

    return ret;
}

}; //namespace qhwc
