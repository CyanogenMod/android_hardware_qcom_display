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
#include "external.h"

#define SUPPORT_4LAYER 0

namespace qhwc {

/****** Class PipeMgr ***********/

void inline PipeMgr::reset() {
    mVGPipes = MAX_VG;
    mVGUsed = 0;
    mVGIndex = 0;
    mRGBPipes = MAX_RGB;
    mRGBUsed = 0;
    mRGBIndex = MAX_VG;
    mTotalAvail = mVGPipes + mRGBPipes;
    memset(&mStatus, 0x0 , sizeof(int)*mTotalAvail);
}

int PipeMgr::req_for_pipe(int pipe_req) {

    switch(pipe_req) {
        case PIPE_REQ_VG: //VG
            if(mVGPipes){
                mVGPipes--;
                mVGUsed++;
                mTotalAvail--;
                return PIPE_REQ_VG;
            }
        case PIPE_REQ_RGB: // RGB
            if(mRGBPipes) {
                mRGBPipes--;
                mRGBUsed++;
                mTotalAvail--;
                return PIPE_REQ_RGB;
            }
            return PIPE_NONE;
        case PIPE_REQ_FB: //FB
            if(mRGBPipes) {
               mRGBPipes--;
               mRGBUsed++;
               mTotalAvail--;
               mStatus[VAR_INDEX] = PIPE_IN_FB_MODE;
               return PIPE_REQ_FB;
           }
        default:
            break;
    };
    return PIPE_NONE;
}

int PipeMgr::assign_pipe(int pipe_pref) {
    switch(pipe_pref) {
        case PIPE_REQ_VG: //VG
            if(mVGUsed) {
                mVGUsed--;
                mStatus[mVGIndex] = PIPE_IN_COMP_MODE;
                return mVGIndex++;
            }
        case PIPE_REQ_RGB: //RGB
            if(mRGBUsed) {
                mRGBUsed--;
                mStatus[mRGBIndex] = PIPE_IN_COMP_MODE;
                return mRGBIndex++;
            }
        default:
            ALOGE("%s: PipeMgr:invalid case in pipe_mgr_assign",
                                                       __FUNCTION__);
            return -1;
    };
}

/****** Class MDPComp ***********/

MDPComp::State MDPComp::sMDPCompState = MDPCOMP_OFF;
struct MDPComp::frame_info MDPComp::sCurrentFrame;
PipeMgr MDPComp::sPipeMgr;
IdleInvalidator *MDPComp::idleInvalidator = NULL;
bool MDPComp::sIdleFallBack = false;
bool MDPComp::sDebugLogs = false;
int MDPComp::sSkipCount = 0;
int MDPComp::sMaxLayers = 0;

bool MDPComp::deinit() {
    //XXX: Tear down MDP comp state
    return true;
}

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
    if(sMDPCompState == MDPCOMP_ON) {
        sMDPCompState = MDPCOMP_OFF_PENDING;
    }

    sCurrentFrame.count = 0;
    if(sCurrentFrame.pipe_layer) {
        free(sCurrentFrame.pipe_layer);
        sCurrentFrame.pipe_layer = NULL;
    }

    //Reset MDP pipes
    sPipeMgr.reset();
    sPipeMgr.setStatus(VAR_INDEX, PIPE_IN_FB_MODE);

#if SUPPORT_4LAYER
    configure_var_pipe(ctx);
#endif
}

void MDPComp::setLayerIndex(hwc_layer_1_t* layer, const int pipe_index)
{
    layer->flags &= ~HWC_MDPCOMP_INDEX_MASK;
    layer->flags |= pipe_index << MDPCOMP_INDEX_OFFSET;
}

int MDPComp::getLayerIndex(hwc_layer_1_t* layer)
{
    int byp_index = -1;

    if(layer->flags & HWC_MDPCOMP) {
        byp_index = ((layer->flags & HWC_MDPCOMP_INDEX_MASK) >>
                                               MDPCOMP_INDEX_OFFSET);
        byp_index = (byp_index < sMaxLayers ? byp_index : -1 );
    }
    return byp_index;
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
/*
 * Configures pipe(s) for MDP composition
 */
int MDPComp::prepare(hwc_context_t *ctx, hwc_layer_1_t *layer,
                                            mdp_pipe_info& mdp_info) {

    int nPipeIndex = mdp_info.index;

    if (ctx) {

        private_handle_t *hnd = (private_handle_t *)layer->handle;

        overlay::Overlay& ov = *(ctx->mOverlay[HWC_DISPLAY_PRIMARY]);

        if(!hnd) {
            ALOGE("%s: layer handle is NULL", __FUNCTION__);
            return -1;
        }


        int hw_w = ctx->mFbDev->width;
        int hw_h = ctx->mFbDev->height;


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

        //REDUNDANT ??
        if(hnd != NULL &&
               (hnd->flags & private_handle_t::PRIV_FLAGS_NONCONTIGUOUS_MEM )) {
            ALOGE("%s: failed due to non-pmem memory",__FUNCTION__);
            return -1;
        }

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
        ovutils::eDest dest = ovutils::OV_PIPE_ALL;
        if (nPipeIndex == 0) {
            dest = ovutils::OV_PIPE0;
        } else if (nPipeIndex == 1) {
            dest = ovutils::OV_PIPE1;
        } else if (nPipeIndex == 2) {
            dest = ovutils::OV_PIPE2;
        }

        ovutils::eZorder zOrder = ovutils::ZORDER_0;

        if(mdp_info.z_order == 0 ) {
            zOrder = ovutils::ZORDER_0;
        } else if(mdp_info.z_order == 1 ) {
            zOrder = ovutils::ZORDER_1;
        } else if(mdp_info.z_order == 2 ) {
            zOrder = ovutils::ZORDER_2;
        }

        // Order order order
        // setSource - just setting source
        // setParameter - changes src w/h/f accordingly
        // setCrop - ROI - src_rect
        // setPosition - dst_rect
        // commit - commit changes to mdp driver
        // queueBuffer - not here, happens when draw is called

        ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(layer->transform);

        ov.setTransform(orient, dest);
        ovutils::Whf info(hnd->width, hnd->height, hnd->format, hnd->size);
        ovutils::eMdpFlags mdpFlags = mdp_info.isVG ? ovutils::OV_MDP_PIPE_SHARE
                                                   : ovutils::OV_MDP_FLAGS_NONE;
        ovutils::setMdpFlags(mdpFlags,ovutils::OV_MDP_BACKEND_COMPOSITION);
        ovutils::eIsFg isFG = mdp_info.isFG ? ovutils::IS_FG_SET
                                                    : ovutils::IS_FG_OFF;

        if(layer->blending == HWC_BLENDING_PREMULT) {
            ovutils::setMdpFlags(mdpFlags,
                    ovutils::OV_MDP_BLEND_FG_PREMULT);
        }

        ovutils::PipeArgs parg(mdpFlags,
                               info,
                               zOrder,
                               isFG,
                               ovutils::ROT_FLAG_DISABLED);

        ovutils::PipeArgs pargs[MAX_PIPES] = { parg, parg, parg };
        if (!ov.setSource(pargs, dest)) {
            ALOGE("%s: setSource failed", __FUNCTION__);
            return -1;
        }

        ovutils::Dim dcrop(crop.left, crop.top, crop_w, crop_h);
        if (!ov.setCrop(dcrop, dest)) {
            ALOGE("%s: setCrop failed", __FUNCTION__);
            return -1;
        }

        ovutils::Dim dim(dst.left, dst.top, dst_w, dst_h);
        if (!ov.setPosition(dim, dest)) {
            ALOGE("%s: setPosition failed", __FUNCTION__);
            return -1;
        }

        ALOGD_IF(isDebug(),"%s: MDP set: crop[%d,%d,%d,%d] dst[%d,%d,%d,%d] \
                       nPipe: %d isFG: %d zorder: %d",__FUNCTION__, dcrop.x,
                       dcrop.y,dcrop.w, dcrop.h, dim.x, dim.y, dim.w, dim.h,
                       nPipeIndex,mdp_info.isFG, mdp_info.z_order);

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

bool MDPComp::is_doable(hwc_context_t *ctx, hwc_display_contents_1_t* list) {
    //Number of layers
    int numAppLayers = ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers;
    if(numAppLayers < 1 || numAppLayers > (uint32_t)sMaxLayers) {
        ALOGD_IF(isDebug(), "%s: Unsupported number of layers",__FUNCTION__);
        return false;
    }

    //Disable MDPComp when ext display connected
    if(isExternalActive(ctx)) {
        ALOGD_IF(isDebug(), "%s: External display connected.", __FUNCTION__);
        return false;
    }

    //FB composition on idle timeout
    if(sIdleFallBack) {
        ALOGD_IF(isDebug(), "%s: idle fallback",__FUNCTION__);
        return false;
    }

    //MDP composition is not efficient if rotation is needed.
    for(int i = 0; i < numAppLayers; ++i) {
        if(list->hwLayers[i].transform) {
                ALOGD_IF(isDebug(), "%s: orientation involved",__FUNCTION__);
                return false;
        }
    }

    return true;
}

void MDPComp::setMDPCompLayerFlags(hwc_display_contents_1_t* list) {

    for(int index = 0 ; index < sCurrentFrame.count; index++ )
    {
        int layer_index = sCurrentFrame.pipe_layer[index].layer_index;
        if(layer_index >= 0) {
            hwc_layer_1_t* layer = &(list->hwLayers[layer_index]);

            layer->flags |= HWC_MDPCOMP;
            layer->compositionType = HWC_OVERLAY;
            layer->hints |= HWC_HINT_CLEAR_FB;
        }
    }
}

void MDPComp::get_layer_info(hwc_layer_1_t* layer, int& flags) {

    private_handle_t* hnd = (private_handle_t*)layer->handle;

    if(layer->flags & HWC_SKIP_LAYER) {
        flags |= MDPCOMP_LAYER_SKIP;
    } else if(hnd != NULL &&
        (hnd->flags & private_handle_t::PRIV_FLAGS_NONCONTIGUOUS_MEM )) {
        flags |= MDPCOMP_LAYER_UNSUPPORTED_MEM;
    }

    if(layer->blending != HWC_BLENDING_NONE)
        flags |= MDPCOMP_LAYER_BLEND;

    int dst_w, dst_h;
    getLayerResolution(layer, dst_w, dst_h);

    hwc_rect_t sourceCrop = layer->sourceCrop;
    const int src_w = sourceCrop.right - sourceCrop.left;
    const int src_h = sourceCrop.bottom - sourceCrop.top;
    if(((src_w > dst_w) || (src_h > dst_h))) {
        flags |= MDPCOMP_LAYER_DOWNSCALE;
    }
}

int MDPComp::mark_layers(hwc_context_t *ctx,
        hwc_display_contents_1_t* list, layer_mdp_info* layer_info,
        frame_info& current_frame) {

    int layer_count = ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers;

    if(layer_count > sMaxLayers) {
        if(!sPipeMgr.req_for_pipe(PIPE_REQ_FB)) {
            ALOGE("%s: binding var pipe to FB failed!!", __FUNCTION__);
            return 0;
        }
    }

    //Parse layers from higher z-order
    for(int index = layer_count - 1 ; index >= 0; index-- ) {
        hwc_layer_1_t* layer = &list->hwLayers[index];

        int layer_prop = 0;
        get_layer_info(layer, layer_prop);

        ALOGD_IF(isDebug(),"%s: prop for layer [%d]: %x", __FUNCTION__,
                                                             index, layer_prop);

        //Both in cases of NON-CONTIGUOUS memory or SKIP layer,
        //current version of mdp composition falls back completely to FB
        //composition.
        //TO DO: Support dual mode composition

        if(layer_prop & MDPCOMP_LAYER_UNSUPPORTED_MEM) {
            ALOGD_IF(isDebug(), "%s: Non contigous memory",__FUNCTION__);
            return MDPCOMP_ABORT;
        }

        if(layer_prop & MDPCOMP_LAYER_SKIP) {
            ALOGD_IF(isDebug(), "%s:skip layer",__FUNCTION__);
            return MDPCOMP_ABORT;
        }

        //Request for MDP pipes
        int pipe_pref = PIPE_REQ_VG;

        if((layer_prop & MDPCOMP_LAYER_DOWNSCALE) &&
                        (layer_prop & MDPCOMP_LAYER_BLEND)) {
            pipe_pref = PIPE_REQ_RGB;
         }

        int allocated_pipe = sPipeMgr.req_for_pipe( pipe_pref);
        if(allocated_pipe) {
          layer_info[index].can_use_mdp = true;
          layer_info[index].pipe_pref = allocated_pipe;
          current_frame.count++;
        }else {
            ALOGE("%s: pipe marking in mark layer fails for : %d",
                                          __FUNCTION__, allocated_pipe);
            return MDPCOMP_FAILURE;
        }
    }
    return MDPCOMP_SUCCESS;
}

void MDPComp::reset_layer_mdp_info(layer_mdp_info* layer_info, int count) {
    for(int i = 0 ; i < count; i++ ) {
        layer_info[i].can_use_mdp = false;
        layer_info[i].pipe_pref = PIPE_NONE;
    }
}

bool MDPComp::alloc_layer_pipes(hwc_context_t *ctx,
        hwc_display_contents_1_t* list,
        layer_mdp_info* layer_info, frame_info& current_frame) {

    int layer_count = ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers;
    int mdp_count = current_frame.count;
    int fallback_count = layer_count - mdp_count;
    int frame_pipe_count = 0;

    ALOGD_IF(isDebug(), "%s:  dual mode: %d  total count: %d \
                                mdp count: %d fallback count: %d",
                            __FUNCTION__, (layer_count != mdp_count),
                            layer_count, mdp_count, fallback_count);

    for(int index = 0 ; index < layer_count ; index++ ) {
        hwc_layer_1_t* layer = &list->hwLayers[index];

        if(layer_info[index].can_use_mdp) {
             pipe_layer_pair& info = current_frame.pipe_layer[frame_pipe_count];
             mdp_pipe_info& pipe_info = info.pipe_index;

             pipe_info.index = sPipeMgr.assign_pipe(layer_info[index].pipe_pref);
             pipe_info.isVG = (layer_info[index].pipe_pref == PIPE_REQ_VG);
             pipe_info.isFG = (frame_pipe_count == 0);
             /* if VAR pipe is attached to FB, FB will be updated with
                VSYNC WAIT flag, so no need to set VSYNC WAIT for any
                bypass pipes. if not, set VSYNC WAIT to the last updating pipe*/
             pipe_info.vsync_wait =
                 (sPipeMgr.getStatus(VAR_INDEX) == PIPE_IN_FB_MODE) ? false:
                                      (frame_pipe_count == (mdp_count - 1));
             /* All the layers composed on FB will have MDP zorder 0, so start
                assigning from  1*/
                pipe_info.z_order = index -
                        (fallback_count ? fallback_count - 1 : fallback_count);

             info.layer_index = index;
             frame_pipe_count++;
        }
    }
    return 1;
}

//returns array of layers and their allocated pipes
bool MDPComp::parse_and_allocate(hwc_context_t* ctx,
        hwc_display_contents_1_t* list, frame_info& current_frame ) {

    int layer_count = ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers;

    /* clear pipe status */
    sPipeMgr.reset();

    layer_mdp_info* bp_layer_info = (layer_mdp_info*)
                                   malloc(sizeof(layer_mdp_info)* layer_count);

    reset_layer_mdp_info(bp_layer_info, layer_count);

    /* iterate through layer list to mark candidate */
    if(mark_layers(ctx, list, bp_layer_info, current_frame) == MDPCOMP_ABORT) {
        free(bp_layer_info);
        current_frame.count = 0;
        ALOGE_IF(isDebug(), "%s:mark_layers failed!!", __FUNCTION__);
        return false;
    }
    current_frame.pipe_layer = (pipe_layer_pair*)
                          malloc(sizeof(pipe_layer_pair) * current_frame.count);

    /* allocate MDP pipes for marked layers */
    alloc_layer_pipes(ctx, list, bp_layer_info, current_frame);

    free(bp_layer_info);
    return true;
}
#if SUPPORT_4LAYER
int MDPComp::configure_var_pipe(hwc_context_t* ctx) {

    if(!ctx) {
       ALOGE("%s: invalid context", __FUNCTION__);
       return -1;
    }

    framebuffer_device_t *fbDev = ctx->fbDev;
    if (!fbDev) {
        ALOGE("%s: fbDev is NULL", __FUNCTION__);
        return -1;
    }

    int new_mode = -1, cur_mode;
    fbDev->perform(fbDev,EVENT_GET_VAR_PIPE_MODE, (void*)&cur_mode);

    if(sPipeMgr.getStatus(VAR_INDEX) == PIPE_IN_FB_MODE) {
        new_mode = VAR_PIPE_FB_ATTACH;
    } else if(sPipeMgr.getStatus(VAR_INDEX) == PIPE_IN_BYP_MODE) {
        new_mode = VAR_PIPE_FB_DETACH;
        fbDev->perform(fbDev,EVENT_WAIT_POSTBUFFER,NULL);
    }

    ALOGD_IF(isDebug(),"%s: old_mode: %d new_mode: %d", __FUNCTION__,
                                                      cur_mode, new_mode);

    if((new_mode != cur_mode) && (new_mode >= 0)) {
       if(fbDev->perform(fbDev,EVENT_SET_VAR_PIPE_MODE,(void*)&new_mode) < 0) {
           ALOGE("%s: Setting var pipe mode failed", __FUNCTION__);
       }
    }

    return 0;
}
#endif

bool MDPComp::setup(hwc_context_t* ctx, hwc_display_contents_1_t* list) {
    int nPipeIndex, vsync_wait, isFG;
    int numHwLayers = ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers;

    frame_info &current_frame = sCurrentFrame;
    current_frame.count = 0;

    if(current_frame.pipe_layer) {
        free(current_frame.pipe_layer);
        current_frame.pipe_layer = NULL;
    }

    if(!ctx) {
       ALOGE("%s: invalid context", __FUNCTION__);
       return -1;
    }

    framebuffer_device_t *fbDev = ctx->mFbDev;
    if (!fbDev) {
        ALOGE("%s: fbDev is NULL", __FUNCTION__);
        return -1;
    }

    if(!parse_and_allocate(ctx, list, current_frame)) {
#if SUPPORT_4LAYER
       int mode = VAR_PIPE_FB_ATTACH;
       if(fbDev->perform(fbDev,EVENT_SET_VAR_PIPE_MODE,(void*)&mode) < 0 ) {
           ALOGE("%s: setting var pipe mode failed", __FUNCTION__);
       }
#endif
       ALOGD_IF(isDebug(), "%s: Falling back to FB", __FUNCTION__);
       return false;
    }
#if SUPPORT_4LAYER
    configure_var_pipe(ctx);
#endif

    overlay::Overlay& ov = *(ctx->mOverlay[HWC_DISPLAY_PRIMARY]);
    ovutils::eOverlayState state = ov.getState();

    if (current_frame.count == 1) {
         state = ovutils::OV_BYPASS_1_LAYER;
    } else if (current_frame.count == 2) {
         state = ovutils::OV_BYPASS_2_LAYER;
    } else if (current_frame.count == 3) {
         state = ovutils::OV_BYPASS_3_LAYER;
   }

      ov.setState(state);


    for (int index = 0 ; index < current_frame.count; index++) {
        int layer_index = current_frame.pipe_layer[index].layer_index;
        hwc_layer_1_t* layer = &list->hwLayers[layer_index];
        mdp_pipe_info& cur_pipe = current_frame.pipe_layer[index].pipe_index;

        if( prepare(ctx, layer, cur_pipe) != 0 ) {
           ALOGD_IF(isDebug(), "%s: MDPComp failed to configure overlay for \
                                    layer %d with pipe index:%d",__FUNCTION__,
                                    index, cur_pipe.index);
           return false;
         } else {
            setLayerIndex(layer, index);
         }
    }
    return true;
}

void MDPComp::unsetMDPCompLayerFlags(hwc_context_t* ctx, hwc_display_contents_1_t* list)
{
    for (int index = 0 ; index < sCurrentFrame.count; index++) {
        int l_index = sCurrentFrame.pipe_layer[index].layer_index;
        if(list->hwLayers[l_index].flags & HWC_MDPCOMP) {
            list->hwLayers[l_index].flags &= ~HWC_MDPCOMP;
        }

        if(list->hwLayers[l_index].compositionType == HWC_OVERLAY) {
            list->hwLayers[l_index].compositionType = HWC_FRAMEBUFFER;
        }
    }
}

int MDPComp::draw(hwc_context_t *ctx, hwc_display_contents_1_t* list) {

    if(!isEnabled()) {
        ALOGD_IF(isDebug(),"%s: MDP Comp. not enabled",__FUNCTION__);
        return 0;
     }

    if(!ctx || !list) {
        ALOGE("%s: invalid contxt or list",__FUNCTION__);
        return -1;
    }

    overlay::Overlay& ov = *(ctx->mOverlay[HWC_DISPLAY_PRIMARY]);

    int numHwLayers = ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers;
    for(int i = 0; i < numHwLayers; i++ )
    {
        hwc_layer_1_t *layer = &list->hwLayers[i];

        if(!(layer->flags & HWC_MDPCOMP)) {
            ALOGD_IF(isDebug(), "%s: Layer Not flagged for MDP comp",
                                                                __FUNCTION__);
            continue;
        }

        int data_index = getLayerIndex(layer);
        mdp_pipe_info& pipe_info =
                          sCurrentFrame.pipe_layer[data_index].pipe_index;
        int index = pipe_info.index;

        if(index < 0) {
            ALOGE("%s: Invalid pipe index (%d)", __FUNCTION__, index);
            return -1;
        }

        /* reset Invalidator */
        if(idleInvalidator)
        idleInvalidator->markForSleep();

        ovutils::eDest dest;

        if (index == 0) {
            dest = ovutils::OV_PIPE0;
        } else if (index == 1) {
            dest = ovutils::OV_PIPE1;
        } else if (index == 2) {
            dest = ovutils::OV_PIPE2;
        }

        if (ctx ) {
            private_handle_t *hnd = (private_handle_t *)layer->handle;
            if(!hnd) {
                ALOGE("%s handle null", __FUNCTION__);
                return -1;
            }

            ALOGD_IF(isDebug(),"%s: MDP Comp: Drawing layer: %p hnd: %p \
                                 using  pipe: %d", __FUNCTION__, layer,
                                 hnd, index );

            if (!ov.queueBuffer(hnd->fd, hnd->offset, dest)) {
                ALOGE("%s: queueBuffer failed for external", __FUNCTION__);
                return -1;
            }
        }
        layer->flags &= ~HWC_MDPCOMP;
        layer->flags |= HWC_MDPCOMP_INDEX_MASK;
    }
    return 0;
}

bool MDPComp::init(hwc_context_t *dev) {

    if(!dev) {
        ALOGE("%s: Invalid hwc context!!",__FUNCTION__);
        return false;
    }

#if SUPPORT_4LAYER
    if(MAX_MDPCOMP_LAYERS > MAX_STATIC_PIPES) {
        framebuffer_device_t *fbDev = dev->fbDevice;
        if(fbDev == NULL) {
            ALOGE("%s: FATAL: framebuffer device is NULL", __FUNCTION__);
            return false;
        }

        //Receive VAR pipe object from framebuffer
        if(fbDev->perform(fbDev,EVENT_GET_VAR_PIPE,(void*)&ov) < 0) {
            ALOGE("%s: FATAL: getVariablePipe failed!!", __FUNCTION__);
            return false;
        }

        sPipeMgr.setStatus(VAR_INDEX, PIPE_IN_FB_MODE);
    }
#endif
    char property[PROPERTY_VALUE_MAX];

    sMaxLayers = 0;
    if(property_get("debug.mdpcomp.maxlayer", property, NULL) > 0) {
        if(atoi(property) != 0)
           sMaxLayers = atoi(property);
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
       idleInvalidator->init(timeout_handler, dev, idle_timeout);
    }
    return true;
}

bool MDPComp::configure(hwc_context_t *ctx,  hwc_display_contents_1_t* list) {

    if(!isEnabled()) {
        ALOGD_IF(isDebug(),"%s: MDP Comp. not enabled.", __FUNCTION__);
        return false;
    }

    bool isMDPCompUsed = true;
    bool doable = is_doable(ctx, list);

    if(doable) {
        if(setup(ctx, list)) {
            setMDPCompLayerFlags(list);
            sMDPCompState = MDPCOMP_ON;
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

     sIdleFallBack = false;

     return isMDPCompUsed;
}
}; //namespace

