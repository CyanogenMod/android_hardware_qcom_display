/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
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
#include "hwc_qbuf.h"
#include "hwc_video.h"
#include "hwc_external.h"

namespace qhwc {

#define FINAL_TRANSFORM_MASK 0x000F

//Static Members
ovutils::eOverlayState VideoOverlay::sState = ovutils::OV_CLOSED;
int VideoOverlay::sYuvCount = 0;
int VideoOverlay::sYuvLayerIndex = -1;
bool VideoOverlay::sIsModeOn = false;
bool VideoOverlay::sIsLayerSkip = false;

//Cache stats, figure out the state, config overlay
bool VideoOverlay::prepare(hwc_context_t *ctx, hwc_layer_list_t *list) {
    sIsModeOn = false;
    if(!ctx->hasOverlay) {
       ALOGD_IF(VIDEO_DEBUG,"%s, this hw doesnt support overlay", __FUNCTION__);
       return false;
    }
    chooseState(ctx);
    //if the state chosen above is CLOSED, skip this block.
    if(sState != ovutils::OV_CLOSED) {
        if(configure(ctx, &list->hwLayers[sYuvLayerIndex])) {
            markFlags(&list->hwLayers[sYuvLayerIndex]);
        }
    }

    ALOGD_IF(VIDEO_DEBUG, "%s: stats: yuvCount = %d, yuvIndex = %d,"
            "IsModeOn = %d, IsSkipLayer = %d", __FUNCTION__, sYuvCount,
            sYuvLayerIndex, sIsModeOn, sIsLayerSkip);

    return sIsModeOn;
}

void VideoOverlay::chooseState(hwc_context_t *ctx) {
    ALOGD_IF(VIDEO_DEBUG, "%s: old state = %s", __FUNCTION__,
            ovutils::getStateString(sState));

    ovutils::eOverlayState newState = ovutils::OV_CLOSED;
    //TODO check if device supports overlay and hdmi

    //Support 1 video layer
    if(sYuvCount == 1) {
        //Skip on primary, display on ext.
        if(sIsLayerSkip && ctx->mExtDisplay->getExternalDisplay()) {
            //TODO
            //VIDEO_ON_TV_ONLY
        } else if(sIsLayerSkip) { //skip on primary, no ext
            newState = ovutils::OV_CLOSED;
        } else if(ctx->mExtDisplay->getExternalDisplay()) {
            //display on both
            newState = ovutils::OV_2D_VIDEO_ON_PANEL_TV;
        } else { //display on primary only
            newState = ovutils::OV_2D_VIDEO_ON_PANEL;
        }
    }
    sState = newState;
    ALOGD_IF(VIDEO_DEBUG, "%s: new chosen state = %s", __FUNCTION__,
            ovutils::getStateString(sState));
}

void VideoOverlay::markFlags(hwc_layer_t *layer) {
    switch(sState) {
        case ovutils::OV_2D_VIDEO_ON_PANEL:
        case ovutils::OV_2D_VIDEO_ON_PANEL_TV:
            layer->compositionType = HWC_OVERLAY;
            layer->hints |= HWC_HINT_CLEAR_FB;
            break;
        //TODO
        //case ovutils::OV_2D_VIDEO_ON_TV:
            //just break, dont update flags.
        default:
            break;
    }
}

bool VideoOverlay::configure(hwc_context_t *ctx, hwc_layer_t *layer)
{
    if (LIKELY(ctx->mOverlay)) {

        overlay::Overlay& ov = *(ctx->mOverlay);
        // Set overlay state
        ov.setState(sState);

        private_handle_t *hnd = (private_handle_t *)layer->handle;
        ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

        //TODO change this based on state.
        ovutils::eDest dest = ovutils::OV_PIPE_ALL;

        ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
        if (hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
            ovutils::setMdpFlags(mdpFlags,
                                 ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
        }

        ovutils::eIsFg isFgFlag = ovutils::IS_FG_OFF;
        if (ctx->numHwLayers == 1) {
            isFgFlag = ovutils::IS_FG_SET;
        }

        ovutils::PipeArgs parg(mdpFlags,
                               info,
                               ovutils::ZORDER_0,
                               isFgFlag,
                               ovutils::ROT_FLAG_DISABLED);
        ovutils::PipeArgs pargs[ovutils::MAX_PIPES] = { parg, parg, parg };
        ov.setSource(pargs, dest);

        hwc_rect_t sourceCrop = layer->sourceCrop;
        // x,y,w,h
        ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
                           sourceCrop.right - sourceCrop.left,
                           sourceCrop.bottom - sourceCrop.top);
        //Only for External
        ov.setCrop(dcrop, ovutils::OV_PIPE1);

        // FIXME: Use source orientation for TV when source is portrait
        //Only for External
        ov.setTransform(0, dest);

        ovutils::Dim dpos;
        hwc_rect_t displayFrame = layer->displayFrame;
        dpos.x = displayFrame.left;
        dpos.y = displayFrame.top;
        dpos.w = (displayFrame.right - displayFrame.left);
        dpos.h = (displayFrame.bottom - displayFrame.top);

        //Only for External
        ov.setPosition(dpos, ovutils::OV_PIPE1);

        //Calculate the rect for primary based on whether the supplied position
        //is within or outside bounds.
        const int fbWidth =
            ovutils::FrameBufferInfo::getInstance()->getWidth();
        const int fbHeight =
            ovutils::FrameBufferInfo::getInstance()->getHeight();

        if( displayFrame.left < 0 ||
            displayFrame.top < 0 ||
            displayFrame.right > fbWidth ||
            displayFrame.bottom > fbHeight) {

            calculate_crop_rects(sourceCrop, displayFrame, fbWidth, fbHeight);

            //Update calculated width and height
            dcrop.w = sourceCrop.right - sourceCrop.left;
            dcrop.h = sourceCrop.bottom - sourceCrop.top;

            dpos.w = displayFrame.right - displayFrame.left;
            dpos.h = displayFrame.bottom - displayFrame.top;
        }

        //Only for Primary
        ov.setCrop(dcrop, ovutils::OV_PIPE0);

        int transform = layer->transform & FINAL_TRANSFORM_MASK;
        ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(transform);
        ov.setTransform(orient, ovutils::OV_PIPE0);

        ov.setPosition(dpos, ovutils::OV_PIPE0);

        //Both prim and external
        if (!ov.commit(dest)) {
            ALOGE("%s: commit fails", __FUNCTION__);
            return false;
        }

        sIsModeOn = true;
    }
    return sIsModeOn;
}

bool VideoOverlay::draw(hwc_context_t *ctx, hwc_layer_list_t *list)
{
    if(!sIsModeOn || sYuvLayerIndex == -1) {
        return true;
    }

    private_handle_t *hnd =
            (private_handle_t *)list->hwLayers[sYuvLayerIndex].handle;

    // Lock this buffer for read.
    ctx->qbuf->lockAndAdd(hnd);
    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eOverlayState state = ov.getState();

    switch (state) {
        case ovutils::OV_2D_VIDEO_ON_PANEL_TV:
        case ovutils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            // Play external
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE1)) {
                ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                ret = false;
            }

            // Play primary
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0)) {
                ALOGE("%s: queueBuffer failed for primary", __FUNCTION__);
                ret = false;
            }

            // Wait for external vsync to be done
            if (!ov.waitForVsync(ovutils::OV_PIPE1)) {
                ALOGE("%s: waitForVsync failed for external", __FUNCTION__);
                ret = false;
            }
            break;
        default:
            // In most cases, displaying only to one (primary or external)
            // so use OV_PIPE_ALL since overlay will ignore NullPipes
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE_ALL)) {
                ALOGE("%s: queueBuffer failed", __FUNCTION__);
                ret = false;
            }
            break;
    }

    return ret;
}


}; //namespace qhwc
