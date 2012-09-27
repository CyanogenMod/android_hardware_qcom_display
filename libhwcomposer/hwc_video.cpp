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

#define FINAL_TRANSFORM_MASK 0x000F

//Static Members
ovutils::eOverlayState VideoOverlay::sState = ovutils::OV_CLOSED;
bool VideoOverlay::sIsModeOn = false;

//Cache stats, figure out the state, config overlay
bool VideoOverlay::prepare(hwc_context_t *ctx, hwc_display_contents_1_t *list,
        int dpy) {
    int yuvIndex =  ctx->listStats[dpy].yuvIndex;

    if(!ctx->mMDP.hasOverlay) {
       ALOGD_IF(VIDEO_DEBUG,"%s, this hw doesnt support overlay", __FUNCTION__);
       return false;
    }
    if(yuvIndex == -1) {
        return false;
    }

    //index guaranteed to be not -1 at this point
    hwc_layer_1_t *yuvLayer = &list->hwLayers[yuvIndex];
    chooseState(ctx, dpy, yuvLayer);
    if(configure(ctx, dpy, yuvLayer)) {
        markFlags(yuvLayer);
        sIsModeOn = true;
    }

    return sIsModeOn;
}

void VideoOverlay::chooseState(hwc_context_t *ctx, int dpy,
        hwc_layer_1_t *yuvLayer) {
    ALOGD_IF(VIDEO_DEBUG, "%s: old state = %s", __FUNCTION__,
            ovutils::getStateString(sState));

    private_handle_t *hnd = NULL;
    if(yuvLayer) {
        hnd = (private_handle_t *)yuvLayer->handle;
    }
    ovutils::eOverlayState newState = ovutils::OV_CLOSED;
    switch(dpy) {
        case HWC_DISPLAY_PRIMARY:
            if(ctx->listStats[dpy].yuvCount == 1) {
                newState = isExternalActive(ctx) ?
                    ovutils::OV_2D_VIDEO_ON_PANEL_TV : ovutils::OV_2D_VIDEO_ON_PANEL;
                if(isSkipLayer(yuvLayer) && !isSecureBuffer(hnd)) {
                    newState = isExternalActive(ctx) ?
                        ovutils::OV_2D_VIDEO_ON_TV : ovutils::OV_CLOSED;
                }
            }
            break;
        case HWC_DISPLAY_EXTERNAL:
        //TODO needs overlay state change for UI also
            newState = sState; //Previously set by HWC_DISPLAY_PRIMARY
            /*if(ctx->listStats[dpy].yuvCount == 1 && isExternalActive(ctx)) {
                if(!isSkipLayer(yuvLayer) || isSecureBuffer(hnd)) {
                    switch(sState) { //set by primary chooseState
                        case ovutils::OV_2D_VIDEO_ON_PANEL:
                            //upgrade
                            sState = ovutils::OV_2D_VIDEO_PANEL_TV;
                            break;
                        case ovutils::OV_CLOSED:
                            sState = ovutils::OV_2D_VIDEO_ON_TV;
                            break;
                    }
                }
            }*/
            break;
        default:
            break;
    }

    sState = newState;
    ALOGD_IF(VIDEO_DEBUG, "%s: new chosen state = %s", __FUNCTION__,
            ovutils::getStateString(sState));
}

void VideoOverlay::markFlags(hwc_layer_1_t *yuvLayer) {
    if(yuvLayer) {
        yuvLayer->compositionType = HWC_OVERLAY;
        yuvLayer->hints |= HWC_HINT_CLEAR_FB;
    }
}

/* Helpers */
bool configPrimVid(hwc_context_t *ctx, hwc_layer_1_t *layer) {
    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

    ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
    if (isSecureBuffer(hnd)) {
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
    ov.setSource(pargs, ovutils::OV_PIPE0);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    hwc_rect_t displayFrame = layer->displayFrame;

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
    }

    // source crop x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);
    //Only for Primary
    ov.setCrop(dcrop, ovutils::OV_PIPE0);

    int transform = layer->transform;
    ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(transform);
    ov.setTransform(orient, ovutils::OV_PIPE0);

    // position x,y,w,h
    ovutils::Dim dpos(displayFrame.left,
            displayFrame.top,
            displayFrame.right - displayFrame.left,
            displayFrame.bottom - displayFrame.top);
    ov.setPosition(dpos, ovutils::OV_PIPE0);

    if (!ov.commit(ovutils::OV_PIPE0)) {
        ALOGE("%s: commit fails", __FUNCTION__);
        return false;
    }
    return true;
}

bool configExtVid(hwc_context_t *ctx, hwc_layer_1_t *layer) {
    overlay::Overlay& ov = *(ctx->mOverlay);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

    ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
    if (isSecureBuffer(hnd)) {
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
            ovutils::ROT_FLAG_ENABLED); //TODO remove this hack when sync for
            //ext is done
    ovutils::PipeArgs pargs[ovutils::MAX_PIPES] = { parg, parg, parg };
    ov.setSource(pargs, ovutils::OV_PIPE1);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    // x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);
    //Only for External
    ov.setCrop(dcrop, ovutils::OV_PIPE1);

    // FIXME: Use source orientation for TV when source is portrait
    //Only for External
    ov.setTransform(0, ovutils::OV_PIPE1);

    ovutils::Dim dpos;
    hwc_rect_t displayFrame = layer->displayFrame;
    dpos.x = displayFrame.left;
    dpos.y = displayFrame.top;
    dpos.w = (displayFrame.right - displayFrame.left);
    dpos.h = (displayFrame.bottom - displayFrame.top);

    //Only for External
    ov.setPosition(dpos, ovutils::OV_PIPE1);

    if (!ov.commit(ovutils::OV_PIPE1)) {
        ALOGE("%s: commit fails", __FUNCTION__);
        return false;
    }
    return true;
}

bool VideoOverlay::configure(hwc_context_t *ctx, int dpy,
        hwc_layer_1_t *yuvLayer) {
    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    switch(dpy) {
        case HWC_DISPLAY_PRIMARY:
            // Set overlay state
            ov.setState(sState);
            switch(sState) {
                case ovutils::OV_2D_VIDEO_ON_PANEL:
                    ret &= configPrimVid(ctx, yuvLayer);
                    break;
                case ovutils::OV_2D_VIDEO_ON_PANEL_TV:
                    ret &= configPrimVid(ctx, yuvLayer);
                    ret &= configExtVid(ctx, yuvLayer);
                    break;
                case ovutils::OV_2D_VIDEO_ON_TV:
                    ret &= configExtVid(ctx, yuvLayer);
                    break;
                default:
                    return false;
            }
            break;
        case HWC_DISPLAY_EXTERNAL:
            ov.setState(sState);
            switch(sState) {
                case ovutils::OV_2D_VIDEO_ON_PANEL_TV:
                case ovutils::OV_2D_VIDEO_ON_TV:
                    ret = configExtVid(ctx, yuvLayer);
                    break;
                default:
                    return false;
            }
            break;
    }
    return ret;
}

bool VideoOverlay::draw(hwc_context_t *ctx, hwc_display_contents_1_t *list,
        int dpy)
{
    int yuvIndex = ctx->listStats[dpy].yuvIndex;
    if(!sIsModeOn || yuvIndex == -1) {
        return true;
    }

    private_handle_t *hnd = (private_handle_t *)
            list->hwLayers[yuvIndex].handle;

    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eOverlayState state = ov.getState();

    switch(dpy) {
        case HWC_DISPLAY_PRIMARY:
            switch (state) {
                case ovutils::OV_2D_VIDEO_ON_PANEL:
                    // Play primary
                    if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0)) {
                        ALOGE("%s: queueBuffer failed for primary", __FUNCTION__);
                        ret = false;
                    }
                    break;
                case ovutils::OV_2D_VIDEO_ON_PANEL_TV:
                    if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0)) {
                        ALOGE("%s: queueBuffer failed for primary", __FUNCTION__);
                        ret = false;
                    }
                    // Play external
                    if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE1)) {
                        ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                        ret = false;
                    }
                    break;
                case ovutils::OV_2D_VIDEO_ON_TV:
                    // Play external
                    if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE1)) {
                        ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                        ret = false;
                    }
                    break;
                default:
                    ret = false;
                    break;
            }
            break;
        case HWC_DISPLAY_EXTERNAL:
            switch(state) {
                case ovutils::OV_2D_VIDEO_ON_PANEL_TV:
                case ovutils::OV_2D_VIDEO_ON_TV:
                    // Play external
                    if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE1)) {
                        ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                        ret = false;
                    }
                    break;
                default:
                    ret = false;
                    break;
            }
            break;
    }
    return ret;
}

}; //namespace qhwc
