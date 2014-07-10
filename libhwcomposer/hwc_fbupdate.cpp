/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2013, The Linux Foundation. All rights reserved.
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

#define DEBUG_FBUPDATE 0
#include <cutils/properties.h>
#include <gralloc_priv.h>
#include <overlayRotator.h>
#include "hwc_fbupdate.h"
#include "external.h"
#include "virtual.h"

using overlay::Rotator;
using namespace overlay::utils;

namespace qhwc {

namespace ovutils = overlay::utils;

IFBUpdate* IFBUpdate::getObject(hwc_context_t *ctx, const int& width, const int& dpy) {
    if(width > MAX_DISPLAY_DIM) {
        return new FBUpdateHighRes(ctx, dpy);
    }
    return new FBUpdateLowRes(ctx, dpy);
}

IFBUpdate::IFBUpdate(hwc_context_t *ctx, const int& dpy) : mDpy(dpy) {
    getBufferSizeAndDimensions(ctx->dpyAttr[dpy].xres,
            ctx->dpyAttr[dpy].yres,
            HAL_PIXEL_FORMAT_RGBA_8888,
            mAlignedFBWidth,
            mAlignedFBHeight);
}

void IFBUpdate::reset() {
    mModeOn = false;
    mRot = NULL;
}

//================= Low res====================================
FBUpdateLowRes::FBUpdateLowRes(hwc_context_t *ctx, const int& dpy):
        IFBUpdate(ctx, dpy) {}

void FBUpdateLowRes::reset() {
    IFBUpdate::reset();
    mDest = ovutils::OV_INVALID;
}

bool FBUpdateLowRes::preRotateExtDisplay(hwc_context_t *ctx,
                                            hwc_layer_1_t *layer,
                                            ovutils::Whf &info,
                                            hwc_rect_t& sourceCrop,
                                            ovutils::eMdpFlags& mdpFlags,
                                            int& rotFlags)
{
    int extOrient = getExtOrientation(ctx);
    ovutils::eTransform orient = static_cast<ovutils::eTransform >(extOrient);

    if(mDpy && (extOrient & HWC_TRANSFORM_ROT_90)) {
        mRot = ctx->mRotMgr->getNext();
        if(mRot == NULL) return false;
        Whf origWhf(mAlignedFBWidth, mAlignedFBHeight,
                    getMdpFormat(HAL_PIXEL_FORMAT_RGBA_8888));
        //Configure rotator for pre-rotation
        if(configRotator(mRot, info, origWhf, mdpFlags, orient, 0) < 0) {
            ALOGE("%s: configRotator Failed!", __FUNCTION__);
            mRot = NULL;
            return false;
        }
       ctx->mLayerRotMap[mDpy]->add(layer, mRot);
        info.format = (mRot)->getDstFormat();
        updateSource(orient, info, sourceCrop);
        rotFlags |= ovutils::ROT_PREROTATED;
    }
    return true;
}

bool FBUpdateLowRes::prepare(hwc_context_t *ctx, hwc_display_contents_1 *list,
                             int fbZorder) {
    if(!ctx->mMDP.hasOverlay) {
        ALOGD_IF(DEBUG_FBUPDATE, "%s, this hw doesnt support overlays",
                 __FUNCTION__);
        return false;
    }
    mModeOn = configure(ctx, list, fbZorder);
    return mModeOn;
}

// Configure
bool FBUpdateLowRes::configure(hwc_context_t *ctx, hwc_display_contents_1 *list,
                               int fbZorder) {
    bool ret = false;
    hwc_layer_1_t *layer = &list->hwLayers[list->numHwLayers - 1];
    if (LIKELY(ctx->mOverlay)) {
        int extOnlyLayerIndex = ctx->listStats[mDpy].extOnlyLayerIndex;
        // ext only layer present..
        if(extOnlyLayerIndex != -1) {
            layer = &list->hwLayers[extOnlyLayerIndex];
            layer->compositionType = HWC_OVERLAY;
        }
        overlay::Overlay& ov = *(ctx->mOverlay);

        ovutils::Whf info(mAlignedFBWidth,
                mAlignedFBHeight,
                ovutils::getMdpFormat(HAL_PIXEL_FORMAT_RGBA_8888));

        //Request a fb pipe
        ovutils::eDest dest = getPipeForFb(ctx, mDpy);
        if(dest == ovutils::OV_INVALID) { //None available
            ALOGE("%s: No pipes available to configure fb for dpy %d",
                __FUNCTION__, mDpy);
            return false;
        }
        mDest = dest;

        if((mDpy && ctx->deviceOrientation) &&
            ctx->listStats[mDpy].isDisplayAnimating) {
            fbZorder = 0;
        }

        ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_BLEND_FG_PREMULT;
        ovutils::eIsFg isFg = ovutils::IS_FG_OFF;
        ovutils::eZorder zOrder = static_cast<ovutils::eZorder>(fbZorder);

        hwc_rect_t sourceCrop = integerizeSourceCrop(layer->sourceCropf);
        hwc_rect_t displayFrame = layer->displayFrame;
        int transform = layer->transform;
        int rotFlags = ovutils::ROT_FLAGS_NONE;

        ovutils::eTransform orient =
                    static_cast<ovutils::eTransform>(transform);
        // use ext orientation if any
        int extOrient = getExtOrientation(ctx);

        // Do not use getNonWormholeRegion() function to calculate the
        // sourceCrop during animation on external display and
        // Dont do wormhole calculation when extorientation is set on External
        // Dont do wormhole calculation when extDownscale is enabled on External
        if(ctx->listStats[mDpy].isDisplayAnimating && mDpy) {
            sourceCrop = layer->displayFrame;
            displayFrame = sourceCrop;
        } else if((!mDpy ||
                   (mDpy && !extOrient
                   && !ctx->dpyAttr[mDpy].mDownScaleMode))
                   && (extOnlyLayerIndex == -1)) {
                getNonWormholeRegion(list, sourceCrop);
                displayFrame = sourceCrop;
        }
        calcExtDisplayPosition(ctx, NULL, mDpy, sourceCrop, displayFrame,
                                   transform, orient);
        //Store the displayFrame, will be used in getDisplayViewFrame
        ctx->dpyAttr[mDpy].mDstRect = displayFrame;
        setMdpFlags(layer, mdpFlags, 0, transform);
        // For External use rotator if there is a rotation value set
        ret = preRotateExtDisplay(ctx, layer, info,
                sourceCrop, mdpFlags, rotFlags);
        if(!ret) {
            ALOGE("%s: preRotate for external Failed!", __FUNCTION__);
            ctx->mOverlay->clear(mDpy);
            ctx->mLayerRotMap[mDpy]->clear();
            return false;
        }
        //For the mdp, since either we are pre-rotating or MDP does flips
        orient = ovutils::OVERLAY_TRANSFORM_0;
        transform = 0;

        //XXX: FB layer plane alpha is currently sent as zero from
        //surfaceflinger
        ovutils::PipeArgs parg(mdpFlags,
                info,
                zOrder,
                isFg,
                static_cast<ovutils::eRotFlags>(rotFlags),
                ovutils::DEFAULT_PLANE_ALPHA,
                (ovutils::eBlending) getBlending(layer->blending));

        ret = true;
        if(configMdp(ctx->mOverlay, parg, orient, sourceCrop, displayFrame,
                    NULL, mDest) < 0) {
            ALOGE("%s: configMdp failed for dpy %d", __FUNCTION__, mDpy);
            ctx->mLayerRotMap[mDpy]->clear();
            ret = false;
        }
    }
    return ret;
}

bool FBUpdateLowRes::draw(hwc_context_t *ctx, private_handle_t *hnd)
{
    if(!mModeOn) {
        return true;
    }
    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eDest dest = mDest;
    int fd = hnd->fd;
    uint32_t offset = hnd->offset;
    if(mRot) {
        if(!mRot->queueBuffer(fd, offset))
            return false;
        fd = mRot->getDstMemId();
        offset = mRot->getDstOffset();
    }
    if (!ov.queueBuffer(fd, offset, dest)) {
        ALOGE("%s: queueBuffer failed for FBUpdate", __FUNCTION__);
        ret = false;
    }
    return ret;
}

//================= High res====================================
FBUpdateHighRes::FBUpdateHighRes (hwc_context_t *ctx, const int& dpy):
    IFBUpdate(ctx, dpy) {}

void FBUpdateHighRes::reset() {
    IFBUpdate::reset();
    mDestLeft = ovutils::OV_INVALID;
    mDestRight = ovutils::OV_INVALID;
    mRot = NULL;
}

bool FBUpdateHighRes::prepare(hwc_context_t *ctx, hwc_display_contents_1 *list,
                              int fbZorder) {
    if(!ctx->mMDP.hasOverlay) {
        ALOGD_IF(DEBUG_FBUPDATE, "%s, this hw doesnt support overlays",
                 __FUNCTION__);
        return false;
    }
    ALOGD_IF(DEBUG_FBUPDATE, "%s, mModeOn = %d", __FUNCTION__, mModeOn);
    mModeOn = configure(ctx, list, fbZorder);
    return mModeOn;
}

// Configure
bool FBUpdateHighRes::configure(hwc_context_t *ctx,
        hwc_display_contents_1 *list, int fbZorder) {
    bool ret = false;
    hwc_layer_1_t *layer = &list->hwLayers[list->numHwLayers - 1];
    if (LIKELY(ctx->mOverlay)) {
        int extOnlyLayerIndex = ctx->listStats[mDpy].extOnlyLayerIndex;
        // ext only layer present..
        if(extOnlyLayerIndex != -1) {
            layer = &list->hwLayers[extOnlyLayerIndex];
            layer->compositionType = HWC_OVERLAY;
        }
        overlay::Overlay& ov = *(ctx->mOverlay);

        ovutils::Whf info(mAlignedFBWidth,
                mAlignedFBHeight,
                ovutils::getMdpFormat(HAL_PIXEL_FORMAT_RGBA_8888));

        //Request left pipe
        ovutils::eDest destL = getPipeForFb(ctx, mDpy);
        if(destL == ovutils::OV_INVALID) { //None available
            ALOGE("%s: No pipes available to configure fb for dpy %d's left"
                    " mixer", __FUNCTION__, mDpy);
            return false;
        }
        //Request right pipe
        ovutils::eDest destR = getPipeForFb(ctx, mDpy);
        if(destR == ovutils::OV_INVALID) { //None available
            ALOGE("%s: No pipes available to configure fb for dpy %d's right"
                    " mixer", __FUNCTION__, mDpy);
            return false;
        }

        mDestLeft = destL;
        mDestRight = destR;

        ovutils::eMdpFlags mdpFlagsL = ovutils::OV_MDP_BLEND_FG_PREMULT;

        ovutils::eZorder zOrder = static_cast<ovutils::eZorder>(fbZorder);

        //XXX: FB layer plane alpha is currently sent as zero from
        //surfaceflinger
        ovutils::PipeArgs pargL(mdpFlagsL,
                info,
                zOrder,
                ovutils::IS_FG_OFF,
                ovutils::ROT_FLAGS_NONE,
                ovutils::DEFAULT_PLANE_ALPHA,
                (ovutils::eBlending) getBlending(layer->blending));
        ov.setSource(pargL, destL);

        ovutils::eMdpFlags mdpFlagsR = mdpFlagsL;
        ovutils::setMdpFlags(mdpFlagsR, ovutils::OV_MDSS_MDP_RIGHT_MIXER);
        ovutils::PipeArgs pargR(mdpFlagsR,
                info,
                zOrder,
                ovutils::IS_FG_OFF,
                ovutils::ROT_FLAGS_NONE,
                ovutils::DEFAULT_PLANE_ALPHA,
                (ovutils::eBlending) getBlending(layer->blending));
        ov.setSource(pargR, destR);

        hwc_rect_t sourceCrop = integerizeSourceCrop(layer->sourceCropf);
        hwc_rect_t displayFrame = layer->displayFrame;
        // Do not use getNonWormholeRegion() function to calculate the
        // sourceCrop during animation on external display.
        if(ctx->listStats[mDpy].isDisplayAnimating && mDpy) {
            sourceCrop = layer->displayFrame;
            displayFrame = sourceCrop;
        } else if(extOnlyLayerIndex == -1) {
            getNonWormholeRegion(list, sourceCrop);
            displayFrame = sourceCrop;
        }
        ovutils::Dim dcropL(sourceCrop.left, sourceCrop.top,
                            (sourceCrop.right - sourceCrop.left) / 2,
                            sourceCrop.bottom - sourceCrop.top);
        ovutils::Dim dcropR(
            sourceCrop.left + (sourceCrop.right - sourceCrop.left) / 2,
            sourceCrop.top,
            (sourceCrop.right - sourceCrop.left) / 2,
            sourceCrop.bottom - sourceCrop.top);
        ov.setCrop(dcropL, destL);
        ov.setCrop(dcropR, destR);

        int transform = layer->transform;
        ovutils::eTransform orient =
            static_cast<ovutils::eTransform>(transform);
        ov.setTransform(orient, destL);
        ov.setTransform(orient, destR);

        //For FB left, top will always be 0
        //That should also be the case if using 2 mixers for single display
        ovutils::Dim dposL(displayFrame.left,
                           displayFrame.top,
                           (displayFrame.right - displayFrame.left) / 2,
                           displayFrame.bottom - displayFrame.top);
        ov.setPosition(dposL, destL);
        ovutils::Dim dposR(0,
                           displayFrame.top,
                           (displayFrame.right - displayFrame.left) / 2,
                           displayFrame.bottom - displayFrame.top);
        ov.setPosition(dposR, destR);

        ret = true;
        if (!ov.commit(destL)) {
            ALOGE("%s: commit fails for left", __FUNCTION__);
            ret = false;
        }
        if (!ov.commit(destR)) {
            ALOGE("%s: commit fails for right", __FUNCTION__);
            ret = false;
        }
        if(ret == false) {
            ctx->mLayerRotMap[mDpy]->clear();
        }
    }
    return ret;
}

bool FBUpdateHighRes::draw(hwc_context_t *ctx, private_handle_t *hnd)
{
    if(!mModeOn) {
        return true;
    }
    bool ret = true;
    overlay::Overlay& ov = *(ctx->mOverlay);
    ovutils::eDest destL = mDestLeft;
    ovutils::eDest destR = mDestRight;
    if (!ov.queueBuffer(hnd->fd, hnd->offset, destL)) {
        ALOGE("%s: queue failed for left of dpy = %d",
              __FUNCTION__, mDpy);
        ret = false;
    }
    if (!ov.queueBuffer(hnd->fd, hnd->offset, destR)) {
        ALOGE("%s: queue failed for right of dpy = %d",
              __FUNCTION__, mDpy);
        ret = false;
    }
    return ret;
}

//---------------------------------------------------------------------
}; //namespace qhwc
