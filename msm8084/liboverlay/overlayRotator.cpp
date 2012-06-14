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

#include "overlayRotator.h"
#include "overlayUtils.h"
#include "overlayMdp.h"

namespace ovutils = overlay::utils;

namespace overlay {

namespace utils {
inline mdp_overlay setInfoNullRot(const utils::PipeArgs& args,
        const mdp_overlay& o)
{
    mdp_overlay ov = o;
    utils::Whf whf(args.whf);
    utils::Dim d(utils::getSrcRectDim(ov));

    d.w = whf.w - (utils::alignup(whf.w, 64) - whf.w);
    d.h = whf.h - (utils::alignup(whf.h, 32) - whf.h);
    utils::setSrcRectDim(ov, d);
    return ov;
}

inline mdp_overlay setInfoRot(const utils::PipeArgs& args,
        const mdp_overlay& o)
{
    /* If there are no orientation, then we use setInfoRot
     * That is even if we are a real rotator object (not null)
     * Note, that if args.rotFlags are ENABLED
     * it means we would still like to have rot
     * even though it is ROT_0 */
    if(OVERLAY_TRANSFORM_0 == args.orientation &&
            utils::ROT_FLAG_ENABLED != args.rotFlags) {
        return setInfoNullRot(args, o);
    }

    mdp_overlay ov = o;
    utils::Whf whf(args.whf);
    utils::Dim d(utils::getSrcRectDim(ov));
    d.w = whf.w;
    d.h = whf.h;
    utils::Whf localwhf (utils::getSrcWhf(ov));
    localwhf.w = utils::alignup(whf.w, 64);
    localwhf.h = utils::alignup(whf.h, 32);
    d.x = localwhf.w - whf.w;
    d.y = localwhf.h - whf.h;
    utils::setSrcRectDim(ov, d);
    utils::setSrcWhf(ov, localwhf);
    return ov;
}

} // utils

bool MdpRot::open()
{
    if(!mFd.open(Res::rotPath, O_RDWR)){
        ALOGE("MdpRot failed to open %s", Res::rotPath);
        return false;
    }
    return true;
}

bool MdpRot::open_i(uint32_t numbufs, uint32_t bufsz)
{
    OvMem mem;

    OVASSERT(MAP_FAILED == mem.addr(), "MAP failed in open_i");

    if(!mem.open(numbufs, bufsz)){
        ALOGE("%s: Failed to open", __func__);
        mem.close();
        return false;
    }

    OVASSERT(MAP_FAILED != mem.addr(), "MAP failed");
    OVASSERT(mem.getFD() != -1, "getFd is -1");

    mData.data.memory_id = mem.getFD();
    mRotDataInfo.dst.memory_id = mem.getFD();
    mRotDataInfo.dst.offset = 0;
    mMem.curr().m = mem;
    return true;
}

bool MdpRot::RotMem::close() {
    bool ret = true;
    for(uint32_t i=0; i < RotMem::MAX_ROT_MEM; ++i) {
        // skip current, and if valid, close
        if(m[i].valid() && (m[i].close() != 0)) {
            ALOGE("%s error in closing prev rot mem %d", __FUNCTION__, i);
            ret = false;
        }
    }
    return ret;
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

bool MdpRot::unmapNonCurrent() {
    bool ret = true;
    for(uint32_t i=0; i < RotMem::MAX_ROT_MEM; ++i) {
        // skip current, and if valid, close
        if(i != mMem._curr % RotMem::MAX_ROT_MEM &&
                mMem.m[i].valid() &&
                !mMem.m[i].close()) {
            ALOGE("%s error in closing prev rot mem %d", __FUNCTION__, i);
            ret = false;
        }
    }
    return ret;
}

bool MdpRot::remap(uint32_t numbufs,
        const utils::PipeArgs& args) {
    // if current size changed, remap
    if(args.whf.size == mMem.curr().size()) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: same size %d", __FUNCTION__, args.whf.size);
        return true;
    }

    // remap only if we have orientation.
    // If rotFlags are ENABLED, it means we need rotation bufs
    // even when orientation is 0
    if(utils::OVERLAY_TRANSFORM_0 == args.orientation &&
            utils::ROT_FLAG_ENABLED != args.rotFlags) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: orientation=%d, rotFlags=%d",
                __FUNCTION__, args.orientation, args.rotFlags);
        return true;
    }

    ALOGE_IF(DEBUG_OVERLAY, "%s: size changed - remapping", __FUNCTION__);
    OVASSERT(!mMem.prev().valid(), "Prev should not be valid");

    // remap and have the current to be the new one.
    // ++mMem will make curr to be prev, and prev will be curr
    ++mMem;
    if(!open_i(numbufs, args.whf.size)) {
        ALOGE("%s Error could not open", __FUNCTION__);
        return false;
    }
    OVASSERT(numbufs <= ROT_MAX_BUF_OFFSET,
            "Numbufs %d > ROT_MAX_BUF_OFFSET", numbufs);
    for (uint32_t i = 0; i < numbufs; ++i) {
        mMem.curr().mRotOffset[i] = i * args.whf.size;
    }
    return true;
}

bool MdpRot::start() {
    if(!overlay::mdp_wrapper::startRotator(mFd.getFD(), mRotImgInfo)) {
        ALOGE("MdpRot start failed");
        this->dump();
        return false;
    }
    mRotDataInfo.session_id = mRotImgInfo.session_id;
    return true;
}

void MdpRot::reset() {
    ovutils::memset0(mRotImgInfo);
    ovutils::memset0(mRotDataInfo);
    ovutils::memset0(mData);
    ovutils::memset0(mMem.curr().mRotOffset);
    ovutils::memset0(mMem.prev().mRotOffset);
    mMem.curr().mCurrOffset = 0;
    mMem.prev().mCurrOffset = 0;
    isSrcFB = false;
}

bool MdpRot::prepareQueueBuf(uint32_t offset) {
    // FIXME if it fails, what happens to the above current item?
    if(enabled()) {
        OVASSERT(mMem.curr().m.numBufs(),
                "prepareQueueBuf numbufs is 0");

        // If the rotator source is FB
        if(isSrcFB) {
            mRotDataInfo.src.flags |= MDP_MEMORY_ID_TYPE_FB;
        }

        mRotDataInfo.src.offset = offset;
        mRotDataInfo.dst.offset =
                mMem.curr().mRotOffset[mMem.curr().mCurrOffset];
        mMem.curr().mCurrOffset =
                (mMem.curr().mCurrOffset + 1) % mMem.curr().m.numBufs();
        if(!overlay::mdp_wrapper::rotate(mFd.getFD(), mRotDataInfo)) {
            ALOGE("MdpRot failed rotate");
            return false;
        }
        mData.data.offset =  mRotDataInfo.dst.offset;
    }
    return true;
}


bool MdpRot::play(int fd) {
    if(!overlay::mdp_wrapper::play(fd, mData)) {
        ALOGE("MdpRot failed to play with fd=%d", fd);
        return false;
    }

    // if the prev mem is valid, we need to close
    if(mMem.prev().valid()) {
        // FIXME FIXME FIXME if no wait for vsync the above
        // play will return immediatly and might cause
        // tearing when prev.close is called.
        if(!mMem.prev().close()) {
            ALOGE("%s error in closing prev rot mem", __FUNCTION__);
        }
    }
    return true;
}

///// Null Rot ////

mdp_overlay NullRotator::setInfo(
        const utils::PipeArgs& args,
        const mdp_overlay& o) {
    return utils::setInfoNullRot(args, o);
}

///// Rotator ////

mdp_overlay Rotator::setInfo(
        const utils::PipeArgs& args,
        const mdp_overlay& o)
{
    return utils::setInfoRot(args, o);
}

bool Rotator::overlayTransform(MdpCtrl& mdp,
        utils::eTransform& rot)
{
    ALOGE_IF(DEBUG_OVERLAY, "%s: rot=%d", __FUNCTION__, rot);
    switch(int(rot)) {
        case 0:
        case HAL_TRANSFORM_FLIP_H:
        case HAL_TRANSFORM_FLIP_V:
            overlayTransFlipHV(mdp, rot);
            break;
        case HAL_TRANSFORM_ROT_90:
        case (HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_H):
        case (HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_V):
            overlayTransFlipRot90(mdp, rot);
            break;
        case HAL_TRANSFORM_ROT_180:
            overlayTransFlipRot180(mdp);
            break;
        case HAL_TRANSFORM_ROT_270:
            overlayTransFlipRot270(mdp);
            break;
        default:
            ALOGE("%s: Error due to unknown rot value %d", __FUNCTION__, rot);
            return false;
    }

    /* everything below is rotation related */
    int r = utils::getMdpOrient(rot);
    ALOGE_IF(DEBUG_OVERLAY, "%s: r=%d", __FUNCTION__, r);
    if (r == -1) {
        ALOGE("Ctrl setParameter rot it -1");
        return false;
    }

    // Need to have both in sync
    mdp.setUserData(r);
    this->setRotations(r);
    this->setDisable();
    if(r) {
        this->setEnable();
    }

    /* set src format using rotation info
     * e.g. (12-->5 in case of rotation) */
    mdp.setSrcFormat(this->getSrcWhf());

    // based on 90/270 set flags
    mdp.setRotationFlags();
    return true;
}

void Rotator::overlayTransFlipHV(MdpCtrl& mdp,
        utils::eTransform& rot)
{
    int val = mdp.getUserData();
    ALOGE_IF(DEBUG_OVERLAY, "%s: prev=%d", __FUNCTION__, val);
    utils::Dim d   = mdp.getSrcRectDim();
    utils::Whf whf = mdp.getSrcWhf();
    if (val == MDP_ROT_90) {
        int tmp = d.y;
        d.y = compute(whf.w,
                d.x,
                d.w);
        d.x = tmp;
        mdp.setSrcRectDim(d);
        utils::swapOVRotWidthHeight(mRot, mdp);
    }
    else if (val == MDP_ROT_270) {
        int tmp = d.x;
        d.x = compute(whf.h,
                d.y,
                d.h);
        d.y = tmp;
        mdp.setSrcRectDim(d);
        utils::swapOVRotWidthHeight(mRot, mdp);
    }
}

void Rotator::overlayTransFlipRot90(MdpCtrl& mdp,
        utils::eTransform& rot)
{
    int val = mdp.getUserData();
    ALOGE_IF(DEBUG_OVERLAY, "%s: prev=%d", __FUNCTION__, val);
    utils::Dim d   = mdp.getSrcRectDim();
    utils::Whf whf = mdp.getSrcWhf();
    if (val == MDP_ROT_270) {
        d.x = compute(whf.w,
                d.x,
                d.w);
        d.y = compute(whf.h,
                d.y,
                d.h);
    }
    else if (val == MDP_ROT_NOP || val == MDP_ROT_180) {
        int tmp = d.x;
        d.x = compute(whf.h,
                d.y,
                d.h);
        d.y = tmp;
        mdp.setSrcRectDim(d);
        utils::swapOVRotWidthHeight(mRot, mdp);
    }
}

void Rotator::overlayTransFlipRot180(MdpCtrl& mdp)
{
    int val = mdp.getUserData();
    ALOGE_IF(DEBUG_OVERLAY, "%s: prev=%d", __FUNCTION__, val);
    utils::Dim d   = mdp.getSrcRectDim();
    utils::Whf whf = mdp.getSrcWhf();
    if (val == MDP_ROT_270) {
        int tmp = d.y;
        d.y = compute(whf.w,
                d.x,
                d.w);
        d.x = tmp;
        mdp.setSrcRectDim(d);
        utils::swapOVRotWidthHeight(mRot, mdp);
    }
    else if (val == MDP_ROT_90) {
        int tmp = d.x;
        d.x = compute(whf.h,
                d.y,
                d.h);
        d.y = tmp;
        mdp.setSrcRectDim(d);
        utils::swapOVRotWidthHeight(mRot, mdp);
    }
}

void Rotator::overlayTransFlipRot270(MdpCtrl& mdp)
{
    int val = mdp.getUserData();
    ALOGE_IF(DEBUG_OVERLAY, "%s: prev=%d", __FUNCTION__, val);
    utils::Dim d   = mdp.getSrcRectDim();
    utils::Whf whf = mdp.getSrcWhf();
    if (val == MDP_ROT_90) {
        d.y = compute(whf.h,
                d.y,
                d.h);
        d.x = compute(whf.w,
                d.x,
                d.w);
    }
    else if (val == MDP_ROT_NOP || val == MDP_ROT_180) {
        int tmp = d.y;
        d.y = compute(whf.w,
                d.x,
                d.w);
        d.x = tmp;
        mdp.setSrcRectDim(d);
        utils::swapOVRotWidthHeight(mRot, mdp);
    }
}

void MdpRot::dump() const {
    ALOGE("== Dump MdpRot start ==");
    mFd.dump();
    mMem.curr().m.dump();
    mdp_wrapper::dump("mRotImgInfo", mRotImgInfo);
    mdp_wrapper::dump("mRotDataInfo", mRotDataInfo);
    mdp_wrapper::dump("mData", mData);
    ALOGE("== Dump MdpRot end ==");
}
}
