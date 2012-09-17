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

#include "hwc_extonly.h"
#include "external.h"

namespace qhwc {

#define EXTONLY_DEBUG 0

//Static Members
ovutils::eOverlayState ExtOnly::sState = ovutils::OV_CLOSED;
int ExtOnly::sExtCount = 0;
int ExtOnly::sExtIndex = -1;
bool ExtOnly::sIsExtBlock = false;
bool ExtOnly::sIsModeOn = false;

//Cache stats, figure out the state, config overlay
bool ExtOnly::prepare(hwc_context_t *ctx, hwc_display_contents_1_t *list) {
    sIsModeOn = false;
    if(!ctx->mMDP.hasOverlay) {
       ALOGD_IF(EXTONLY_DEBUG,"%s, this hw doesnt support overlay",
            __FUNCTION__);
       return false;
    }
    if(sExtIndex == -1) {
        return false;
    }
    chooseState(ctx);
    //if the state chosen above is CLOSED, skip this block.
    if(sState != ovutils::OV_CLOSED) {
        hwc_layer_1_t *extLayer = &list->hwLayers[sExtIndex];
        if(configure(ctx, extLayer)) {
            markFlags(extLayer);
            sIsModeOn = true;
        }
    }

    ALOGD_IF(EXTONLY_DEBUG, "%s: stats: extCount = %d, extIndex = %d,"
            "IsExtBlock = %d, IsModeOn = %d",
            __func__, sExtCount, sExtIndex,
            sIsExtBlock, sIsModeOn);

    return sIsModeOn;
}

void ExtOnly::chooseState(hwc_context_t *ctx) {
    ALOGD_IF(EXTONLY_DEBUG, "%s: old state = %s", __FUNCTION__,
            ovutils::getStateString(sState));

    ovutils::eOverlayState newState = ovutils::OV_CLOSED;

    if(sExtCount > 0 &&
        ctx->mExtDisplay->getExternalDisplay()) {
            newState = ovutils::OV_DUAL_DISP;
    }

    sState = newState;
    ALOGD_IF(EXTONLY_DEBUG, "%s: new chosen state = %s", __FUNCTION__,
            ovutils::getStateString(sState));
}

void ExtOnly::markFlags(hwc_layer_1_t *layer) {
    switch(sState) {
        case ovutils::OV_DUAL_DISP:
            layer->compositionType = HWC_OVERLAY;
            break;
        default:
            break;
    }
}

bool ExtOnly::configure(hwc_context_t *ctx, hwc_layer_1_t *layer) {

    overlay::Overlay& ov = *(ctx->mOverlay);
    ov.setState(sState);
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);
    ovutils::eIsFg isFgFlag = ovutils::IS_FG_OFF;
    ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
    ovutils::PipeArgs parg(mdpFlags,
            info,
            ovutils::ZORDER_0,
            isFgFlag,
            ovutils::ROT_FLAG_DISABLED);
    ovutils::PipeArgs pargs[ovutils::MAX_PIPES] = { parg, parg, parg };
    ov.setSource(pargs, ovutils::OV_PIPE0);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    // x,y,w,h
    ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
            sourceCrop.right - sourceCrop.left,
            sourceCrop.bottom - sourceCrop.top);
    ov.setCrop(dcrop, ovutils::OV_PIPE0);

    ov.setTransform(0, ovutils::OV_PIPE0);

    //Setting position same as crop
    //FIXME stretch to full screen
    ov.setPosition(dcrop, ovutils::OV_PIPE0);

    if (!ov.commit(ovutils::OV_PIPE0)) {
        ALOGE("%s: commit fails", __FUNCTION__);
        return false;
    }
    return true;
}

bool ExtOnly::draw(hwc_context_t *ctx, hwc_display_contents_1_t *list)
{
    if(!sIsModeOn || sExtIndex == -1) {
        return true;
    }

    private_handle_t *hnd = (private_handle_t *)
            list->hwLayers[sExtIndex].handle;

    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eOverlayState state = ov.getState();

    switch (state) {
        case ovutils::OV_DUAL_DISP:
            // Play external
            if (!ov.queueBuffer(hnd->fd, hnd->offset, ovutils::OV_PIPE0)) {
                ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                ret = false;
            }
            break;
        default:
            ALOGE("%s Unused state %s", __FUNCTION__,
                    ovutils::getStateString(state));
            break;
    }

    return ret;
}

}; //namespace qhwc
