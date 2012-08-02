/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
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
#include "hwc_external.h"

namespace qhwc {


// Function to get the primary device orientation
// Loops thru the hardware layers and returns the orientation of the max.
// number of layers
int getDeviceOrientation(hwc_context_t* ctx,  hwc_layer_list_t *list) {
    int orientation =  list->hwLayers[0].transform;
    if(!ctx) {
         ALOGD_IF(HWC_UI_MIRROR, "In %s: ctx is NULL!!", __FUNCTION__);
        return -1;
    }
    for(size_t i=0; i <= list->numHwLayers;i++ )
    {
        for(size_t j=i+1; j <= list->numHwLayers; j++)
        {
            // Should we not check for the video layer orientation as it might
            // source orientation(?)
            if(list->hwLayers[i].transform == list->hwLayers[j].transform)
            {
                orientation = list->hwLayers[i].transform;
            }
        }
    }
    return orientation;
}

//Static Members
ovutils::eOverlayState UIMirrorOverlay::sState = ovutils::OV_CLOSED;
bool UIMirrorOverlay::sIsUiMirroringOn = false;


//Prepare the overlay for the UI mirroring
bool UIMirrorOverlay::prepare(hwc_context_t *ctx, hwc_layer_list_t *list) {
    sState = ovutils::OV_CLOSED;
    sIsUiMirroringOn = false;
    // If external display is connected
    if(ctx->mExtDisplay->getExternalDisplay()) {
        sState = ovutils::OV_UI_MIRROR;
        configure(ctx, list);
    }
    return sIsUiMirroringOn;
}

// Configure
bool UIMirrorOverlay::configure(hwc_context_t *ctx, hwc_layer_list_t *list)
{
    if (LIKELY(ctx->mOverlay)) {
        overlay::Overlay& ov = *(ctx->mOverlay);
        // Set overlay state
        ov.setState(sState);
        framebuffer_device_t *fbDev = ctx->mFbDev;
        if(fbDev) {
            private_module_t* m = reinterpret_cast<private_module_t*>(
                    fbDev->common.module);
            int alignedW = ALIGN_TO(m->info.xres, 32);

            private_handle_t const* hnd =
                    reinterpret_cast<private_handle_t const*>(m->framebuffer);
            unsigned int size = hnd->size/m->numBuffers;
            ovutils::Whf info(alignedW, hnd->height, hnd->format, size);
            // Determine the RGB pipe for UI depending on the state
            ovutils::eDest dest = ovutils::OV_PIPE_ALL;
            if (sState == ovutils::OV_2D_TRUE_UI_MIRROR) {
                // True UI mirroring state: external RGB pipe is OV_PIPE2
                dest = ovutils::OV_PIPE2;
            } else if (sState == ovutils::OV_UI_MIRROR) {
                // UI-only mirroring state: external RGB pipe is OV_PIPE0
                dest = ovutils::OV_PIPE0;
            }

            ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_MEMORY_ID_TYPE_FB;
            /* - TODO: Secure content
               if (hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
               ovutils::setMdpFlags(mdpFlags,
               ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
               }
             */

            ovutils::PipeArgs parg(mdpFlags,
                    info,
                    ovutils::ZORDER_0,
                    ovutils::IS_FG_OFF,
                    ovutils::ROT_FLAG_ENABLED);
            ovutils::PipeArgs pargs[ovutils::MAX_PIPES] = { parg, parg, parg };
            ov.setSource(pargs, dest);

            // x,y,w,h
            ovutils::Dim dcrop(0, 0, m->info.xres, m->info.yres);
            ov.setCrop(dcrop, dest);
            //Get the current orientation on primary panel
            int transform = getDeviceOrientation(ctx, list);
            ovutils::eTransform orient =
                    static_cast<ovutils::eTransform>(transform);
            ov.setTransform(orient, dest);

            ovutils::Dim dim;
            dim.x = 0;
            dim.y = 0;
            dim.w = m->info.xres;
            dim.h = m->info.yres;
            ov.setPosition(dim, dest);
            if (!ov.commit(dest)) {
                ALOGE("%s: commit fails", __FUNCTION__);
                return false;
            }
            sIsUiMirroringOn = true;
        }
    }
    return sIsUiMirroringOn;
}

bool UIMirrorOverlay::draw(hwc_context_t *ctx)
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
        //wait for the fb_post to be called
        pthread_mutex_lock(&m->fbPostLock);
        while(m->fbPostDone == false) {
            pthread_cond_wait(&(m->fbPostCond), &(m->fbPostLock));
        }
        pthread_mutex_unlock(&m->fbPostLock);
        switch (state) {
            case ovutils::OV_UI_MIRROR:
                if (!ov.queueBuffer(m->framebuffer->fd, m->currentOffset,
                                                           ovutils::OV_PIPE0)) {
                    ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                }
                break;
            case ovutils::OV_2D_TRUE_UI_MIRROR:
                if (!ov.queueBuffer(m->framebuffer->fd, m->currentOffset,
                                                           ovutils::OV_PIPE2)) {
                    ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                }
                break;

        default:
            break;
        }
        // TODO:
        // Call PANDISPLAY ioctl here to kickoff
    }
    return ret;
}

//---------------------------------------------------------------------
}; //namespace qhwc
