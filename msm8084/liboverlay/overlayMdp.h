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
#include "overlayRotator.h"

namespace overlay{

class RotatorBase;

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

    /* Open underlying device using fbnum */
    bool open(uint32_t fbnum);

    /* unset overlay, reset and close fd */
    bool close();

    /* reset and set ov id to -1*/
    void reset();

    /* get orient / user_data[0] */
    int getOrient() const;

    /* returns session id */
    int getId() const;

    /* returns the fd associated to ctrl*/
    int getFd() const;

    /* Get screen info. out: info*/
    bool getScreenInfo(utils::ScreenInfo& info);

    /* overlay get */
    bool get();

    /* returns flags from mdp structure.
     * Flags are WAIT/NOWAIT/PIPE SHARED*/
    int getFlags() const;

    /* set flags to mdp structure */
    void setFlags(int f);

    /* code taken from OverlayControlChannel::setOverlayInformation  */
    bool setInfo(RotatorBase* r,
            const utils::PipeArgs& args,
            const utils::ScreenInfo& info);

    /* given whf, update src */
    void updateSource(RotatorBase* r,
            const utils::PipeArgs& args,
            const utils::ScreenInfo& info);

    /* set z order */
    void setZ(utils::eZorder z);

    /* set Wait/nowait */
    void setWait(utils::eWait wait);

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

    /* set source format based on rot info */
    void setSrcFormat(const utils::Whf& whf);

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
     * than lask known good overlay */
    bool ovChanged() const;

    /* save mOVInfo to be last known good ov*/
    void save();

    /* restore last known good ov to be the current */
    void restore();

    /*
     * Sets ROI, the unpadded region, for source buffer.
     * Should be called before a setPosition, for small clips.
     * Dim - ROI dimensions.
     */
    bool setCrop(const utils::Dim& d);

    /* given a dim and w/h, set overlay dim */
    bool setPosition(const utils::Dim& dim, int w, int h);

    /* using user_data, sets/unsets roationvalue in mdp flags */
    void setRotationFlags();

    /* dump state of the object */
    void dump() const;
private:

    /* last good known ov info */
    mdp_overlay   mLkgo;

    /* Actual overlay mdp structure */
    mdp_overlay   mOVInfo;

    /* FD for the mdp fbnum */
    OvFD          mFd;

    /* cached size FIXME do we need it? */
    uint32_t mSize;
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

    /* open FD */
    bool open(uint32_t fbnum);

    /* memset0 the underlying mdp object */
    void reset();

    /* close fd, and reset */
    bool close();

    /* Set FD / memid */
    void setMemoryId(int id);

    /* set id of mdp data */
    void setId(int id);

    /* return ses id of data */
    int getId() const;

    /* get underlying fd*/
    int getFd() const;

    /* get memory_id */
    int getMemoryId() const;

    /* set offset in underlying mdp obj */
    void setOffset(uint32_t o);

    /* calls wrapper play */
    bool play();

    /* calls wrapper playWait */
    bool playWait();

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
    inline bool openDev(OvFD& fd, int fb,
            const char* const s,
            int flags) {
        return overlay::open(fd, fb, Res::devTemplate, O_RDWR);
    }
}

template <class T>
        inline void init(T& t) {
            memset(&t, 0, sizeof(T));
        }

/////   MdpCtrl  //////

inline MdpCtrl::MdpCtrl() : mSize(0) {
    reset();
}

inline MdpCtrl::~MdpCtrl() {
    close();
}

inline int MdpCtrl::getOrient() const {
    return getUserData();
}

inline int MdpCtrl::getId() const {
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

inline void MdpCtrl::setWait(overlay::utils::eWait wait) {
    mOVInfo.flags = utils::setWait(wait, mOVInfo.flags);
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
    if(static_cast<ssize_t>(mOVInfo.id) == -1) {
        ALOGE("MdpCtrl current ov has id -1, will not save");
        // FIXME dump both?
        return;
    }
    mLkgo = mOVInfo;
}

inline void MdpCtrl::restore() {
    if(static_cast<ssize_t>(mLkgo.id) == -1) {
        ALOGE("MdpCtrl Lkgo ov has id -1, will not restore");
        // FIXME dump both?
        return;
    }
    mOVInfo = mLkgo;
}

inline overlay::utils::Whf MdpCtrl::getSrcWhf() const {
    return utils::Whf(mOVInfo.src.width,
            mOVInfo.src.height,
            mOVInfo.src.format);
}

inline void MdpCtrl::setSrcWhf(const overlay::utils::Whf& whf) {
    mOVInfo.src.width  = whf.w;
    mOVInfo.src.height = whf.h;
    mOVInfo.src.format = whf.format;
}

inline overlay::utils::Dim MdpCtrl::getSrcRectDim() const {
    return utils::Dim(mOVInfo.src_rect.x,
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
    return utils::Dim(mOVInfo.dst_rect.x,
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
            mOVInfo.src.height); }

inline void MdpCtrl::swapSrcRectWH() {
    utils::swap(mOVInfo.src_rect.h,
            mOVInfo.src_rect.w); }

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

inline bool MdpData::open(uint32_t fbnum) {
    // FD open
    if(!utils::openDev(mFd, fbnum, Res::devTemplate, O_RDWR)){
        ALOGE("Ctrl failed to open fbnum=%d", fbnum);
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

inline void MdpData::setMemoryId(int id) { mOvData.data.memory_id = id; }
inline int MdpData::getMemoryId() const { return mOvData.data.memory_id; }

inline void MdpData::setId(int id) { mOvData.id = id; }

inline int MdpData::getId() const { return mOvData.id; }

inline int MdpData::getFd() const { return mFd.getFD(); }

inline void MdpData::setOffset(uint32_t o) { mOvData.data.offset = o; }

inline bool MdpData::play() {
    if(!mdp_wrapper::play(mFd.getFD(), mOvData)){
        ALOGE("MdpData failed to play");
        return false;
    }
    return true;
}

inline bool MdpData::playWait() {
    if(!mdp_wrapper::playWait(mFd.getFD(), mOvData)){
        ALOGE("MdpData failed to playWait");
        return false;
    }
    return true;
}

} // overlay

#endif // OVERLAY_MDP_H
