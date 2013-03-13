/*
* Copyright (C) 2008 The Android Open Source Project
* Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
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

#include <mdp_version.h>
#include "overlayUtils.h"
#include "overlayMdp.h"

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
    mDownscale = 0;
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

void MdpCtrl::setSource(const utils::PipeArgs& args) {
    setSrcWhf(args.whf);

    //TODO These are hardcoded. Can be moved out of setSource.
    mOVInfo.alpha = 0xff;
    mOVInfo.transp_mask = 0xffffffff;

    //TODO These calls should ideally be a part of setPipeParams API
    setFlags(args.mdpFlags);
    setZ(args.zorder);
    setIsFg(args.isFg);
}

void MdpCtrl::setCrop(const utils::Dim& d) {
    setSrcRectDim(d);
}

void MdpCtrl::setPosition(const overlay::utils::Dim& d) {
    setDstRectDim(d);
}

void MdpCtrl::setTransform(const utils::eTransform& orient) {
    int rot = utils::getMdpOrient(orient);
    setUserData(rot);
    //getMdpOrient will switch the flips if the source is 90 rotated.
    //Clients in Android dont factor in 90 rotation while deciding the flip.
    mOrientation = static_cast<utils::eTransform>(rot);
}

void MdpCtrl::doTransform() {
    setRotationFlags();
    utils::Whf whf = getSrcWhf();
    utils::Dim dim = getSrcRectDim();
    utils::preRotateSource(mOrientation, whf, dim);
    setSrcWhf(whf);
    setSrcRectDim(dim);
}

void MdpCtrl::doDownscale() {
    mOVInfo.src_rect.x >>= mDownscale;
    mOVInfo.src_rect.y >>= mDownscale;
    mOVInfo.src_rect.w >>= mDownscale;
    mOVInfo.src_rect.h >>= mDownscale;
}

bool MdpCtrl::set() {
    //deferred calcs, so APIs could be called in any order.
    doTransform();
    doDownscale();
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

//Update src format based on rotator's destination format.
void MdpCtrl::updateSrcFormat(const uint32_t& rotDestFmt) {
    utils::Whf whf = getSrcWhf();
    whf.format =  rotDestFmt;
    setSrcWhf(whf);
}

void MdpCtrl::dump() const {
    ALOGE("== Dump MdpCtrl start ==");
    mFd.dump();
    mdp_wrapper::dump("mOVInfo", mOVInfo);
    ALOGE("== Dump MdpCtrl end ==");
}

void MdpCtrl::getDump(char *buf, size_t len) {
    ovutils::getDump(buf, len, "Ctrl(mdp_overlay)", mOVInfo);
}

void MdpData::dump() const {
    ALOGE("== Dump MdpData start ==");
    mFd.dump();
    mdp_wrapper::dump("mOvData", mOvData);
    ALOGE("== Dump MdpData end ==");
}

void MdpData::getDump(char *buf, size_t len) {
    ovutils::getDump(buf, len, "Data(msmfb_overlay_data)", mOvData);
}

void MdpCtrl3D::dump() const {
    ALOGE("== Dump MdpCtrl start ==");
    mFd.dump();
    ALOGE("== Dump MdpCtrl end ==");
}

} // overlay
