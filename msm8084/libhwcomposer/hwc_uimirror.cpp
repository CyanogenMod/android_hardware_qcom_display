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


// Function to get the orientation of UI on primary.
// When external display is connected, the primary UI is
// fixed to landscape by the phone window manager.
// Return the landscape orientation based on w and hw of primary
int getDeviceOrientation() {
    int orientation = 0;
    //Calculate the rect for primary based on whether the supplied
    //position
    //is within or outside bounds.
    const int fbWidth =
        ovutils::FrameBufferInfo::getInstance()->getWidth();
    const int fbHeight =
        ovutils::FrameBufferInfo::getInstance()->getHeight();
    if(fbWidth >= fbHeight) {
        // landscape panel
        orientation = overlay::utils::OVERLAY_TRANSFORM_0;
    } else {
        // portrait panel
        orientation = overlay::utils::OVERLAY_TRANSFORM_ROT_90;
    }
    return orientation;
}

//Static Members
ovutils::eOverlayState UIMirrorOverlay::sState = ovutils::OV_CLOSED;
bool UIMirrorOverlay::sIsUiMirroringOn = false;

void UIMirrorOverlay::reset() {
    sIsUiMirroringOn = false;
    sState = ovutils::OV_CLOSED;
}

//Prepare the overlay for the UI mirroring
bool UIMirrorOverlay::prepare(hwc_context_t *ctx, hwc_layer_1_t *fblayer) {
    sState = ovutils::OV_CLOSED;
    sIsUiMirroringOn = false;

    if(!ctx->mMDP.hasOverlay) {
        ALOGD_IF(HWC_UI_MIRROR, "%s, this hw doesnt support mirroring",
                __FUNCTION__);
       return false;
    }
    // If external display is active
    if(isExternalActive(ctx)) {
        sState = ovutils::OV_UI_MIRROR;
        configure(ctx, fblayer);
    }
    return sIsUiMirroringOn;
}

// Configure
bool UIMirrorOverlay::configure(hwc_context_t *ctx, hwc_layer_1_t *layer)
{
    if (LIKELY(ctx->mOverlay)) {
        overlay::Overlay& ov = *(ctx->mOverlay);
        // Set overlay state
        ov.setState(sState);
        framebuffer_device_t *fbDev = ctx->mFbDev;
        if(fbDev) {
            private_handle_t *hnd = (private_handle_t *)layer->handle;
            ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);
            // Determine the RGB pipe for UI depending on the state
            ovutils::eDest dest = ovutils::OV_PIPE_ALL;
            if (sState == ovutils::OV_2D_TRUE_UI_MIRROR) {
                // True UI mirroring state: external RGB pipe is OV_PIPE2
                dest = ovutils::OV_PIPE2;
            } else if (sState == ovutils::OV_UI_MIRROR) {
                // UI-only mirroring state: external RGB pipe is OV_PIPE0
                dest = ovutils::OV_PIPE0;
            }

            ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
            if(ctx->mSecureMode) {
                ovutils::setMdpFlags(mdpFlags,
                        ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
            }

            ovutils::PipeArgs parg(mdpFlags,
                    info,
                    ovutils::ZORDER_0,
                    ovutils::IS_FG_OFF,
                    ovutils::ROT_FLAG_ENABLED);
            ovutils::PipeArgs pargs[ovutils::MAX_PIPES] = { parg, parg, parg };
            ov.setSource(pargs, dest);

            hwc_rect_t sourceCrop = layer->sourceCrop;
            // x,y,w,h
            ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top,
                sourceCrop.right - sourceCrop.left,
                sourceCrop.bottom - sourceCrop.top);
            ov.setCrop(dcrop, dest);

            //Get the current orientation on primary panel
            int transform = getDeviceOrientation();
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
                return false;
            }
            sIsUiMirroringOn = true;
        }
    }
    return sIsUiMirroringOn;
}

bool UIMirrorOverlay::draw(hwc_context_t *ctx, hwc_layer_1_t *layer)
{
    if(!sIsUiMirroringOn) {
        return true;
    }
    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eOverlayState state = ov.getState();
    ovutils::eDest dest = ovutils::OV_PIPE_ALL;
    framebuffer_device_t *fbDev = ctx->mFbDev;
    if(fbDev) {
        private_module_t* m = reinterpret_cast<private_module_t*>(
                              fbDev->common.module);
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        switch (state) {
            case ovutils::OV_UI_MIRROR:
                //TODO why is this primary fd
                if (!ov.queueBuffer(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].fd,
                            hnd->offset,  //div by line_length like in PAN?
                            ovutils::OV_PIPE0)) {
                    ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                    ret = false;
                }
                break;
            case ovutils::OV_2D_TRUE_UI_MIRROR:
                if (!ov.queueBuffer(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].fd,
                            hnd->offset,
                            ovutils::OV_PIPE2)) {
                    ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                    ret = false;
                }
                break;

        default:
            break;
        }
    }
    return ret;
}

//---------------------------------------------------------------------
}; //namespace qhwc
