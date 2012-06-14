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

#include "overlayUtils.h"
#include "overlayMdp.h"

#undef ALOG_TAG
#define ALOG_TAG "overlay"

namespace ovutils = overlay::utils;
namespace overlay {
bool MdpCtrl::open(uint32_t fbnum) {
    // FD open
    if(!utils::openDev(mFd, fbnum,
                Res::devTemplate, O_RDWR)){
        ALOGE("Ctrl failed to open fbnum=%d", fbnum);
        return false;
    }
    return true;
}

void MdpCtrl::reset() {
    utils::memset0(mOVInfo);
    utils::memset0(mLkgo);
    mOVInfo.id = -1;
    mLkgo.id = -1;
}

bool MdpCtrl::close() {
    if(-1 == static_cast<int>(mOVInfo.id)) return true;
    if(!mdp_wrapper::unsetOverlay(mFd.getFD(), mOVInfo.id)) {
        ALOGE("MdpCtrl close error in unset");
        return false;
    }
    reset();
    if(!mFd.close()) {
        return false;
    }
    return true;
}

bool MdpCtrl::getScreenInfo(overlay::utils::ScreenInfo& info) {
    fb_fix_screeninfo finfo;
    if (!mdp_wrapper::getFScreenInfo(mFd.getFD(), finfo)) {
        return false;
    }

    fb_var_screeninfo vinfo;
    if (!mdp_wrapper::getVScreenInfo(mFd.getFD(), vinfo)) {
        return false;
    }
    info.mFBWidth   = vinfo.xres;
    info.mFBHeight  = vinfo.yres;
    info.mFBbpp     = vinfo.bits_per_pixel;
    info.mFBystride = finfo.line_length;
    return true;
}

bool MdpCtrl::get() {
    mdp_overlay ov;
    ov.id = mOVInfo.id;
    if (!mdp_wrapper::getOverlay(mFd.getFD(), ov)) {
        ALOGE("MdpCtrl get failed");
        return false;
    }
    mOVInfo = ov;
    return true;
}

// that is the second part of original setParameter function
void MdpCtrl::setSrcFormat(const utils::Whf& whf) {

    //By default mdp src format is the same as buffer's
    mOVInfo.src.format = whf.format;

    //If rotation is used and input formats are tiled then output of rotator is
    //non-tiled.
    // FIXME mRotInfo.enable = 1; for enable
    if (getUserData()) { // if rotations enabled in MdpCtrl
        if (whf.format == MDP_Y_CRCB_H2V2_TILE)
            mOVInfo.src.format = MDP_Y_CRCB_H2V2;
        else if (whf.format == MDP_Y_CBCR_H2V2_TILE)
            mOVInfo.src.format = MDP_Y_CBCR_H2V2;
        return;
    }

}

bool MdpCtrl::set() {
    if(!this->ovChanged()) {
        return true; // nothing todo here.
    }

    if(!mdp_wrapper::setOverlay(mFd.getFD(), mOVInfo)) {
        ALOGE("MdpCtrl failed to setOverlay, restoring last known "
                "good ov info");
        mdp_wrapper::dump("== Bad OVInfo is: ", mOVInfo);
        mdp_wrapper::dump("== Last good known OVInfo is: ", mLkgo);
        this->restore();
        // FIXME, do we need to set the old one?
        return false;
    }
    this->save();
    return true;
}

bool MdpCtrl::setPosition(const overlay::utils::Dim& d,
        int fbw, int fbh)
{
    // Validatee against FB size
    if(!d.check(fbw, fbh)) {
        ALOGE("MdpCtrl setPosition failed dest dim violate screen limits");
        return false;
    }

    ovutils::Dim dim(d);
    ovutils::Dim ovsrcdim = getSrcRectDim();
    // Scaling of upto a max of 8 times supported
    if(dim.w >(ovsrcdim.w * ovutils::HW_OV_MAGNIFICATION_LIMIT)){
        dim.w = ovutils::HW_OV_MAGNIFICATION_LIMIT * ovsrcdim.w;
        dim.x = (fbw - dim.w) / 2;
    }
    if(dim.h >(ovsrcdim.h * ovutils::HW_OV_MAGNIFICATION_LIMIT)) {
        dim.h = ovutils::HW_OV_MAGNIFICATION_LIMIT * ovsrcdim.h;
        dim.y = (fbh - dim.h) / 2;
    }

    //dim.even_out();
    setDstRectDim(dim);
    return true;
}

void MdpCtrl::updateSource(RotatorBase* r,
        const utils::PipeArgs& args,
        const utils::ScreenInfo& info) {
    utils::Whf whf(args.whf);
    mOVInfo.src.width  = whf.w;
    mOVInfo.src.height = whf.h;
    mOVInfo.src_rect.x = 0;
    mOVInfo.src_rect.y = 0;
    mOVInfo.dst_rect.x = 0;
    mOVInfo.dst_rect.y = 0;
    mOVInfo.dst_rect.w = whf.w;
    mOVInfo.dst_rect.h = whf.h;
    mOVInfo.src.format = whf.format;

    if(whf.format == MDP_Y_CRCB_H2V2_TILE ||
        (whf.format == MDP_Y_CBCR_H2V2_TILE)) {
        // passing by value, setInfo fills it and return by val
        mOVInfo = r->setInfo(args, mOVInfo);
    } else {
        mOVInfo.src_rect.w = whf.w;
        mOVInfo.src_rect.h = whf.h;
    }

    if (whf.w > info.mFBWidth)
        mOVInfo.dst_rect.w = info.mFBWidth;
    if (whf.h > info.mFBHeight)
        mOVInfo.dst_rect.h = info.mFBHeight;
    mSize = whf.size;
}


bool MdpCtrl::setInfo(RotatorBase* r,
        const utils::PipeArgs& args,
        const utils::ScreenInfo& info)
{
    // new request
    utils::Whf whf(args.whf);
    mOVInfo.id = MSMFB_NEW_REQUEST;

    updateSource(r, args, info);

    setUserData(0);
    mOVInfo.alpha = 0xff;
    mOVInfo.transp_mask = 0xffffffff;
    setZ(args.zorder);
    setFlags(args.mdpFlags);
    setWait(args.wait);
    setIsFg(args.isFg);
    mSize = whf.size;
    return true;
}

bool MdpCtrl::setCrop(const utils::Dim& cdim) {
    utils::Dim d(cdim);
    const utils::Whf ovwhf = getSrcWhf();
    int udata = getUserData();
    switch(udata) {
        case MDP_ROT_NOP:
            break; // nothing to do here
        case MDP_ROT_90:
        case MDP_ROT_90 | MDP_FLIP_UD:
        case MDP_ROT_90 | MDP_FLIP_LR:
            {
                if (ovwhf.w < (d.y + d.h)) {
                    ALOGE("MdpCtrl setCrop failed ROT 90 udata=%d",
                            udata);
                    d.dump();
                    this->dump();
                    return false;
                }
                uint32_t tmp = d.x;
                d.x = ovwhf.w - (d.y + d.h);
                d.y = tmp;
                utils::swap(d.w, d.h);
            }break;
        case MDP_ROT_270:
            {
                if (ovwhf.h < (d.x + d.w)) {
                    ALOGE("MdpCtrl setCrop failed ROT 270 udata=%d",
                            udata);
                    d.dump();
                    this->dump();
                    return false;
                }
                uint32_t tmp = d.y;
                d.y = ovwhf.h - (d.x + d.w);
                d.x = tmp;
                utils::swap(d.w, d.h);
            }break;
        case MDP_ROT_180:
            {
                if ((ovwhf.h < (d.y + d.h)) ||
                        (ovwhf.w < ( d.x + d.w))) {
                    ALOGE("MdpCtrl setCrop failed ROT 180 udata=%d",
                            udata);
                    d.dump();
                    this->dump();
                    return false;
                }
                d.x = ovwhf.w - (d.x + d.w);
                d.y = ovwhf.h - (d.y + d.h);
            }break;
        default:
            if(!(udata & (MDP_FLIP_UD | MDP_FLIP_LR))) {
                ALOGE("MdpCtrl setCrop unknown rot %d", udata);
                return false;
            }
    }

    if(getSrcRectDim() == d) {
        return true; // Nothing to do here
    }

    utils::normalizeCrop(d.x, d.w);
    utils::normalizeCrop(d.y, d.h);

    setSrcRectDim(d);

    return true;
}

void MdpCtrl::dump() const {
    ALOGE("== Dump MdpCtrl start ==");
    ALOGE("size=%d", mSize);
    mFd.dump();
    mdp_wrapper::dump("mOVInfo", mOVInfo);
    ALOGE("== Dump MdpCtrl end ==");
}

void MdpData::dump() const {
    ALOGE("== Dump MdpData start ==");
    mFd.dump();
    mdp_wrapper::dump("mOvData", mOvData);
    ALOGE("== Dump MdpData end ==");
}

void MdpCtrl3D::dump() const {
    ALOGE("== Dump MdpCtrl start ==");
    mFd.dump();
    ALOGE("== Dump MdpCtrl end ==");
}

} // overlay
