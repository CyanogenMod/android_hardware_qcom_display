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

#include "hwc_utils.h"

namespace qhwc {
// Determine overlay state based on decoded video info
static ovutils::eOverlayState determineOverlayState(hwc_context_t* ctx,
                                                    uint32_t bypassLayer,
                                                    uint32_t format)
{
    ovutils::eOverlayState state = ovutils::OV_CLOSED;

    // Sanity check
    if (!ctx) {
        ALOGE("%s: NULL ctx", __FUNCTION__);
        return state;
    }

    overlay::Overlay& ov = *(ctx->mOverlay);
    state = ov.getState();

    // If there are any bypassLayers, state is based on number of layers
    if ((bypassLayer > 0) && (ctx->hdmiEnabled == EXT_TYPE_NONE)) {
        if (bypassLayer == 1) {
            state = ovutils::OV_BYPASS_1_LAYER;
        } else if (bypassLayer == 2) {
            state = ovutils::OV_BYPASS_2_LAYER;
        } else if (bypassLayer == 3) {
            state = ovutils::OV_BYPASS_3_LAYER;
        }
        return state;
    }

    // RGB is ambiguous for determining overlay state
    if (ovutils::isRgb(ovutils::getMdpFormat(format))) {
        return state;
    }

    // Content type is either 2D or 3D
    uint32_t fmt3D = 0;//XXX: 3D - ovutils::getS3DFormat(format);

    // Determine state based on the external display, content type, and hw type
    if (ctx->hdmiEnabled == EXT_TYPE_HDMI) {
        // External display is HDMI
        if (fmt3D) {
            // Content type is 3D
            if (ovutils::is3DTV()) {
                // TV panel type is 3D
                state = ovutils::OV_3D_VIDEO_ON_3D_TV;
            } else {
                // TV panel type is 2D
                state = ovutils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV;
            }
        } else {
            // Content type is 2D
            if (ovutils::FrameBufferInfo::getInstance()->supportTrueMirroring()) {
                // True UI mirroring is supported
                state = ovutils::OV_2D_TRUE_UI_MIRROR;
            } else {
                // True UI mirroring is not supported
                state = ovutils::OV_2D_VIDEO_ON_PANEL_TV;
            }
        }
    } else if (ctx->hdmiEnabled == EXT_TYPE_WIFI) {
        // External display is Wifi (currently unsupported)
        ALOGE("%s: WIFI external display is unsupported", __FUNCTION__);
        return state;
    } else {
        // No external display (primary panel only)
        if (fmt3D) {
            // Content type is 3D
            if (ovutils::usePanel3D()) {
                // Primary panel type is 3D
                state = ovutils::OV_3D_VIDEO_ON_3D_PANEL;
            } else {
                // Primary panel type is 2D
                state = ovutils::OV_3D_VIDEO_ON_2D_PANEL;
            }
        } else {
            // Content type is 2D
            state = ovutils::OV_2D_VIDEO_ON_PANEL;
        }
    }

    return state;
}

void setOverlayState(hwc_context_t *ctx, ovutils::eOverlayState state)
{
    if (!ctx) {
        ALOGE("%s: NULL ctx", __FUNCTION__);
        return;
    }

    overlay::Overlay *ov = ctx->mOverlay;
    if (!ov) {
        ALOGE("%s: NULL OV object", __FUNCTION__);
        return;
    }
    ov->setState(state);
}

bool prepareOverlay(hwc_context_t *ctx, hwc_layer_t *layer)
{
    bool ret = false;
    if (LIKELY(ctx->mOverlay)) {
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        overlay::Overlay& ov = *(ctx->mOverlay);
        ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

        // Set overlay state
        ovutils::eOverlayState state = determineOverlayState(ctx, 0, info.format);
        setOverlayState(ctx, state);

        ovutils::eDest dest = ovutils::OV_PIPE_ALL;

        // In the true UI mirroring case, video needs to go to OV_PIPE0 (for
        // primary) and OV_PIPE1 (for external)
        if (state == ovutils::OV_2D_TRUE_UI_MIRROR) {
            dest = static_cast<ovutils::eDest>(
                ovutils::OV_PIPE0 | ovutils::OV_PIPE1);
        }

        // Order order order
        // setSource - just setting source
        // setParameter - changes src w/h/f accordingly
        // setCrop - ROI - that is src_rect
        // setPosition - need to do scaling
        // commit - commit changes to mdp driver
        // queueBuffer - not here, happens when draw is called

        ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
        if (hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
            ovutils::setMdpFlags(mdpFlags,
                                 ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
        }

        // FIXME: Use source orientation for TV when source is portrait
        int transform = layer->transform & FINAL_TRANSFORM_MASK;
        ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(transform);

        ovutils::eWait waitFlag = ovutils::NO_WAIT;
        if (ctx->skipComposition == true) {
            waitFlag = ovutils::WAIT;
        }

        ovutils::eIsFg isFgFlag = ovutils::IS_FG_OFF;
        if (ctx->numHwLayers == 1) {
            isFgFlag = ovutils::IS_FG_SET;
        }

        ovutils::PipeArgs parg(mdpFlags,
                               orient,
                               info,
                               waitFlag,
                               ovutils::ZORDER_0,
                               isFgFlag,
                               ovutils::ROT_FLAG_DISABLED);
        ovutils::PipeArgs pargs[ovutils::MAX_PIPES] = { parg, parg, parg };
        ret = ov.setSource(pargs, dest);
        if (!ret) {
            ALOGE("%s: setSource failed", __FUNCTION__);
            return ret;
        }

        const ovutils::Params prms (ovutils::OVERLAY_TRANSFORM, orient);
        ret = ov.setParameter(prms, dest);
        if (!ret) {
            ALOGE("%s: setParameter failed transform %x", __FUNCTION__, orient);
            return ret;
        }

        hwc_rect_t sourceCrop = layer->sourceCrop;
        // x,y,w,h
        ovutils::Dim dcrop(sourceCrop.left, sourceCrop.top, // x, y
                           sourceCrop.right - sourceCrop.left, // w
                           sourceCrop.bottom - sourceCrop.top);// h
        ret = ov.setCrop(dcrop, dest);
        if (!ret) {
            ALOGE("%s: setCrop failed", __FUNCTION__);
            return ret;
        }

        int orientation = 0;
        ovutils::Dim dim;
        hwc_rect_t displayFrame = layer->displayFrame;
        dim.x = displayFrame.left;
        dim.y = displayFrame.top;
        dim.w = (displayFrame.right - displayFrame.left);
        dim.h = (displayFrame.bottom - displayFrame.top);
        dim.o = orientation;

        ret = ov.setPosition(dim, dest);
        if (!ret) {
            ALOGE("%s: setPosition failed", __FUNCTION__);
            return ret;
        }
        if (!ov.commit(dest)) {
            ALOGE("%s: commit fails", __FUNCTION__);
            return false;
        }
    }
    return true;
}

bool drawLayerUsingOverlay(hwc_context_t *ctx, hwc_layer_t *layer)
{
    private_handle_t *hnd = (private_handle_t *)layer->handle;

    // Lock this buffer for read.
    ctx->qbuf->lockAndAdd(hnd);
    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eOverlayState state = ov.getState();

    // Differentiate between states that need to wait for vsync
    switch (state) {
        case ovutils::OV_2D_VIDEO_ON_PANEL_TV:
        case ovutils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
        case ovutils::OV_2D_TRUE_UI_MIRROR:
            // If displaying on both primary and external, must play each
            // pipe individually since wait for vsync needs to be done at
            // the end. Do the following:
            //     - Play external
            //     - Play primary
            //     - Wait for external vsync to be done
            // NOTE: In these states
            //           - primary VG = OV_PIPE0
            //           - external VG = OV_PIPE1
            //           - external RGB = OV_PIPE2
            //             - Only in true UI mirroring case, played by fb

            // Same FD for both primary and external VG pipes
            ov.setMemoryId(hnd->fd, static_cast<ovutils::eDest>(
                    ovutils::OV_PIPE0 | ovutils::OV_PIPE1));

            // Play external
            if (!ov.queueBuffer(hnd->offset, ovutils::OV_PIPE1)) {
                ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                ret = false;
            }

            // Play primary
            if (!ov.queueBuffer(hnd->offset, ovutils::OV_PIPE0)) {
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
            ov.setMemoryId(hnd->fd, ovutils::OV_PIPE_ALL);
            if (!ov.queueBuffer(hnd->offset, ovutils::OV_PIPE_ALL)) {
                ALOGE("%s: queueBuffer failed", __FUNCTION__);
                ret = false;
            }
            break;
    }

    if (!ret) {
        ALOGE("%s: failed", __FUNCTION__);
    }
    return ret;
}

void cleanOverlays(hwc_context_t *ctx )
{
    //XXX: handle for HDMI
    if(0 == ctx->yuvBufferCount)
        setOverlayState(ctx, ovutils::OV_CLOSED);
}
}; //namespace qhwc
