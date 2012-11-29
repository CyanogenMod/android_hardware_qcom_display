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

#define HWC_FB_UPDATE 0
#include <gralloc_priv.h>
#include <fb_priv.h>
#include "hwc_fbupdate.h"
#include "external.h"

namespace qhwc {

namespace ovutils = overlay::utils;

//Static Members
bool FBUpdate::sModeOn[] = {false};
ovutils::eDest FBUpdate::sDest[] = {ovutils::OV_INVALID};

void FBUpdate::reset() {
    sModeOn[HWC_DISPLAY_PRIMARY] = false;
    sModeOn[HWC_DISPLAY_EXTERNAL] = false;
    sDest[HWC_DISPLAY_PRIMARY] = ovutils::OV_INVALID;
    sDest[HWC_DISPLAY_EXTERNAL] = ovutils::OV_INVALID;
}

bool FBUpdate::prepare(hwc_context_t *ctx, hwc_layer_1_t *fblayer, int dpy) {
    if(!ctx->mMDP.hasOverlay) {
        ALOGD_IF(HWC_FB_UPDATE, "%s, this hw doesnt support mirroring",
                __FUNCTION__);
       return false;
    }

    return (sModeOn[dpy] = configure(ctx, fblayer, dpy));

}

// Configure
bool FBUpdate::configure(hwc_context_t *ctx, hwc_layer_1_t *layer, int dpy)
{
    bool ret = false;
    if (LIKELY(ctx->mOverlay)) {
        overlay::Overlay& ov = *(ctx->mOverlay);
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if (!hnd) {
            ALOGE("%s:NULL private handle for layer!", __FUNCTION__);
            return false;
        }
        ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

        //Request an RGB pipe
        ovutils::eDest dest = ov.nextPipe(ovutils::OV_MDP_PIPE_RGB, dpy);
        if(dest == ovutils::OV_INVALID) { //None available
            return false;
        }

        sDest[dpy] = dest;

        ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
        if(ctx->mSecureMode) {
            ovutils::setMdpFlags(mdpFlags,
                    ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
        }

        ovutils::PipeArgs parg(mdpFlags,
                info,
                ovutils::ZORDER_0,
                ovutils::IS_FG_SET,
                ovutils::ROT_FLAG_DISABLED);
        ov.setSource(parg, dest);

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

        ret = true;
        if (!ov.commit(dest)) {
            ALOGE("%s: commit fails", __FUNCTION__);
            ret = false;
        }
    }
    return ret;
}

bool FBUpdate::draw(hwc_context_t *ctx, hwc_layer_1_t *layer, int dpy)
{
    if(!sModeOn[dpy]) {
        return true;
    }
    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eDest dest = sDest[dpy];
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if (!ov.queueBuffer(hnd->fd, hnd->offset, dest)) {
        ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
        ret = false;
    }
    return ret;
}

//---------------------------------------------------------------------
}; //namespace qhwc
