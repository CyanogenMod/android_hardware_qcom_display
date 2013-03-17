/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
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

#include "overlayUtils.h"
#include "overlayRotator.h"

#ifdef VENUS_COLOR_FORMAT
#include <media/msm_media_info.h>
#else
#define VENUS_BUFFER_SIZE(args...) 0
#endif

#ifndef MDSS_MDP_ROT_ONLY
#define MDSS_MDP_ROT_ONLY 0x80
#endif

#define SIZE_1M 0x00100000
#define MDSS_ROT_MASK (MDP_ROT_90 | MDP_FLIP_UD | MDP_FLIP_LR)

namespace ovutils = overlay::utils;

namespace overlay {
MdssRot::MdssRot() {
    reset();
    init();
}

MdssRot::~MdssRot() { close(); }

inline void MdssRot::setEnable() { mEnabled = true; }

inline void MdssRot::setDisable() { mEnabled = false; }

inline bool MdssRot::enabled() const { return mEnabled; }

inline void MdssRot::setRotations(uint32_t flags) { mRotInfo.flags |= flags; }

inline int MdssRot::getDstMemId() const {
    return mRotData.dst_data.memory_id;
}

inline uint32_t MdssRot::getDstOffset() const {
    return mRotData.dst_data.offset;
}

inline uint32_t MdssRot::getDstFormat() const {
    //For mdss src and dst formats are same
    return mRotInfo.src.format;
}

inline uint32_t MdssRot::getSessId() const { return mRotInfo.id; }

inline void MdssRot::setSrcFB() {
    mRotData.data.flags |= MDP_MEMORY_ID_TYPE_FB;
}

inline bool MdssRot::init() {
    if(!utils::openDev(mFd, 0, Res::fbPath, O_RDWR)) {
        ALOGE("MdssRot failed to init fb0");
        return false;
    }
    return true;
}

void MdssRot::setSource(const overlay::utils::Whf& awhf) {
    utils::Whf whf(awhf);

    mRotInfo.src.format = whf.format;
    if(whf.format == MDP_Y_CRCB_H2V2_TILE ||
        whf.format == MDP_Y_CBCR_H2V2_TILE) {
        whf.w =  utils::alignup(awhf.w, 64);
        whf.h = utils::alignup(awhf.h, 32);
    }

    mRotInfo.src.width = whf.w;
    mRotInfo.src.height = whf.h;

    mRotInfo.src_rect.w = whf.w;
    mRotInfo.src_rect.h = whf.h;

    mRotInfo.dst_rect.w = whf.w;
    mRotInfo.dst_rect.h = whf.h;

    mBufSize = awhf.size;
}

inline void MdssRot::setDownscale(int ds) {}

inline void MdssRot::setFlags(const utils::eMdpFlags& flags) {
    mRotInfo.flags |= flags;
}

inline void MdssRot::setTransform(const utils::eTransform& rot)
{
    int flags = utils::getMdpOrient(rot);
    if (flags != -1)
        setRotations(flags);
    //getMdpOrient will switch the flips if the source is 90 rotated.
    //Clients in Android dont factor in 90 rotation while deciding the flip.
    mOrientation = static_cast<utils::eTransform>(flags);
    ALOGE_IF(DEBUG_OVERLAY, "%s: rot=%d", __FUNCTION__, flags);
}

inline void MdssRot::setRotatorUsed(const bool& rotUsed) {
    setDisable();
    if(rotUsed) {
        setEnable();
    }
}

inline void MdssRot::doTransform() {
    if(mOrientation & utils::OVERLAY_TRANSFORM_ROT_90)
        utils::swap(mRotInfo.dst_rect.w, mRotInfo.dst_rect.h);
}

bool MdssRot::commit() {
    doTransform();
    setBufSize(mRotInfo.src.format);
    mRotInfo.flags |= MDSS_MDP_ROT_ONLY;
    if(!overlay::mdp_wrapper::setOverlay(mFd.getFD(), mRotInfo)) {
        ALOGE("MdssRot commit failed!");
        dump();
        return false;
    }
    mRotData.id = mRotInfo.id;
    // reset rotation flags to avoid stale orientation values
    mRotInfo.flags &= ~MDSS_ROT_MASK;
    return true;
}

bool MdssRot::queueBuffer(int fd, uint32_t offset) {
    if(enabled()) {
        mRotData.data.memory_id = fd;
        mRotData.data.offset = offset;

        remap(RotMem::Mem::ROT_NUM_BUFS);
        OVASSERT(mMem.curr().m.numBufs(), "queueBuffer numbufs is 0");

        mRotData.dst_data.offset =
                mMem.curr().mRotOffset[mMem.curr().mCurrOffset];
        mMem.curr().mCurrOffset =
                (mMem.curr().mCurrOffset + 1) % mMem.curr().m.numBufs();

        if(!overlay::mdp_wrapper::play(mFd.getFD(), mRotData)) {
            ALOGE("MdssRot play failed!");
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

bool MdssRot::open_i(uint32_t numbufs, uint32_t bufsz)
{
    OvMem mem;
    OVASSERT(MAP_FAILED == mem.addr(), "MAP failed in open_i");
    bool isSecure = mRotInfo.flags & utils::OV_MDP_SECURE_OVERLAY_SESSION;

    if(!mem.open(numbufs, bufsz, isSecure)){
        ALOGE("%s: Failed to open", __func__);
        mem.close();
        return false;
    }

    OVASSERT(MAP_FAILED != mem.addr(), "MAP failed");
    OVASSERT(mem.getFD() != -1, "getFd is -1");

    mRotData.dst_data.memory_id = mem.getFD();
    mRotData.dst_data.offset = 0;
    mMem.curr().m = mem;
    return true;
}

bool MdssRot::remap(uint32_t numbufs) {
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

bool MdssRot::close() {
    bool success = true;
    if(mFd.valid() && (getSessId() != (uint32_t) MSMFB_NEW_REQUEST)) {
        if(!mdp_wrapper::unsetOverlay(mFd.getFD(), getSessId())) {
            ALOGE("MdssRot::close unsetOverlay failed, fd=%d sessId=%d",
                  mFd.getFD(), getSessId());
            success = false;
        }
    }

    if (!mFd.close()) {
        ALOGE("Mdss Rot error closing fd");
        success = false;
    }
    if (!mMem.close()) {
        ALOGE("Mdss Rot error closing mem");
        success = false;
    }
    reset();
    return success;
}

void MdssRot::reset() {
    ovutils::memset0(mRotInfo);
    ovutils::memset0(mRotData);
    mRotData.data.memory_id = -1;
    mRotInfo.id = MSMFB_NEW_REQUEST;
    ovutils::memset0(mMem.curr().mRotOffset);
    ovutils::memset0(mMem.prev().mRotOffset);
    mMem.curr().mCurrOffset = 0;
    mMem.prev().mCurrOffset = 0;
    mBufSize = 0;
    mOrientation = utils::OVERLAY_TRANSFORM_0;
}

void MdssRot::dump() const {
    ALOGE("== Dump MdssRot start ==");
    mFd.dump();
    mMem.curr().m.dump();
    mdp_wrapper::dump("mRotInfo", mRotInfo);
    mdp_wrapper::dump("mRotData", mRotData);
    ALOGE("== Dump MdssRot end ==");
}

void MdssRot::setBufSize(int format) {

    switch (format) {
        case MDP_Y_CBCR_H2V2_VENUS:
            mBufSize = VENUS_BUFFER_SIZE(COLOR_FMT_NV12, mRotInfo.dst_rect.w,
                                         mRotInfo.dst_rect.h);
            break;

        case MDP_Y_CR_CB_GH2V2:
            int alignedw = utils::align(mRotInfo.dst_rect.w, 16);
            int alignedh = mRotInfo.dst_rect.h;
            mBufSize = (alignedw*alignedh) +
                (utils::align(alignedw/2, 16) * (alignedh/2))*2;
            mBufSize = utils::align(mBufSize, 4096);
            break;
    }

    if (mRotInfo.flags & utils::OV_MDP_SECURE_OVERLAY_SESSION)
        mBufSize = utils::align(mBufSize, SIZE_1M);
}
} // namespace overlay
