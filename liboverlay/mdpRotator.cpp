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
#include "overlayRotator.h"

namespace ovutils = overlay::utils;

namespace overlay {

bool MdpRot::init()
{
    if(!mFd.open(Res::rotPath, O_RDWR)){
        ALOGE("MdpRot failed to init %s", Res::rotPath);
        return false;
    }
    return true;
}

void MdpRot::setSource(const overlay::utils::Whf& awhf) {
    utils::Whf whf(awhf);

    mRotImgInfo.src.format = whf.format;
    if(whf.format == MDP_Y_CRCB_H2V2_TILE ||
        whf.format == MDP_Y_CBCR_H2V2_TILE) {
        whf.w =  utils::alignup(awhf.w, 64);
        whf.h = utils::alignup(awhf.h, 32);
    }

    mRotImgInfo.src.width = whf.w;
    mRotImgInfo.src.height = whf.h;

    mRotImgInfo.src_rect.w = whf.w;
    mRotImgInfo.src_rect.h = whf.h;

    mRotImgInfo.dst.width = whf.w;
    mRotImgInfo.dst.height = whf.h;

    mBufSize = awhf.size;
}

void MdpRot::setFlags(const utils::eMdpFlags& flags) {
    mRotImgInfo.secure = 0;
    if(flags & utils::OV_MDP_SECURE_OVERLAY_SESSION)
        mRotImgInfo.secure = 1;
}

void MdpRot::setTransform(const utils::eTransform& rot, const bool& rotUsed)
{
    int r = utils::getMdpOrient(rot);
    setRotations(r);
    //getMdpOrient will switch the flips if the source is 90 rotated.
    //Clients in Android dont factor in 90 rotation while deciding the flip.
    mOrientation = static_cast<utils::eTransform>(r);
    ALOGE_IF(DEBUG_OVERLAY, "%s: r=%d", __FUNCTION__, r);

    setDisable();
    if(rotUsed) {
        setEnable();
    }
}

void MdpRot::doTransform() {
    if(mOrientation & utils::OVERLAY_TRANSFORM_ROT_90)
        utils::swap(mRotImgInfo.dst.width, mRotImgInfo.dst.height);
}

bool MdpRot::commit() {
    doTransform();
    if(rotConfChanged()) {
        if(!overlay::mdp_wrapper::startRotator(mFd.getFD(), mRotImgInfo)) {
            ALOGE("MdpRot commit failed");
            dump();
            return false;
        }
        save();
        mRotDataInfo.session_id = mRotImgInfo.session_id;
    }
    return true;
}

bool MdpRot::open_i(uint32_t numbufs, uint32_t bufsz)
{
    OvMem mem;

    OVASSERT(MAP_FAILED == mem.addr(), "MAP failed in open_i");

    if(!mem.open(numbufs, bufsz, mRotImgInfo.secure)){
        ALOGE("%s: Failed to open", __func__);
        mem.close();
        return false;
    }

    OVASSERT(MAP_FAILED != mem.addr(), "MAP failed");
    OVASSERT(mem.getFD() != -1, "getFd is -1");

    mRotDataInfo.dst.memory_id = mem.getFD();
    mRotDataInfo.dst.offset = 0;
    mMem.curr().m = mem;
    return true;
}

bool MdpRot::close() {
    bool success = true;
    if(mFd.valid() && (getSessId() > 0)) {
        if(!mdp_wrapper::endRotator(mFd.getFD(), getSessId())) {
            ALOGE("Mdp Rot error endRotator, fd=%d sessId=%d",
                    mFd.getFD(), getSessId());
            success = false;
        }
    }
    if (!mFd.close()) {
        ALOGE("Mdp Rot error closing fd");
        success = false;
    }
    if (!mMem.close()) {
        ALOGE("Mdp Rot error closing mem");
        success = false;
    }
    reset();
    return success;
}

bool MdpRot::remap(uint32_t numbufs) {
    // if current size changed, remap
    if(mBufSize == mMem.curr().size()) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: same size %d", __FUNCTION__, mBufSize);
        return true;
    }

    ALOGE_IF(DEBUG_OVERLAY, "%s: size changed - remapping", __FUNCTION__);
    OVASSERT(!mMem.prev().valid(), "Prev should not be valid");

    // ++mMem will make curr to be prev, and prev will be curr
    ++mMem;
    if(!open_i(numbufs, mBufSize)) {
        ALOGE("%s Error could not open", __FUNCTION__);
        return false;
    }
    for (uint32_t i = 0; i < numbufs; ++i) {
        mMem.curr().mRotOffset[i] = i * mBufSize;
    }
    return true;
}

void MdpRot::reset() {
    ovutils::memset0(mRotImgInfo);
    ovutils::memset0(mLSRotImgInfo);
    ovutils::memset0(mRotDataInfo);
    ovutils::memset0(mMem.curr().mRotOffset);
    ovutils::memset0(mMem.prev().mRotOffset);
    mMem.curr().mCurrOffset = 0;
    mMem.prev().mCurrOffset = 0;
    mBufSize = 0;
    mOrientation = utils::OVERLAY_TRANSFORM_0;
}

bool MdpRot::queueBuffer(int fd, uint32_t offset) {
    if(enabled()) {
        mRotDataInfo.src.memory_id = fd;
        mRotDataInfo.src.offset = offset;

        remap(RotMem::Mem::ROT_NUM_BUFS);
        OVASSERT(mMem.curr().m.numBufs(),
                "queueBuffer numbufs is 0");
        mRotDataInfo.dst.offset =
                mMem.curr().mRotOffset[mMem.curr().mCurrOffset];
        mMem.curr().mCurrOffset =
                (mMem.curr().mCurrOffset + 1) % mMem.curr().m.numBufs();

        if(!overlay::mdp_wrapper::rotate(mFd.getFD(), mRotDataInfo)) {
            ALOGE("MdpRot failed rotate");
            dump();
            return false;
        }

        // if the prev mem is valid, we need to close
        if(mMem.prev().valid()) {
            // FIXME if no wait for vsync the above
            // play will return immediatly and might cause
            // tearing when prev.close is called.
            if(!mMem.prev().close()) {
                ALOGE("%s error in closing prev rot mem", __FUNCTION__);
                return false;
            }
        }
    }
    return true;
}

void MdpRot::dump() const {
    ALOGE("== Dump MdpRot start ==");
    mFd.dump();
    mMem.curr().m.dump();
    mdp_wrapper::dump("mRotImgInfo", mRotImgInfo);
    mdp_wrapper::dump("mRotDataInfo", mRotDataInfo);
    ALOGE("== Dump MdpRot end ==");
}

} // namespace overlay
