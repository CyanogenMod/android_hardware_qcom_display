/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
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

#define HWC_UI_MIRROR 0
#include <gralloc_priv.h>
#include <fb_priv.h>
#include "hwc_uimirror.h"
#include "external.h"

namespace qhwc {

//Static Members
ovutils::eOverlayState UIMirrorOverlay::sState = ovutils::OV_CLOSED;
bool UIMirrorOverlay::sIsUiMirroringOn = false;

void UIMirrorOverlay::reset() {
    sIsUiMirroringOn = false;
    sState = ovutils::OV_CLOSED;
}

//Prepare the overlay for the UI mirroring
bool UIMirrorOverlay::prepare(hwc_context_t *ctx, hwc_layer_1_t *fblayer) {
    reset();

    if(!ctx->mMDP.hasOverlay) {
        ALOGD_IF(HWC_UI_MIRROR, "%s, this hw doesnt support mirroring",
                __FUNCTION__);
       return false;
    }

    sState = ovutils::OV_UI_MIRROR;
    ovutils::eOverlayState newState = ctx->mOverlay[HWC_DISPLAY_EXTERNAL]->getState();
    if(newState == ovutils::OV_UI_VIDEO_TV) {
        sState = newState;
    }

    configure(ctx, fblayer);
    return sIsUiMirroringOn;
}

// Configure
bool UIMirrorOverlay::configure(hwc_context_t *ctx, hwc_layer_1_t *layer)
{
    if (LIKELY(ctx->mOverlay[HWC_DISPLAY_EXTERNAL])) {
        overlay::Overlay& ov = *(ctx->mOverlay[HWC_DISPLAY_EXTERNAL]);
        // Set overlay state
        ov.setState(sState);
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if (!hnd) {
            ALOGE("%s:NULL private handle for layer!", __FUNCTION__);
            return false;
        }
        ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);
        // Determine the RGB pipe for UI depending on the state
        ovutils::eDest dest = ovutils::OV_PIPE0;

        ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
        if(ctx->mSecureMode) {
            ovutils::setMdpFlags(mdpFlags,
                    ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
        }

        ovutils::PipeArgs parg(mdpFlags,
                info,
                ovutils::ZORDER_0,
                ovutils::IS_FG_OFF,
                ovutils::ROT_FLAG_DISABLED);
        ovutils::PipeArgs pargs[ovutils::MAX_PIPES] = { parg, parg, parg };
        ov.setSource(pargs, dest);

        hwc_rect_t sourceCrop = layer->sourceCrop;
        // x,y,w,h
        ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
                sourceCrop.right - sourceCrop.left,
                sourceCrop.bottom - sourceCrop.top);
        ov.setCrop(dcrop, dest);

        int transform = layer->transform;
        ovutils::eTransform orient =
                static_cast<ovutils::eTransform>(transform);
        ov.setTransform(orient, dest);

        hwc_rect_t displayFrame = layer->displayFrame;
        ovutils::Dim dpos(displayFrame.left,
                displayFrame.top,
                displayFrame.right - displayFrame.left,
                displayFrame.bottom - displayFrame.top);
        ov.setPosition(dpos, dest);

        if (!ov.commit(dest)) {
            ALOGE("%s: commit fails", __FUNCTION__);
            sIsUiMirroringOn = false;
            return false;
        }
        sIsUiMirroringOn = true;
    }
    return sIsUiMirroringOn;
}

bool UIMirrorOverlay::draw(hwc_context_t *ctx, hwc_layer_1_t *layer)
{
    if(!sIsUiMirroringOn) {
        return true;
    }
    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay[HWC_DISPLAY_EXTERNAL]);
    ovutils::eOverlayState state = ov.getState();
    ovutils::eDest dest = ovutils::OV_PIPE0;
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    switch (state) {
        case ovutils::OV_UI_MIRROR:
        case ovutils::OV_UI_VIDEO_TV:
            if (!ov.queueBuffer(hnd->fd, hnd->offset, dest)) {
                ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                ret = false;
            }
            break;
        default:
            break;
    }
    return ret;
}

//---------------------------------------------------------------------
}; //namespace qhwc
