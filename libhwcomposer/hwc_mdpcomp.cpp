/*
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
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

#include "hwc_mdpcomp.h"
#include <sys/ioctl.h>
#include "external.h"

namespace qhwc {

namespace ovutils = overlay::utils;
/****** Class MDPComp ***********/

MDPComp::eState MDPComp::sMDPCompState = MDPCOMP_OFF;
struct MDPComp::FrameInfo MDPComp::sCurrentFrame;
IdleInvalidator *MDPComp::idleInvalidator = NULL;
bool MDPComp::sIdleFallBack = false;
bool MDPComp::sDebugLogs = false;
bool MDPComp::sEnabled = false;
int MDPComp::sActiveMax = 0;
bool MDPComp::sSecuredVid = false;

bool MDPComp::deinit() {
    //XXX: Tear down MDP comp state
    return true;
}
bool MDPComp::isSkipPresent (hwc_context_t *ctx) {
    return  ctx->listStats[HWC_DISPLAY_PRIMARY].skipCount;
};

bool MDPComp::isYuvPresent (hwc_context_t *ctx) {
    return  ctx->listStats[HWC_DISPLAY_PRIMARY].yuvCount;
};

void MDPComp::timeout_handler(void *udata) {
    struct hwc_context_t* ctx = (struct hwc_context_t*)(udata);

    if(!ctx) {
        ALOGE("%s: received empty data in timer callback", __FUNCTION__);
        return;
    }

    if(!ctx->proc) {
        ALOGE("%s: HWC proc not registered", __FUNCTION__);
        return;
    }
    sIdleFallBack = true;
    /* Trigger SF to redraw the current frame */
    ctx->proc->invalidate(ctx->proc);
}

void MDPComp::reset(hwc_context_t *ctx, hwc_display_contents_1_t* list ) {
    //Reset flags and states
    unsetMDPCompLayerFlags(ctx, list);
    sCurrentFrame.count = 0;
    if(sCurrentFrame.pipeLayer) {
        free(sCurrentFrame.pipeLayer);
        sCurrentFrame.pipeLayer = NULL;
    }
}

void MDPComp::print_info(hwc_layer_1_t* layer)
{
     hwc_rect_t sourceCrop = layer->sourceCrop;
     hwc_rect_t displayFrame = layer->displayFrame;

     int s_l = sourceCrop.left;
     int s_t = sourceCrop.top;
     int s_r = sourceCrop.right;
     int s_b = sourceCrop.bottom;

     int d_l = displayFrame.left;
     int d_t = displayFrame.top;
     int d_r = displayFrame.right;
     int d_b = displayFrame.bottom;

     ALOGD_IF(isDebug(), "src:[%d,%d,%d,%d] (%d x %d) \
                             dst:[%d,%d,%d,%d] (%d x %d)",
                             s_l, s_t, s_r, s_b, (s_r - s_l), (s_b - s_t),
                             d_l, d_t, d_r, d_b, (d_r - d_l), (d_b - d_t));
}

void MDPComp::setVidInfo(hwc_layer_1_t *layer, ovutils::eMdpFlags &mdpFlags) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;

    if(isSecureBuffer(hnd)) {
        ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
        sSecuredVid = true;
    }
}

/*
 * Configures pipe(s) for MDP composition
 */
int MDPComp::prepare(hwc_context_t *ctx, hwc_layer_1_t *layer,
                                            MdpPipeInfo& mdp_info) {

    int nPipeIndex = mdp_info.index;

    if (ctx) {

        private_handle_t *hnd = (private_handle_t *)layer->handle;

        overlay::Overlay& ov = *ctx->mOverlay;

        if(!hnd) {
            ALOGE("%s: layer handle is NULL", __FUNCTION__);
            return -1;
        }

        int hw_w = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
        int hw_h = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres;

        hwc_rect_t sourceCrop = layer->sourceCrop;
        hwc_rect_t displayFrame = layer->displayFrame;

        const int src_w = sourceCrop.right - sourceCrop.left;
        const int src_h = sourceCrop.bottom - sourceCrop.top;

        hwc_rect_t crop = sourceCrop;
        int crop_w = crop.right - crop.left;
        int crop_h = crop.bottom - crop.top;

        hwc_rect_t dst = displayFrame;
        int dst_w = dst.right - dst.left;
        int dst_h = dst.bottom - dst.top;

        if(dst.left < 0 || dst.top < 0 ||
               dst.right > hw_w || dst.bottom > hw_h) {
            ALOGD_IF(isDebug(),"%s: Destination has negative coordinates",
                                                                  __FUNCTION__);
            qhwc::calculate_crop_rects(crop, dst, hw_w, hw_h, 0);

            //Update calulated width and height
            crop_w = crop.right - crop.left;
            crop_h = crop.bottom - crop.top;

            dst_w = dst.right - dst.left;
            dst_h = dst.bottom - dst.top;
        }

        if( (dst_w > hw_w)|| (dst_h > hw_h)) {
            ALOGD_IF(isDebug(),"%s: Dest rect exceeds FB", __FUNCTION__);
            print_info(layer);
            dst_w = hw_w;
            dst_h = hw_h;
        }

        // Determine pipe to set based on pipe index
        ovutils::eDest dest = (ovutils::eDest)mdp_info.index;

        ovutils::eZorder zOrder = ovutils::ZORDER_0;

        if(mdp_info.zOrder == 0 ) {
            zOrder = ovutils::ZORDER_0;
        } else if(mdp_info.zOrder == 1 ) {
            zOrder = ovutils::ZORDER_1;
        } else if(mdp_info.zOrder == 2 ) {
            zOrder = ovutils::ZORDER_2;
        } else if(mdp_info.zOrder == 3) {
            zOrder = ovutils::ZORDER_3;
        }

        // Order order order
        // setSource - just setting source
        // setParameter - changes src w/h/f accordingly
        // setCrop - ROI - src_rect
        // setPosition - dst_rect
        // commit - commit changes to mdp driver
        // queueBuffer - not here, happens when draw is called

        ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);

        ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;

        if(isYuvBuffer(hnd))
            setVidInfo(layer, mdpFlags);

        ovutils::setMdpFlags(mdpFlags,ovutils::OV_MDP_BACKEND_COMPOSITION);

        if(layer->blending == HWC_BLENDING_PREMULT) {
            ovutils::setMdpFlags(mdpFlags,
                    ovutils::OV_MDP_BLEND_FG_PREMULT);
        }

        ovutils::eTransform orient = overlay::utils::OVERLAY_TRANSFORM_0 ;

        if(!(layer->transform & HWC_TRANSFORM_ROT_90)) {
            if(layer->transform & HWC_TRANSFORM_FLIP_H) {
                ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_FLIP_H);
            }

            if(layer->transform & HWC_TRANSFORM_FLIP_V) {
                ovutils::setMdpFlags(mdpFlags,  ovutils::OV_MDP_FLIP_V);
            }
        } else {
            orient = static_cast<ovutils::eTransform>(layer->transform);
        }

        ovutils::PipeArgs parg(mdpFlags,
                               info,
                               zOrder,
                               ovutils::IS_FG_OFF,
                               ovutils::ROT_FLAG_DISABLED);

        ov.setSource(parg, dest);

        ov.setTransform(orient, dest);

        ovutils::Dim dcrop(crop.left, crop.top, crop_w, crop_h);
        ov.setCrop(dcrop, dest);

        ovutils::Dim dim(dst.left, dst.top, dst_w, dst_h);
        ov.setPosition(dim, dest);

        ALOGD_IF(isDebug(),"%s: MDP set: crop[%d,%d,%d,%d] dst[%d,%d,%d,%d] \
                       nPipe: %d zorder: %d",__FUNCTION__, dcrop.x,
                       dcrop.y,dcrop.w, dcrop.h, dim.x, dim.y, dim.w, dim.h,
                       mdp_info.index, mdp_info.zOrder);

        if (!ov.commit(dest)) {
            ALOGE("%s: commit failed", __FUNCTION__);
            return -1;
        }
    }
    return 0;
}

/*
 * MDPComp not possible when
 * 1. We have more than sMaxLayers
 * 2. External display connected
 * 3. Composition is triggered by
 *    Idle timer expiry
 * 4. Rotation is  needed
 * 5. Overlay in use
 */

bool MDPComp::isDoable(hwc_context_t *ctx, hwc_display_contents_1_t* list) {
    //Number of layers
    int numAppLayers = ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers;

    if(numAppLayers < 1 || numAppLayers > sActiveMax) {
        ALOGD_IF(isDebug(), "%s: Unsupported number of layers",__FUNCTION__);
        return false;
    }

    if(isSecuring(ctx)) {
        ALOGD_IF(isDebug(), "%s: MDP securing is active", __FUNCTION__);
        return false;
    }

    if(ctx->mSecureMode)
        return false;

    //Check for skip layers
    if(isSkipPresent(ctx)) {
        ALOGD_IF(isDebug(), "%s: Skip layers are present",__FUNCTION__);
        return false;
    }

    if(ctx->listStats[HWC_DISPLAY_PRIMARY].needsAlphaScale) {
        ALOGD_IF(isDebug(), "%s: frame needs alpha downscaling",__FUNCTION__);
        return false;
    }

    //FB composition on idle timeout
    if(sIdleFallBack) {
        sIdleFallBack = false;
        ALOGD_IF(isDebug(), "%s: idle fallback",__FUNCTION__);
        return false;
    }

    //MDP composition is not efficient if layer needs rotator.
    for(int i = 0; i < numAppLayers; ++i) {
        // As MDP h/w supports flip operation, use MDP comp only for
        // 180 transforms. Fail for any transform involving 90 (90, 270).
        hwc_layer_1_t* layer = &list->hwLayers[i];
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if((layer->transform & HWC_TRANSFORM_ROT_90)  && !isYuvBuffer(hnd)) {
            ALOGD_IF(isDebug(), "%s: orientation involved",__FUNCTION__);
            return false;
        }
    }
    return true;
}

void MDPComp::setMDPCompLayerFlags(hwc_context_t *ctx,
                                hwc_display_contents_1_t* list) {
    LayerProp *layerProp = ctx->layerProp[HWC_DISPLAY_PRIMARY];

    for(int index = 0 ; index < sCurrentFrame.count; index++ ) {
        hwc_layer_1_t* layer = &(list->hwLayers[index]);
        layerProp[index].mFlags |= HWC_MDPCOMP;
        layer->compositionType = HWC_OVERLAY;
        layer->hints |= HWC_HINT_CLEAR_FB;
    }
}

int MDPComp::getMdpPipe(hwc_context_t *ctx, ePipeType type){
    overlay::Overlay& ov = *ctx->mOverlay;
    int mdp_pipe = -1;

    switch(type) {
    case MDPCOMP_OV_ANY:
    case MDPCOMP_OV_RGB:
        mdp_pipe = ov.nextPipe(ovutils::OV_MDP_PIPE_RGB, HWC_DISPLAY_PRIMARY);
        if(mdp_pipe != ovutils::OV_INVALID) {
            return mdp_pipe;
        }

        if(type == MDPCOMP_OV_RGB) {
            //Requested only for RGB pipe
            return -1;
        }
    case  MDPCOMP_OV_VG:
        mdp_pipe = ov.nextPipe(ovutils::OV_MDP_PIPE_VG, HWC_DISPLAY_PRIMARY);
        if(mdp_pipe != ovutils::OV_INVALID) {
            return mdp_pipe;
        }
        return -1;
    default:
        ALOGE("%s: Invalid pipe type",__FUNCTION__);
        return -1;
    };
}

bool MDPComp::allocLayerPipes(hwc_context_t *ctx,
        hwc_display_contents_1_t* list,
        FrameInfo& currentFrame) {

    overlay::Overlay& ov = *ctx->mOverlay;

    int layer_count = ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers;

    currentFrame.count = layer_count;

    currentFrame.pipeLayer = (PipeLayerPair*)
                          malloc(sizeof(PipeLayerPair) * currentFrame.count);

    if(isYuvPresent(ctx)) {
        int nYuvIndex = ctx->listStats[HWC_DISPLAY_PRIMARY].yuvIndex;
        hwc_layer_1_t* layer = &list->hwLayers[nYuvIndex];
        PipeLayerPair& info = currentFrame.pipeLayer[nYuvIndex];
        MdpPipeInfo& pipe_info = info.pipeIndex;
        pipe_info.index = getMdpPipe(ctx, MDPCOMP_OV_VG);
        if(pipe_info.index < 0) {
            ALOGD_IF(isDebug(), "%s: Unable to get pipe for Videos",
                                                          __FUNCTION__);
            return false;
        }
        pipe_info.zOrder = nYuvIndex;
    }

    for(int index = 0 ; index < layer_count ; index++ ) {
        if(index  == ctx->listStats[HWC_DISPLAY_PRIMARY].yuvIndex )
            continue;

        hwc_layer_1_t* layer = &list->hwLayers[index];
        PipeLayerPair& info = currentFrame.pipeLayer[index];
        MdpPipeInfo& pipe_info = info.pipeIndex;
        pipe_info.index = getMdpPipe(ctx, MDPCOMP_OV_ANY);
        if(pipe_info.index < 0) {
            ALOGD_IF(isDebug(), "%s: Unable to get pipe for UI", __FUNCTION__);
            return false;
        }
        pipe_info.zOrder = index;
    }
    return true;
}

bool MDPComp::setup(hwc_context_t* ctx, hwc_display_contents_1_t* list) {
    int nPipeIndex, vsync_wait, isFG;
    int numHwLayers = ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers;

    FrameInfo &currentFrame = sCurrentFrame;
    currentFrame.count = 0;

    if(currentFrame.pipeLayer) {
        free(currentFrame.pipeLayer);
        currentFrame.pipeLayer = NULL;
    }

    if(!ctx) {
       ALOGE("%s: invalid context", __FUNCTION__);
       return -1;
    }

    if(!allocLayerPipes(ctx, list, currentFrame)) {
        //clean current frame data
        currentFrame.count = 0;

        if(currentFrame.pipeLayer) {
            free(currentFrame.pipeLayer);
            currentFrame.pipeLayer = NULL;
        }

        ALOGD_IF(isDebug(), "%s: Falling back to FB", __FUNCTION__);
        return false;
    }

    for (int index = 0 ; index < currentFrame.count; index++) {
        hwc_layer_1_t* layer = &list->hwLayers[index];
        MdpPipeInfo& cur_pipe = currentFrame.pipeLayer[index].pipeIndex;

        if( prepare(ctx, layer, cur_pipe) != 0 ) {
           ALOGD_IF(isDebug(), "%s: MDPComp failed to configure overlay for \
                                    layer %d with pipe index:%d",__FUNCTION__,
                                    index, cur_pipe.index);
           return false;
         }
    }
    return true;
}

void MDPComp::unsetMDPCompLayerFlags(hwc_context_t* ctx,
                                     hwc_display_contents_1_t* list)
{
    LayerProp *layerProp = ctx->layerProp[HWC_DISPLAY_PRIMARY];

    for (int index = 0 ;
         index < ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers; index++) {
        if(layerProp[index].mFlags & HWC_MDPCOMP) {
            layerProp[index].mFlags &= ~HWC_MDPCOMP;
        }

        if(list->hwLayers[index].compositionType == HWC_OVERLAY) {
            list->hwLayers[index].compositionType = HWC_FRAMEBUFFER;
        }
    }
}

bool MDPComp::draw(hwc_context_t *ctx, hwc_display_contents_1_t* list) {

    if(!isEnabled() || !isUsed()) {
        ALOGD_IF(isDebug(),"%s: MDP Comp not configured", __FUNCTION__);
        return true;
     }

    if(!ctx || !list) {
        ALOGE("%s: invalid contxt or list",__FUNCTION__);
        return false;
    }

    /* reset Invalidator */
    if(idleInvalidator)
        idleInvalidator->markForSleep();

    overlay::Overlay& ov = *ctx->mOverlay;
    LayerProp *layerProp = ctx->layerProp[HWC_DISPLAY_PRIMARY];

    int numHwLayers = ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers;
    for(int i = 0; i < numHwLayers; i++ )
    {
        hwc_layer_1_t *layer = &list->hwLayers[i];

        if(!(layerProp[i].mFlags & HWC_MDPCOMP)) {
            continue;
        }

        MdpPipeInfo& pipe_info =
                        sCurrentFrame.pipeLayer[i].pipeIndex;
        int index = pipe_info.index;

        if(index < 0) {
            ALOGE("%s: Invalid pipe index (%d)", __FUNCTION__, index);
            return false;
        }

        ovutils::eDest dest = (ovutils::eDest)index;

        if (ctx ) {
            private_handle_t *hnd = (private_handle_t *)layer->handle;
            if(!hnd) {
                ALOGE("%s handle null", __FUNCTION__);
                return false;
            }

            ALOGD_IF(isDebug(),"%s: MDP Comp: Drawing layer: %p hnd: %p \
                                 using  pipe: %d", __FUNCTION__, layer,
                                 hnd, index );

            if (!ov.queueBuffer(hnd->fd, hnd->offset, dest)) {
                ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                return false;
            }
        }

        layerProp[i].mFlags &= ~HWC_MDPCOMP;
    }
    return true;
}

/*
 * Sets up BORDERFILL as default base pipe and detaches RGB0.
 * Framebuffer is always updated using PLAY ioctl.
 */

bool MDPComp::setupBasePipe(hwc_context_t *ctx) {

    int fb_stride = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].stride;
    int fb_width = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
    int fb_height = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres;
    int fb_fd = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].fd;

    mdp_overlay ovInfo;
    msmfb_overlay_data ovData;
    memset(&ovInfo, 0, sizeof(mdp_overlay));
    memset(&ovData, 0, sizeof(msmfb_overlay_data));

    ovInfo.src.format = MDP_RGB_BORDERFILL;
    ovInfo.src.width  = fb_width;
    ovInfo.src.height = fb_height;
    ovInfo.src_rect.w = fb_width;
    ovInfo.src_rect.h = fb_height;
    ovInfo.dst_rect.w = fb_width;
    ovInfo.dst_rect.h = fb_height;
    ovInfo.id = MSMFB_NEW_REQUEST;

    if (ioctl(fb_fd, MSMFB_OVERLAY_SET, &ovInfo) < 0) {
        ALOGE("Failed to call ioctl MSMFB_OVERLAY_SET err=%s",
                  strerror(errno));
        return false;
    }

    ovData.id = ovInfo.id;
    if (ioctl(fb_fd, MSMFB_OVERLAY_PLAY, &ovData) < 0) {
        ALOGE("Failed to call ioctl MSMFB_OVERLAY_PLAY err=%s",
                   strerror(errno));
        return false;
    }
    return true;
}

bool MDPComp::init(hwc_context_t *ctx) {

    if(!ctx) {
        ALOGE("%s: Invalid hwc context!!",__FUNCTION__);
        return false;
    }

    if(!setupBasePipe(ctx)) {
        ALOGE("%s: Failed to setup primary base pipe", __FUNCTION__);
        return false;
    }

    char property[PROPERTY_VALUE_MAX];

    sEnabled = false;
    if((property_get("persist.hwc.mdpcomp.enable", property, NULL) > 0) &&
                      (!strncmp(property, "1", PROPERTY_VALUE_MAX ) ||
                      (!strncasecmp(property,"true", PROPERTY_VALUE_MAX )))) {
           sEnabled = true;
    }

    sDebugLogs = false;
    if(property_get("debug.mdpcomp.logs", property, NULL) > 0) {
        if(atoi(property) != 0)
           sDebugLogs = true;
    }

    unsigned long idle_timeout = DEFAULT_IDLE_TIME;
    if(property_get("debug.mdpcomp.idletime", property, NULL) > 0) {
        if(atoi(property) != 0)
           idle_timeout = atoi(property);
    }

    //create Idle Invalidator
    idleInvalidator = IdleInvalidator::getInstance();

    if(idleInvalidator == NULL) {
       ALOGE("%s: failed to instantiate idleInvalidator  object", __FUNCTION__);
    } else {
       idleInvalidator->init(timeout_handler, ctx, idle_timeout);
    }
    return true;
}

bool MDPComp::configure(hwc_context_t *ctx,
                        hwc_display_contents_1_t* list) {

    if(!isEnabled()) {
        ALOGE_IF(isDebug(),"%s: MDP Comp. not enabled.", __FUNCTION__);
        return false;
    }

    overlay::Overlay& ov = *ctx->mOverlay;

    sActiveMax = ov.availablePipes();

    bool isMDPCompUsed = true;
    bool doable = isDoable(ctx, list);

    if(doable) {
        if(setup(ctx, list)) {
            setMDPCompLayerFlags(ctx, list);
        } else {
            ALOGD_IF(isDebug(),"%s: MDP Comp Failed",__FUNCTION__);
            isMDPCompUsed = false;
        }
     } else {
        ALOGD_IF( isDebug(),"%s: MDP Comp not possible[%d]",__FUNCTION__,
                   doable);
        isMDPCompUsed = false;
     }

     //Reset states
     if(!isMDPCompUsed) {
        //Reset current frame
         reset(ctx, list);
     }

     sMDPCompState = isMDPCompUsed ? MDPCOMP_ON : MDPCOMP_OFF;

     return isMDPCompUsed;
}
}; //namespace

