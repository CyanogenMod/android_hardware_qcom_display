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

#ifndef OVERLAY_MDP_H
#define OVERLAY_MDP_H

#include <linux/msm_mdp.h>

#include "overlayUtils.h"
#include "mdpWrapper.h"

namespace overlay{

/*
* Mdp Ctrl holds corresponding fd and MDP related struct.
* It is simple wrapper to MDP services
* */
class MdpCtrl {
public:
    /* ctor reset */
    explicit MdpCtrl();

    /* dtor close */
    ~MdpCtrl();

    /* init underlying device using fbnum */
    bool init(uint32_t fbnum);

    /* unset overlay, reset and close fd */
    bool close();

    /* reset and set ov id to -1 / MSMFB_NEW_REQUEST */
    void reset();

    /* get orient / user_data[0] */
    int getOrient() const;

    /* returns session id */
    int getPipeId() const;

    /* returns the fd associated to ctrl*/
    int getFd() const;

    /* Get screen info. out: info*/
    bool getScreenInfo(utils::ScreenInfo& info);

    /* overlay get */
    bool get();

    /* returns flags from mdp structure */
    int getFlags() const;

    /* set flags to mdp structure */
    void setFlags(int f);

    /* set z order */
    void setZ(utils::eZorder z);

    /* set isFg flag */
    void setIsFg(utils::eIsFg isFg);

    /* calls overlay set
     * Set would always consult last good known ov instance.
     * Only if it is different, set would actually exectue ioctl.
     * On a sucess ioctl. last good known ov instance is updated */
    bool set();

    /* return a copy of src whf*/
    utils::Whf getSrcWhf() const;

    /* set src whf */
    void setSrcWhf(const utils::Whf& whf);

    /* adjust source width height format based on rot info */
    void adjustSrcWhf(const bool& rotUsed);

    /* swap src w/h*/
    void swapSrcWH();

    /* swap src rect w/h */
    void swapSrcRectWH();

    /* returns a copy to src rect dim */
    utils::Dim getSrcRectDim() const;

    /* set src/dst rect dim */
    void setSrcRectDim(const utils::Dim d);
    void setDstRectDim(const utils::Dim d);

    /* returns a copy ro dst rect dim */
    utils::Dim getDstRectDim() const;

    /* returns user_data[0]*/
    int getUserData() const;

    /* sets user_data[0] */
    void setUserData(int v);

    /* return true if current overlay is different
     * than last known good overlay */
    bool ovChanged() const;

    /* save mOVInfo to be last known good ov*/
    void save();

    /* restore last known good ov to be the current */
    void restore();

    /* Sets the source total width, height, format */
    bool setSource(const utils::PipeArgs& pargs);

    /*
     * Sets ROI, the unpadded region, for source buffer.
     * Should be called before a setPosition, for small clips.
     * Dim - ROI dimensions.
     */
    bool setCrop(const utils::Dim& d);

    bool setTransform(const utils::eTransform& orient, const bool& rotUsed);

    /* given a dim and w/h, set overlay dim */
    bool setPosition(const utils::Dim& dim, int w, int h);

    /* using user_data, sets/unsets roationvalue in mdp flags */
    void setRotationFlags();

    /* dump state of the object */
    void dump() const;

private:

    /* helper functions for overlayTransform */
    void doTransform();
    void overlayTransFlipH();
    void overlayTransFlipV();
    void overlayTransRot90();

    utils::eTransform mOrientation; //Holds requested orientation
    bool mRotUsed; //whether rotator should be used even if requested
                   //orientation is 0.

    /* last good known ov info */
    mdp_overlay   mLkgo;

    /* Actual overlay mdp structure */
    mdp_overlay   mOVInfo;

    /* FD for the mdp fbnum */
    OvFD          mFd;
};


/* MDP 3D related ctrl */
class MdpCtrl3D {
public:
    /* ctor reset data */
    MdpCtrl3D();
    /* calls MSMFB_OVERLAY_3D */
    bool close();
    /* set w/h. format is ignored*/
    void setWh(const utils::Whf& whf);
    /* set is_3d calls MSMFB_OVERLAY_3D */
    bool useVirtualFB();
    /* set fd to be used in ioctl */
    void setFd(int fd);
    /* dump */
    void dump() const;
private:
    /* reset */
    void reset();
    /* actual MSM 3D info */
    msmfb_overlay_3d m3DOVInfo;
    /* FD for the mdp 3D */
    OvFD mFd;
};

/* MDP data */
class MdpData {
public:
    /* ctor reset data */
    explicit MdpData();

    /* dtor close*/
    ~MdpData();

    /* init FD */
    bool init(uint32_t fbnum);

    /* memset0 the underlying mdp object */
    void reset();

    /* close fd, and reset */
    bool close();

    /* set id of mdp data */
    void setPipeId(int id);

    /* return ses id of data */
    int getPipeId() const;

    /* get underlying fd*/
    int getFd() const;

    /* get memory_id */
    int getSrcMemoryId() const;

    /* calls wrapper play */
    bool play(int fd, uint32_t offset);

    /* dump state of the object */
    void dump() const;
private:

    /* actual overlay mdp data */
    msmfb_overlay_data mOvData;

    /* fd to mdp fbnum */
    OvFD mFd;
};

//--------------Inlines---------------------------------
namespace utils {
inline bool openDev(OvFD& fd, int fbnum,
        const char* const devpath,
        int flags) {
    return overlay::open(fd, fbnum, devpath, flags);
}
}
namespace {
// just a helper func for common operations x-(y+z)
int compute(uint32_t x, uint32_t y, uint32_t z) {
    return x-(y+z);
}
}

/////   MdpCtrl  //////

inline MdpCtrl::MdpCtrl() {
    reset();
}

inline MdpCtrl::~MdpCtrl() {
    close();
}

inline int MdpCtrl::getOrient() const {
    return getUserData();
}

inline int MdpCtrl::getPipeId() const {
    return mOVInfo.id;
}

inline int MdpCtrl::getFd() const {
    return mFd.getFD();
}

inline int MdpCtrl::getFlags() const {
    return mOVInfo.flags;
}

inline void MdpCtrl::setFlags(int f) {
    mOVInfo.flags = f;
}

inline void MdpCtrl::setZ(overlay::utils::eZorder z) {
    mOVInfo.z_order = z;
}

inline void MdpCtrl::setIsFg(overlay::utils::eIsFg isFg) {
    mOVInfo.is_fg = isFg;
}

inline bool MdpCtrl::ovChanged() const {
    // 0 means same
    if(0 == ::memcmp(&mOVInfo, &mLkgo, sizeof (mdp_overlay))) {
        return false;
    }
    return true;
}

inline void MdpCtrl::save() {
    if(static_cast<ssize_t>(mOVInfo.id) == MSMFB_NEW_REQUEST) {
        ALOGE("MdpCtrl current ov has id -1, will not save");
        return;
    }
    mLkgo = mOVInfo;
}

inline void MdpCtrl::restore() {
    if(static_cast<ssize_t>(mLkgo.id) == MSMFB_NEW_REQUEST) {
        ALOGE("MdpCtrl Lkgo ov has id -1, will not restore");
        return;
    }
    mOVInfo = mLkgo;
}

inline overlay::utils::Whf MdpCtrl::getSrcWhf() const {
    return utils::Whf(  mOVInfo.src.width,
                        mOVInfo.src.height,
                        mOVInfo.src.format);
}

inline void MdpCtrl::setSrcWhf(const overlay::utils::Whf& whf) {
    mOVInfo.src.width  = whf.w;
    mOVInfo.src.height = whf.h;
    mOVInfo.src.format = whf.format;
}

inline overlay::utils::Dim MdpCtrl::getSrcRectDim() const {
    return utils::Dim(  mOVInfo.src_rect.x,
                        mOVInfo.src_rect.y,
                        mOVInfo.src_rect.w,
                        mOVInfo.src_rect.h);
}

inline void MdpCtrl::setSrcRectDim(const overlay::utils::Dim d) {
    mOVInfo.src_rect.x = d.x;
    mOVInfo.src_rect.y = d.y;
    mOVInfo.src_rect.w = d.w;
    mOVInfo.src_rect.h = d.h;
}

inline overlay::utils::Dim MdpCtrl::getDstRectDim() const {
    return utils::Dim(  mOVInfo.dst_rect.x,
                        mOVInfo.dst_rect.y,
                        mOVInfo.dst_rect.w,
                        mOVInfo.dst_rect.h);
}

inline void MdpCtrl::setDstRectDim(const overlay::utils::Dim d) {
    mOVInfo.dst_rect.x = d.x;
    mOVInfo.dst_rect.y = d.y;
    mOVInfo.dst_rect.w = d.w;
    mOVInfo.dst_rect.h = d.h;
}

inline int MdpCtrl::getUserData() const { return mOVInfo.user_data[0]; }

inline void MdpCtrl::setUserData(int v) { mOVInfo.user_data[0] = v; }

inline void MdpCtrl::setRotationFlags() {
    const int u = getUserData();
    if (u == MDP_ROT_90 || u == MDP_ROT_270)
        mOVInfo.flags |= MDP_SOURCE_ROTATED_90;
    else
        mOVInfo.flags &= ~MDP_SOURCE_ROTATED_90;
}

inline void MdpCtrl::swapSrcWH() {
    utils::swap(mOVInfo.src.width,
            mOVInfo.src.height);
}

inline void MdpCtrl::swapSrcRectWH() {
    utils::swap(mOVInfo.src_rect.w,
            mOVInfo.src_rect.h);
}

inline void MdpCtrl::overlayTransFlipH()
{
    utils::Dim d   = getSrcRectDim();
    utils::Whf whf = getSrcWhf();
    d.x = compute(whf.w, d.x, d.w);
    setSrcRectDim(d);
}

inline void MdpCtrl::overlayTransFlipV()
{
    utils::Dim d   = getSrcRectDim();
    utils::Whf whf = getSrcWhf();
    d.y = compute(whf.h, d.y, d.h);
    setSrcRectDim(d);
}

inline void MdpCtrl::overlayTransRot90()
{
    utils::Dim d   = getSrcRectDim();
    utils::Whf whf = getSrcWhf();
    int tmp = d.x;
    d.x = compute(whf.h,
            d.y,
            d.h);
    d.y = tmp;
    setSrcRectDim(d);
    swapSrcWH();
    swapSrcRectWH();
}


///////    MdpCtrl3D //////

inline MdpCtrl3D::MdpCtrl3D() { reset(); }
inline bool MdpCtrl3D::close() {
    if (m3DOVInfo.is_3d) {
        m3DOVInfo.is_3d = 0;
        if(!mdp_wrapper::set3D(mFd.getFD(), m3DOVInfo)) {
            ALOGE("MdpCtrl3D close failed set3D with 0");
            return false;
        }
    }
    reset();
    return true;
}
inline void MdpCtrl3D::reset() {
    utils::memset0(m3DOVInfo);
}

inline void MdpCtrl3D::setFd(int fd) {
    mFd.copy(fd);
    OVASSERT(mFd.valid(), "MdpCtrl3D setFd, FD should be valid");
}

inline void MdpCtrl3D::setWh(const utils::Whf& whf) {
    // ignore fmt. Needed for useVirtualFB callflow
    m3DOVInfo.width = whf.w;
    m3DOVInfo.height = whf.h;
}

inline bool MdpCtrl3D::useVirtualFB() {
    if(!m3DOVInfo.is_3d) {
        m3DOVInfo.is_3d = 1;
        if(!mdp_wrapper::set3D(mFd.getFD(), m3DOVInfo)) {
            ALOGE("MdpCtrl3D close failed set3D with 0");
            return false;
        }
    }
    return true;
}

///////    MdpData   //////

inline MdpData::MdpData() { reset(); }

inline MdpData::~MdpData() { close(); }

inline bool MdpData::init(uint32_t fbnum) {
    // FD init
    if(!utils::openDev(mFd, fbnum, Res::fbPath, O_RDWR)){
        ALOGE("Ctrl failed to init fbnum=%d", fbnum);
        return false;
    }
    return true;
}

inline void MdpData::reset() {
    overlay::utils::memset0(mOvData);
    mOvData.data.memory_id = -1;
}

inline bool MdpData::close() {
    if(-1 == mOvData.data.memory_id) return true;
    reset();
    if(!mFd.close()) {
        return false;
    }
    return true;
}

inline int MdpData::getSrcMemoryId() const { return mOvData.data.memory_id; }

inline void MdpData::setPipeId(int id) { mOvData.id = id; }

inline int MdpData::getPipeId() const { return mOvData.id; }

inline int MdpData::getFd() const { return mFd.getFD(); }

inline bool MdpData::play(int fd, uint32_t offset) {
    mOvData.data.memory_id = fd;
    mOvData.data.offset = offset;
    if(!mdp_wrapper::play(mFd.getFD(), mOvData)){
        ALOGE("MdpData failed to play");
        dump();
        return false;
    }
    return true;
}

} // overlay

#endif // OVERLAY_MDP_H
