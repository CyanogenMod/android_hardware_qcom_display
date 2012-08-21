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

//Helper to even out x,w and y,h pairs
//x,y are always evened to ceil and w,h are evened to floor
static void normalizeCrop(uint32_t& xy, uint32_t& wh) {
    if(xy & 1) {
        utils::even_ceil(xy);
        if(wh & 1)
            utils::even_floor(wh);
        else
            wh -= 2;
    } else {
        utils::even_floor(wh);
    }
}

bool MdpCtrl::init(uint32_t fbnum) {
    // FD init
    if(!utils::openDev(mFd, fbnum,
                Res::fbPath, O_RDWR)){
        ALOGE("Ctrl failed to init fbnum=%d", fbnum);
        return false;
    }
    return true;
}

void MdpCtrl::reset() {
    utils::memset0(mOVInfo);
    utils::memset0(mLkgo);
    mOVInfo.id = MSMFB_NEW_REQUEST;
    mLkgo.id = MSMFB_NEW_REQUEST;
    mOrientation = utils::OVERLAY_TRANSFORM_0;
    mRotUsed = false;
}

bool MdpCtrl::close() {
    bool result = true;

    if(MSMFB_NEW_REQUEST != static_cast<int>(mOVInfo.id)) {
        if(!mdp_wrapper::unsetOverlay(mFd.getFD(), mOVInfo.id)) {
            ALOGE("MdpCtrl close error in unset");
            result = false;
        }
    }

    reset();
    if(!mFd.close()) {
        result = false;
    }

    return result;
}

bool MdpCtrl::setSource(const utils::PipeArgs& args) {

    setSrcWhf(args.whf);

    //TODO These are hardcoded. Can be moved out of setSource.
    mOVInfo.alpha = 0xff;
    mOVInfo.transp_mask = 0xffffffff;

    //TODO These calls should ideally be a part of setPipeParams API
    setFlags(args.mdpFlags);
    setZ(args.zorder);
    setIsFg(args.isFg);
    return true;
}

bool MdpCtrl::setCrop(const utils::Dim& d) {
    setSrcRectDim(d);
    return true;
}

bool MdpCtrl::setPosition(const overlay::utils::Dim& d,
        int fbw, int fbh)
{
    ovutils::Dim dim(d);
    ovutils::Dim ovsrcdim = getSrcRectDim();
    // Scaling of upto a max of 20 times supported
    if(dim.w >(ovsrcdim.w * ovutils::HW_OV_MAGNIFICATION_LIMIT)){
        dim.w = ovutils::HW_OV_MAGNIFICATION_LIMIT * ovsrcdim.w;
        dim.x = (fbw - dim.w) / 2;
    }
    if(dim.h >(ovsrcdim.h * ovutils::HW_OV_MAGNIFICATION_LIMIT)) {
        dim.h = ovutils::HW_OV_MAGNIFICATION_LIMIT * ovsrcdim.h;
        dim.y = (fbh - dim.h) / 2;
    }

    setDstRectDim(dim);
    return true;
}

bool MdpCtrl::setTransform(const utils::eTransform& orient,
        const bool& rotUsed) {
    int rot = utils::getMdpOrient(orient);
    setUserData(rot);
    //getMdpOrient will switch the flips if the source is 90 rotated.
    //Clients in Android dont factor in 90 rotation while deciding the flip.
    mOrientation = static_cast<utils::eTransform>(rot);

    //Rotator can be requested by client even if layer has 0 orientation.
    mRotUsed = rotUsed;
    return true;
}

void MdpCtrl::doTransform() {
    adjustSrcWhf(mRotUsed);
    setRotationFlags();
    //180 will be H + V
    //270 will be H + V + 90
    if(mOrientation & utils::OVERLAY_TRANSFORM_FLIP_H) {
            overlayTransFlipH();
    }
    if(mOrientation & utils::OVERLAY_TRANSFORM_FLIP_V) {
            overlayTransFlipV();
    }
    if(mOrientation & utils::OVERLAY_TRANSFORM_ROT_90) {
            overlayTransRot90();
    }
}

bool MdpCtrl::set() {
    //deferred calcs, so APIs could be called in any order.
    doTransform();
    utils::Whf whf = getSrcWhf();
    if(utils::isYuv(whf.format)) {
        normalizeCrop(mOVInfo.src_rect.x, mOVInfo.src_rect.w);
        normalizeCrop(mOVInfo.src_rect.y, mOVInfo.src_rect.h);
        utils::even_floor(mOVInfo.dst_rect.w);
        utils::even_floor(mOVInfo.dst_rect.h);
    }

    if(this->ovChanged()) {
        if(!mdp_wrapper::setOverlay(mFd.getFD(), mOVInfo)) {
            ALOGE("MdpCtrl failed to setOverlay, restoring last known "
                  "good ov info");
            mdp_wrapper::dump("== Bad OVInfo is: ", mOVInfo);
            mdp_wrapper::dump("== Last good known OVInfo is: ", mLkgo);
            this->restore();
            return false;
        }
        this->save();
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

//Adjust width, height, format if rotator is used.
void MdpCtrl::adjustSrcWhf(const bool& rotUsed) {
    if(rotUsed) {
        utils::Whf whf = getSrcWhf();
        if(whf.format == MDP_Y_CRCB_H2V2_TILE ||
                whf.format == MDP_Y_CBCR_H2V2_TILE) {
            whf.w = utils::alignup(whf.w, 64);
            whf.h = utils::alignup(whf.h, 32);
        }
        //For example: If original format is tiled, rotator outputs non-tiled,
        //so update mdp's src fmt to that.
        whf.format = utils::getRotOutFmt(whf.format);
        setSrcWhf(whf);
    }
}

void MdpCtrl::dump() const {
    ALOGE("== Dump MdpCtrl start ==");
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
