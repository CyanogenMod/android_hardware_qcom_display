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
ovutils::eOverlayState VideoOverlay::sState[] = {ovutils::OV_CLOSED};
bool VideoOverlay::sIsModeOn[] = {false};

//Cache stats, figure out the state, config overlay
bool VideoOverlay::prepare(hwc_context_t *ctx, hwc_display_contents_1_t *list,
        int dpy) {

    int yuvIndex =  ctx->listStats[dpy].yuvIndex;
    sIsModeOn[dpy] = false;

    if(!ctx->mMDP.hasOverlay) {
       ALOGD_IF(VIDEO_DEBUG,"%s, this hw doesnt support overlay", __FUNCTION__);
       return false;
    }

    if(yuvIndex == -1 || ctx->listStats[dpy].yuvCount != 1) {
        return false;
    }

    //index guaranteed to be not -1 at this point
    hwc_layer_1_t *yuvLayer = &list->hwLayers[yuvIndex];

    private_handle_t *hnd = (private_handle_t *)yuvLayer->handle;
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
    chooseState(ctx, dpy, yuvLayer);
    if(configure(ctx, dpy, yuvLayer)) {
        markFlags(yuvLayer);
        sIsModeOn[dpy] = true;
    }

    return sIsModeOn[dpy];
}

void VideoOverlay::chooseState(hwc_context_t *ctx, int dpy,
        hwc_layer_1_t *yuvLayer) {
    ALOGD_IF(VIDEO_DEBUG, "%s: old state = %s", __FUNCTION__,
            ovutils::getStateString(sState[dpy]));

    private_handle_t *hnd = NULL;
    if(yuvLayer) {
        hnd = (private_handle_t *)yuvLayer->handle;
    }
    ovutils::eOverlayState newState = ovutils::OV_CLOSED;
    switch(dpy) {
        case HWC_DISPLAY_PRIMARY:
            if(ctx->listStats[dpy].yuvCount == 1) {
                newState = ovutils::OV_2D_VIDEO_ON_PANEL;
                if(isSkipLayer(yuvLayer) && !isSecureBuffer(hnd)) {
                    newState = ovutils::OV_CLOSED;
                }
            }
            break;
        case HWC_DISPLAY_EXTERNAL:
            newState = ctx->mOverlay[HWC_DISPLAY_EXTERNAL]->getState(); //If we are here, external is active
            if(ctx->listStats[dpy].yuvCount == 1) {
                if(!isSkipLayer(yuvLayer) || isSecureBuffer(hnd)) {
                    newState = ovutils::OV_UI_VIDEO_TV;
                }
            }
            break;
        default:
            break;
    }

    sState[dpy] = newState;
    ALOGD_IF(VIDEO_DEBUG, "%s: new chosen state = %s", __FUNCTION__,
            ovutils::getStateString(sState[dpy]));
}

void VideoOverlay::markFlags(hwc_layer_1_t *yuvLayer) {
    if(yuvLayer) {
        yuvLayer->compositionType = HWC_OVERLAY;
        yuvLayer->hints |= HWC_HINT_CLEAR_FB;
    }
}

/* Helpers */
bool configPrimVid(hwc_context_t *ctx, hwc_layer_1_t *layer) {
    overlay::Overlay& ov = *(ctx->mOverlay[HWC_DISPLAY_PRIMARY]);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

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
    if (ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers == 1) {
        isFgFlag = ovutils::IS_FG_SET;
    }

    ovutils::PipeArgs parg(mdpFlags,
            info,
            ovutils::ZORDER_0,
            isFgFlag,
            ovutils::ROT_FLAG_DISABLED);
    ovutils::PipeArgs pargs[ovutils::MAX_PIPES] = { parg, parg, parg };
    ov.setSource(pargs, ovutils::OV_PIPE0);

    int transform = layer->transform & FINAL_TRANSFORM_MASK;
    ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(transform);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    hwc_rect_t displayFrame = layer->displayFrame;

    //Calculate the rect for primary based on whether the supplied position
    //is within or outside bounds.
    const int fbWidth = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
    const int fbHeight = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres;

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
    ov.setCrop(dcrop, ovutils::OV_PIPE0);

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
    overlay::Overlay& ov = *(ctx->mOverlay[HWC_DISPLAY_EXTERNAL]);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

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
    if (ctx->listStats[HWC_DISPLAY_EXTERNAL].numAppLayers == 1) {
        isFgFlag = ovutils::IS_FG_SET;
    }

    ovutils::PipeArgs parg(mdpFlags,
            info,
            ovutils::ZORDER_1,
            isFgFlag,
            ovutils::ROT_FLAG_DISABLED);
    ovutils::PipeArgs pargs[ovutils::MAX_PIPES] = { parg, parg, parg };
    ov.setSource(pargs, ovutils::OV_PIPE1);

    int transform = layer->transform;
    ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(transform);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    hwc_rect_t displayFrame = layer->displayFrame;

    //Calculate the rect for primary based on whether the supplied position
    //is within or outside bounds.
    const int fbWidth = ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].xres;
    const int fbHeight = ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].yres;

    if( displayFrame.left < 0 ||
            displayFrame.top < 0 ||
            displayFrame.right > fbWidth ||
            displayFrame.bottom > fbHeight) {
        calculate_crop_rects(sourceCrop, displayFrame, fbWidth, fbHeight,
                transform);
    }

    // x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);
    //Only for External
    ov.setCrop(dcrop, ovutils::OV_PIPE1);

    ov.setTransform(orient, ovutils::OV_PIPE1);

    ovutils::Dim dpos(displayFrame.left,
            displayFrame.top,
            (displayFrame.right - displayFrame.left),
            (displayFrame.bottom - displayFrame.top));

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
    overlay::Overlay& ov = *(ctx->mOverlay[dpy]);
    switch(dpy) {
        case HWC_DISPLAY_PRIMARY:
            // Set overlay state
            ov.setState(sState[dpy]);
            switch(sState[dpy]) {
                case ovutils::OV_2D_VIDEO_ON_PANEL:
                    ret &= configPrimVid(ctx, yuvLayer);
                    break;
                default:
                    return false;
            }
            break;
        case HWC_DISPLAY_EXTERNAL:
            ov.setState(sState[dpy]);
            switch(sState[dpy]) {
                case ovutils::OV_UI_VIDEO_TV:
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
    overlay::Overlay& ov = *(ctx->mOverlay[dpy]);
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
                default:
                    ret = false;
                    break;
            }
            break;
        case HWC_DISPLAY_EXTERNAL:
            switch(state) {
                case ovutils::OV_UI_VIDEO_TV:
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
