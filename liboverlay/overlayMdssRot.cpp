/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
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

#include <math.h>
#include "overlayUtils.h"
#include "overlayRotator.h"

#define DEBUG_MDSS_ROT 0

#ifdef VENUS_COLOR_FORMAT
#include <media/msm_media_info.h>
#else
#define VENUS_BUFFER_SIZE(args...) 0
#endif

#ifndef MDSS_MDP_ROT_ONLY
#define MDSS_MDP_ROT_ONLY 0x80
#endif

#define MDSS_ROT_MASK (MDP_ROT_90 | MDP_FLIP_UD | MDP_FLIP_LR)

namespace ovutils = overlay::utils;

namespace overlay {
using namespace utils;

MdssRot::MdssRot() {
    reset();
    init();
}

MdssRot::~MdssRot() { close(); }

bool MdssRot::enabled() const { return mEnabled; }

void MdssRot::setRotations(uint32_t flags) { mRotInfo.flags |= flags; }

int MdssRot::getSrcMemId() const {
    return mRotData.data.memory_id;
}

int MdssRot::getDstMemId() const {
    return mRotData.dst_data.memory_id;
}

uint32_t MdssRot::getSrcOffset() const {
    return mRotData.data.offset;
}

uint32_t MdssRot::getDstOffset() const {
    return mRotData.dst_data.offset;
}

uint32_t MdssRot::getDstFormat() const {
    //For mdss src and dst formats are same
    return mRotInfo.src.format;
}

utils::Whf MdssRot::getDstWhf() const {
    //For Mdss dst_rect itself represents buffer dimensions. We ignore actual
    //aligned values during buffer allocation. Also the driver overwrites the
    //src.format field if destination format is different.
    //This implementation detail makes it possible to retrieve w,h even before
    //buffer allocation, which happens in queueBuffer.
    return utils::Whf(mRotInfo.dst_rect.w, mRotInfo.dst_rect.h,
            mRotInfo.src.format);
}

utils::Dim MdssRot::getDstDimensions() const {
    return utils::Dim(mRotInfo.dst_rect.x, mRotInfo.dst_rect.y,
            mRotInfo.dst_rect.w, mRotInfo.dst_rect.h);
}

uint32_t MdssRot::getSessId() const { return mRotInfo.id; }

void MdssRot::save() {
    mLSRotInfo = mRotInfo;
}

bool MdssRot::rotConfChanged() const {
    // 0 means same
    if(0 == ::memcmp(&mRotInfo, &mLSRotInfo,
                     sizeof (mdp_overlay))) {
        return false;
    }
    return true;
}

bool MdssRot::init() {
    if(!utils::openDev(mFd, 0, Res::fbPath, O_RDWR)) {
        ALOGE("MdssRot failed to init fb0");
        return false;
    }
    return true;
}

bool MdssRot::isRotBufReusable(const utils::eMdpFlags& flags) {
    if((mRotInfo.flags != 0) &&
      (((mRotInfo.flags & ovutils::OV_MDP_SECURE_OVERLAY_SESSION) &&
         !(flags & ovutils::OV_MDP_SECURE_OVERLAY_SESSION)) ||
      (!(mRotInfo.flags & ovutils::OV_MDP_SECURE_OVERLAY_SESSION) &&
         (flags & ovutils::OV_MDP_SECURE_OVERLAY_SESSION)))) {
          ALOGE("%s: Rotator buffer usage flag is changed, failing",
                     __FUNCTION__);
          return false;
    }
    return true;
}

void MdssRot::setSource(const overlay::utils::Whf& awhf) {
    utils::Whf whf(awhf);

    mRotInfo.src.format = whf.format;
    mRotInfo.src.width = whf.w;
    mRotInfo.src.height = whf.h;
}

void MdssRot::setCrop(const utils::Dim& crop) {
    mRotInfo.src_rect.x = crop.x;
    mRotInfo.src_rect.y = crop.y;
    mRotInfo.src_rect.w = crop.w;
    mRotInfo.src_rect.h = crop.h;
}

void MdssRot::setDownscale(int downscale) {
    mDownscale = downscale;
}

void MdssRot::setFlags(const utils::eMdpFlags& flags) {
    mRotInfo.flags = flags;
}

void MdssRot::setTransform(const utils::eTransform& rot)
{
    // reset rotation flags to avoid stale orientation values
    mRotInfo.flags &= ~MDSS_ROT_MASK;
    int flags = utils::getMdpOrient(rot);
    if (flags != -1)
        setRotations(flags);
    mOrientation = static_cast<utils::eTransform>(flags);
    ALOGE_IF(DEBUG_OVERLAY, "%s: rot=%d", __FUNCTION__, flags);
}

void MdssRot::doTransform() {
    mRotInfo.flags |= mOrientation;
    if(mOrientation & utils::OVERLAY_TRANSFORM_ROT_90)
        utils::swap(mRotInfo.dst_rect.w, mRotInfo.dst_rect.h);
}

bool MdssRot::commit() {
    Dim adjCrop(mRotInfo.src_rect.x,mRotInfo.src_rect.y,
            mRotInfo.src_rect.w,mRotInfo.src_rect.h);
    adjCrop = getFormatAdjustedCrop(adjCrop, mRotInfo.src.format,
            mRotInfo.flags & utils::OV_MDP_DEINTERLACE);
    adjCrop = getDownscaleAdjustedCrop(adjCrop, mDownscale);

    mRotInfo.src_rect.x = adjCrop.x;
    mRotInfo.src_rect.y = adjCrop.y;
    mRotInfo.src_rect.w = adjCrop.w;
    mRotInfo.src_rect.h = adjCrop.h;

    mRotInfo.dst_rect.x = 0;
    mRotInfo.dst_rect.y = 0;
    mRotInfo.dst_rect.w = mDownscale ?
            mRotInfo.src_rect.w / mDownscale : mRotInfo.src_rect.w;
    mRotInfo.dst_rect.h = mDownscale ?
            mRotInfo.src_rect.h / mDownscale : mRotInfo.src_rect.h;
    //Clear for next round
    mDownscale = 0;

    doTransform();

    mRotInfo.flags |= MDSS_MDP_ROT_ONLY;
    mEnabled = true;
    if(!overlay::mdp_wrapper::setOverlay(mFd.getFD(), mRotInfo)) {
        ALOGE("MdssRot commit failed!");
        dump();
        return (mEnabled = false);
    }
    mRotData.id = mRotInfo.id;
    return true;
}

bool MdssRot::queueBuffer(int fd, uint32_t offset) {
    if(enabled() and (not isRotCached(fd,offset))) {
        int prev_fd = getSrcMemId();
        uint32_t prev_offset = getSrcOffset();

        mRotData.data.memory_id = fd;
        mRotData.data.offset = offset;

        if(false == remap(RotMem::ROT_NUM_BUFS)) {
            ALOGE("%s Remap failed, not queuing", __FUNCTION__);
            return false;
        }

        mRotData.dst_data.offset =
                mMem.mRotOffset[mMem.mCurrIndex];

        if(!overlay::mdp_wrapper::play(mFd.getFD(), mRotData)) {
            ALOGE("MdssRot play failed!");
            dump();
            mRotData.data.memory_id = prev_fd;
            mRotData.data.offset = prev_offset;
            return false;
        }
        save();
        mMem.mCurrIndex =
                (mMem.mCurrIndex + 1) % mMem.mem.numBufs();
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
    mMem.mem = mem;
    return true;
}

bool MdssRot::remap(uint32_t numbufs) {
    // Calculate the size based on rotator's dst format, w and h.
    uint32_t opBufSize = calcOutputBufSize();
    // If current size changed, remap
    if(opBufSize == mMem.size()) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: same size %d", __FUNCTION__, opBufSize);
        return true;
    }

    ALOGE_IF(DEBUG_OVERLAY, "%s: size changed - remapping", __FUNCTION__);

    if(!mMem.close()) {
        ALOGE("%s error in closing prev rot mem", __FUNCTION__);
        return false;
    }

    if(!open_i(numbufs, opBufSize)) {
        ALOGE("%s Error could not open", __FUNCTION__);
        return false;
    }

    for (uint32_t i = 0; i < numbufs; ++i) {
        mMem.mRotOffset[i] = i * opBufSize;
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
    ovutils::memset0(mLSRotInfo);
    ovutils::memset0(mRotData);
    mRotData.data.memory_id = -1;
    mRotInfo.id = MSMFB_NEW_REQUEST;
    ovutils::memset0(mMem.mRotOffset);
    mMem.mCurrIndex = 0;
    mOrientation = utils::OVERLAY_TRANSFORM_0;
    mDownscale = 0;
}

void MdssRot::dump() const {
    ALOGE("== Dump MdssRot start ==");
    mFd.dump();
    mMem.mem.dump();
    mdp_wrapper::dump("mRotInfo", mRotInfo);
    mdp_wrapper::dump("mRotData", mRotData);
    ALOGE("== Dump MdssRot end ==");
}

uint32_t MdssRot::calcOutputBufSize() {
    uint32_t opBufSize = 0;
    ovutils::Whf destWhf(mRotInfo.dst_rect.w, mRotInfo.dst_rect.h,
            mRotInfo.src.format); //mdss src and dst formats are same.

    if (mRotInfo.flags & ovutils::OV_MDSS_MDP_BWC_EN) {
        opBufSize = calcCompressedBufSize(destWhf);
    } else {
        opBufSize = Rotator::calcOutputBufSize(destWhf);
    }

    return opBufSize;
}

void MdssRot::getDump(char *buf, size_t len) const {
    ovutils::getDump(buf, len, "MdssRotCtrl", mRotInfo);
    ovutils::getDump(buf, len, "MdssRotData", mRotData);
}

// Calculate the compressed o/p buffer size for BWC
uint32_t MdssRot::calcCompressedBufSize(const ovutils::Whf& destWhf) {
    uint32_t bufSize = 0;
    //Worst case alignments
    int aWidth = ovutils::align(destWhf.w, 64);
    int aHeight = ovutils::align(destWhf.h, 4);
    /*
       Format           |   RAU size (width x height)
       ----------------------------------------------
       ARGB             |       32 pixel x 4 line
       RGB888           |       32 pixel x 4 line
       Y (Luma)         |       64 pixel x 4 line
       CRCB 420         |       32 pixel x 2 line
       CRCB 422 H2V1    |       32 pixel x 4 line
       CRCB 422 H1V2    |       64 pixel x 2 line

       Metadata requirements:-
       1 byte meta data for every 8 RAUs
       2 byte meta data per RAU
     */

    //These blocks attempt to allocate for the worst case in each of the
    //respective format classes, yuv/rgb. The table above is for reference
    if(utils::isYuv(destWhf.format)) {
        int yRauCount = aWidth / 64; //Y
        int cRauCount = aWidth / 32; //C
        int yStride = (64 * 4 * yRauCount) + alignup(yRauCount, 8) / 8;
        int cStride = ((32 * 2 * cRauCount) + alignup(cRauCount, 8) / 8) * 2;
        int yStrideOffset = (aHeight / 4);
        int cStrideOffset = (aHeight / 2);
        bufSize = (yStride * yStrideOffset + cStride * cStrideOffset) +
                (yRauCount * yStrideOffset * 2) +
                (cRauCount * cStrideOffset * 2) * 2;
        ALOGD_IF(DEBUG_MDSS_ROT, "%s:YUV Y RAU Count = %d C RAU Count = %d",
                __FUNCTION__, yRauCount, cRauCount);
    } else {
        int rauCount = aWidth / 32;
        //Single plane
        int stride = (32 * 4 * rauCount) + alignup(rauCount, 8) / 8;
        int strideOffset = (aHeight / 4);
        bufSize = (stride * strideOffset * 4 /*bpp*/) +
            (rauCount * strideOffset * 2);
        ALOGD_IF(DEBUG_MDSS_ROT, "%s:RGB RAU count = %d", __FUNCTION__,
                rauCount);
    }

    ALOGD_IF(DEBUG_MDSS_ROT, "%s: aligned width = %d, aligned height = %d "
            "Buf Size = %d", __FUNCTION__, aWidth, aHeight, bufSize);

    return bufSize;
}

int MdssRot::getDownscaleFactor(const int& srcW, const int& srcH,
        const int& dstW, const int& dstH, const uint32_t& mdpFormat,
        const bool& isInterlaced) {
    if(not srcW or not srcH or not dstW or not dstH or isInterlaced) return 0;

    Dim crop(0, 0, srcW, srcH);
    Dim adjCrop = getFormatAdjustedCrop(crop, mdpFormat,
            false /*isInterlaced */);

    uint32_t downscale = min((adjCrop.w / dstW), (adjCrop.h / dstH));
    //Reduced to a power of 2
    downscale = (uint32_t) powf(2.0f, floorf(log2f((float)downscale)));

    if(downscale < 2 or downscale > 32) return 0;

    //Allow only 1 line or pixel to be chopped off since the source needs to
    //be aligned to downscale. Progressively try with smaller downscale to see
    //if we can satisfy the threshold
    //For YUV the loop shouldnt be needed, unless in exceptional cases
    Dim dsAdjCrop = getDownscaleAdjustedCrop(adjCrop, downscale);
    while(downscale > 2 and (adjCrop.w > dsAdjCrop.w or
            adjCrop.h > dsAdjCrop.h)) {
        downscale /= 2;
        dsAdjCrop = getDownscaleAdjustedCrop(adjCrop, downscale);
    }

    if(not dsAdjCrop.w or not dsAdjCrop.h) return 0;
    return downscale;
}

Dim MdssRot::getFormatAdjustedCrop(const Dim& crop,
            const uint32_t& mdpFormat, const bool& isInterlaced) {
    Dim adjCrop = crop;
    if (isYuv(mdpFormat)) {
        normalizeCrop(adjCrop.x, adjCrop.w);
        normalizeCrop(adjCrop.y, adjCrop.h);
        // For interlaced, crop.h should be 4-aligned
        if (isInterlaced and (adjCrop.h % 4))
            adjCrop.h = aligndown(adjCrop.h, 4);
    }
    return adjCrop;
}

Dim MdssRot::getDownscaleAdjustedCrop(const Dim& crop,
        const uint32_t& downscale) {
    uint32_t alignedSrcW = aligndown(crop.w, downscale * 2);
    uint32_t alignedSrcH = aligndown(crop.h, downscale * 2);
    return Dim(crop.x, crop.y, alignedSrcW, alignedSrcH);
}

} // namespace overlay
