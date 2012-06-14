/*
* Copyright (C) 2008 The Android Open Source Project
* Copyright (c) 2010-2012, Code Aurora Forum. All rights reserved.
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

#include <cutils/properties.h>
#include "overlayCtrlData.h"
#include "fb_priv.h"

namespace overlay{

bool Ctrl::open(uint32_t fbnum,
        RotatorBase* rot) {
    // MDP/FD open
    if(!mMdp.open(fbnum)) {
        ALOGE("Ctrl failed to open fbnum=%d", fbnum);
        return false;
    }

    if(!getScreenInfo(mInfo)) {
        ALOGE("Ctrl failed to getScreenInfo");
        return false;
    }

    OVASSERT(rot, "rot is null");
    mRot = rot;
    // rot should be already opened

    return true;
}

bool Ctrl::start(const utils::PipeArgs& args)
{
    int colorFormat = utils::getColorFormat(args.whf.format);
    utils::eMdpFlags flags = args.mdpFlags;

    //XXX: Support for interlaced content
    if (0) {

        setMdpFlags(flags, utils::OV_MDP_DEINTERLACE);

        // Get the actual format
        colorFormat = args.whf.format ^ HAL_PIXEL_FORMAT_INTERLACE;
    }
    utils::Whf hwwhf(args.whf);
    int fmt = utils::getMdpFormat(colorFormat);
    // FIXME format should probably be int and not uint
    if (fmt < 0) {
        ALOGE("Ctrl failed getMdpFormat unsopported "
                "colorFormat=%d format=%d flags=%d",
                colorFormat, fmt, flags);
        return false;
    }
    hwwhf.format = fmt;

    // devices should be already opened
    // (by calling open earlier in the flow)

    const utils::PipeArgs newargs(flags, // mdp flags
            args.orientation, // trans
            hwwhf,
            args.wait,
            args.zorder,
            args.isFg,
            args.rotFlags);
    if (!setInfo(newargs)) {
        ALOGE("Ctrl failed to setInfo mdpflags=%d wait=%d zorder=%d",
                newargs.mdpFlags, newargs.wait, newargs.zorder);
        hwwhf.dump();
        return false;
    }

    // FIXME, can we remove that and have it in
    // setSource only when source changed?
    if(!mRot->start(newargs)) {
        ALOGE("Ctrl failed to start Rotation session");
        return false;
    }

    // if geom is different, we need to prepare a new rot buffers.
    // remap on demand when the current orientation is 90,180, etc.
    // and the prev orientation was 0. It means we go from orient
    if(!mRot->remap(utils::ROT_NUM_BUFS, newargs)) {
        ALOGE("%s Error in remapping", __FUNCTION__);
    }

    if(!mMdp.set()) {
        ALOGE("Ctrl start failed set overlay");
        return false;
    }

    // cache the src to be the current mCrop vals
    mCrop.w = hwwhf.w;
    mCrop.h = hwwhf.h;

    return true;
}

inline void Ctrl::updateSource(RotatorBase* r,
        const utils::PipeArgs& args,
        utils::ScreenInfo& info)
{
    mMdp.updateSource(r, args, info);
}

bool Ctrl::setSource(const utils::PipeArgs& args)
{
    mMdp.setWait(args.wait);

    utils::PipeArgs newargs(args);
    utils::Whf whf(args.whf);
    // check geom change
    if(mOvBufInfo != whf) {
        // whf.format is given as HAL, that is why it is
        // needed to be MDP fmt.
        whf.format = utils::getColorFormat(whf.format);
        int fmt = utils::getMdpFormat(whf.format);
        OVASSERT(-1 != fmt, "Ctrl setSource format is -1");
        whf.format = fmt;
        newargs.whf = whf;
        updateSource(mRot, newargs, mInfo);
        mMdp.setUserData(0);
        if(!mRot->start(newargs)) {
            ALOGE("%s failed start rot", __FUNCTION__);
            return false;
        }

        // if geom is different, we need to prepare a new rot buffers.
        // remap on demand when the current orientation is 90,180, etc.
        // and the prev orientation was 0. It means we go from orient
        if(!mRot->remap(utils::ROT_NUM_BUFS, newargs)) {
            ALOGE("%s Error in remapping", __FUNCTION__);
        }
    }

    // needed for setSource
    mOrient = args.orientation;

    // cache last whf from gralloc hnd
    mOvBufInfo = args.whf;

    // orign impl is returning false here
    // because they will close the overlay and reopen it.
    // New design would not do that.
    return true;
}

bool Ctrl::setPosition(const utils::Dim& dim)
{
    if(!dim.check(mInfo.mFBWidth, mInfo.mFBHeight)) {
        ALOGE("Ctrl setPosition error in dim");
        dim.dump();
        return false;
    }

    if(!mMdp.setPosition(dim, mInfo.mFBWidth, mInfo.mFBHeight)) {
        ALOGE("Ctrl failed MDP setPosition");
        return false;
    }
    return true;
}

bool Ctrl::setParameter(const utils::Params& p)
{
    if (utils::OVERLAY_TRANSFORM == p.param &&
            p.value == mMdp.getUserData()) {
        // nothing to do here
        return true;
    }

    utils::eTransform trns = static_cast<utils::eTransform>(p.value);
    switch (p.param) {
        case utils::OVERLAY_DITHER:
            // nothing here today
            ALOGE("Ctrl setParameter OVERLAY_DITHER not impl");
            return true;
        case utils::OVERLAY_TRANSFORM:
            if(!mRot->overlayTransform(mMdp, trns)) {
                ALOGE("Ctrl setParameter failed Rot overlayTransform");
                return false;
            }
            break;
        default:
            ALOGE("Ctrl setParameter unknown param %d", p.param);
            return false;
    }
    return true;
}

bool Ctrl::setCrop(const utils::Dim& d)
{
    // FIXME check channel validity
    if(!mMdp.setCrop(d)) {
        ALOGE("Data setCrop failed in MDP setCrop");
        return false;
    }
    mCrop = d;
    return true;
}

utils::Dim Ctrl::getAspectRatio(const utils::Whf& whf) const
{
    utils::Whf inWhf(whf.w, whf.h, mMdp.getSrcWhf().format);
    utils::Whf tmpwhf(inWhf);
    uint32_t fbWidth  = mInfo.mFBWidth;
    uint32_t fbHeight = mInfo.mFBHeight;

    /* Calculate the width and height if it is YUV TILE format*/
    if (inWhf.format == HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED) {
        tmpwhf.w = whf.w - (utils::alignup(whf.w, 64) - whf.w);
        tmpwhf.h = whf.h - (utils::alignup(whf.h, 32) - whf.h);
    }
    if (inWhf.w * fbHeight > fbWidth * inWhf.h) {
        inWhf.h = fbWidth * inWhf.h / inWhf.w;
        utils::even_out(inWhf.h);
        inWhf.w = fbWidth;
    } else if (inWhf.w * fbHeight < fbWidth * inWhf.h) {
        inWhf.w = fbHeight * inWhf.w / inWhf.h;
        utils::even_out(inWhf.w);
        inWhf.h = fbHeight;
    } else {
        inWhf.w = fbWidth;
        inWhf.h = fbHeight;
    }
    /* Scaling of upto a max of 8 times supported */
    if (inWhf.w > (tmpwhf.w * utils::HW_OV_MAGNIFICATION_LIMIT)){
        inWhf.w = utils::HW_OV_MAGNIFICATION_LIMIT * tmpwhf.w;
    }
    if(inWhf.h > (tmpwhf.h * utils::HW_OV_MAGNIFICATION_LIMIT)) {
        inWhf.h = utils::HW_OV_MAGNIFICATION_LIMIT * tmpwhf.h;
    }
    if (inWhf.w > fbWidth) inWhf.w = fbWidth;
    if (inWhf.h > fbHeight) inWhf.h = fbHeight;

    char value[PROPERTY_VALUE_MAX];
    property_get("hw.actionsafe.width", value, "0");
    float asWidth = atof(value);
    property_get("hw.actionsafe.height", value, "0");
    float asHeight = atof(value);
    inWhf.w = inWhf.w * (1.0f - asWidth / 100.0f);
    inWhf.h = inWhf.h * (1.0f - asHeight / 100.0f);

    uint32_t x = (fbWidth - inWhf.w) / 2.0;
    uint32_t y = (fbHeight - inWhf.h) / 2.0;
    return utils::Dim(x, y, inWhf.w, inWhf.h);
}

utils::FrameBufferInfo* utils::FrameBufferInfo::sFBInfoInstance = 0;

// This function gets the destination position for external display
// based on the position and aspect ratio of the primary
utils::Dim Ctrl::getAspectRatio(const utils::Dim& dim) const {
    float priWidth  = utils::FrameBufferInfo::getInstance()->getWidth();
    float priHeight = utils::FrameBufferInfo::getInstance()->getHeight();
    float fbWidth = mInfo.mFBWidth;
    float fbHeight = mInfo.mFBHeight;
    float wRatio = 1.0;
    float hRatio = 1.0;
    float xRatio = 1.0;
    float yRatio = 1.0;
    utils::Dim inDim(dim);

    int xPos = 0;
    int yPos = 0;
    int tmp = 0;
    utils::Dim tmpDim;
    switch(inDim.o) {
        case MDP_ROT_NOP:
        case MDP_ROT_180:
            {
                utils::Whf whf((uint32_t) priWidth, (uint32_t) priHeight, 0);
                tmpDim = getAspectRatio(whf);
                xPos = tmpDim.x;
                yPos = tmpDim.y;
                fbWidth = tmpDim.w;
                fbHeight = tmpDim.h;

                if (inDim.o == MDP_ROT_180) {
                    inDim.x = priWidth - (inDim.x + inDim.w);
                    inDim.y = priHeight - (inDim.y + inDim.h);
                }
                break;
            }
        case MDP_ROT_90:
        case MDP_ROT_270:
            {
                if(inDim.o == MDP_ROT_90) {
                    tmp = inDim.y;
                    inDim.y = priWidth - (inDim.x + inDim.w);
                    inDim.x = tmp;
                }
                else if (inDim.o == MDP_ROT_270) {
                    tmp = inDim.x;
                    inDim.x = priHeight - (inDim.y + inDim.h);
                    inDim.y = tmp;
                }

                // Swap the destination width/height
                utils::swapWidthHeight(inDim.w, inDim.h);
                // Swap width/height for primary
                utils::swapWidthHeight(priWidth, priHeight);
                utils::Whf whf((uint32_t) priWidth, (uint32_t) priHeight, 0);
                tmpDim = getAspectRatio(whf);
                xPos = tmpDim.x;
                yPos = tmpDim.y;
                fbWidth = tmpDim.w;
                fbHeight = tmpDim.h;
                break;
            }
        default:
            ALOGE("%s: Unknown Orientation", __FUNCTION__);
            break;
    }

    // Calculate the position
    xRatio = inDim.x/priWidth;
    yRatio = inDim.y/priHeight;
    wRatio = inDim.w/priWidth;
    hRatio = inDim.h/priHeight;

    return utils::Dim((xRatio * fbWidth) + xPos,   // x
            (yRatio * fbHeight) + yPos,  // y
            (wRatio * fbWidth),          // width
            (hRatio * fbHeight),         // height
            inDim.o);                    // orientation
}

void Ctrl::dump() const {
    ALOGE("== Dump Ctrl start ==");
    ALOGE("orient=%d", mOrient);
    mInfo.dump("mInfo");
    mMdp.dump();
    mRot->dump();
    ALOGE("== Dump Ctrl end ==");
}

} // overlay
