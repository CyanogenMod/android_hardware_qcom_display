/*
* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of Code Aurora Forum, Inc. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef OVERLAY_CTRLDATA_H
#define OVERLAY_CTRLDATA_H

#include "overlayUtils.h"
#include "overlayMdp.h"
#include "gralloc_priv.h" // INTERLACE_MASK

namespace ovutils = overlay::utils;

namespace overlay {

// FIXME make int to be uint32 whenever possible

class RotatorBase;

/*
* FIXME do we want rot to be template parameter?
* It's already using inheritance...
*
* Sequence to use:
* open
* start
* setXXX
* close
*
* Can call setRot anytime to replace rotator on-the-fly
* */
class Ctrl : utils::NoCopy {
public:

    /* ctor */
    explicit Ctrl();

    /* dtor close */
    ~Ctrl();

    /* should open devices? or start()? */
    bool open(uint32_t fbnum, RotatorBase* rot);

    /* close underlying mdp */
    bool close();

    /* Invoke methods for opening underlying devices
     * flags - PIPE SHARED
     * wait - WAIT, NO_WAIT */
    bool start(const utils::PipeArgs& args);

    /* Dynamically set rotator*/
    void setRot(RotatorBase* rot);

    /* set mdp posision using dim */
    bool setPosition(const utils::Dim& dim);

    /* set param using Params (param,value pair)  */
    bool setParameter(const utils::Params& p);

    /* set source using whf, orient and wait flag */
    bool setSource(const utils::PipeArgs& args);

    /* set crop info and pass it down to mdp */
    bool setCrop(const utils::Dim& d);

    /* mdp set overlay/commit changes */
    bool commit();

    /* ctrl id */
    int  getId() const;
    /* ctrl fd */
    int  getFd() const;
    bool getRotSessId(int& id) const;
    utils::Dim getAspectRatio(const utils::Whf& whf) const;
    utils::Dim getAspectRatio(const utils::Dim& dim) const;

    /* access for screen info */
    utils::ScreenInfo getScreenInfo() const;

    /* retrieve cached crop data */
    utils::Dim getCrop() const;

    /* dump the state of the object */
    void dump() const;

private:
    /* Retrieve screen info from underlying mdp */
    bool getScreenInfo(utils::ScreenInfo& info);

    /* calls underlying mdp set info */
    bool setInfo(const utils::PipeArgs& args);

    /* given whf, update src */
    void updateSource(RotatorBase* r,
            const utils::PipeArgs& args,
            utils::ScreenInfo& info);

    // mdp ctrl struct(info e.g.)
    MdpCtrl mMdp;

    // Rotator
    RotatorBase* mRot;

    /* Cache cropped value */
    utils::Dim mCrop;

    /* Screen info */
    utils::ScreenInfo mInfo;

    /* orientation cache FIXME */
    utils::eTransform mOrient;

    /* Cache last known whfz.
     * That would help us compare to a previous
     * source that was submitted */
    utils::Whf mOvBufInfo;
};


/*
* MDP = DataMdp, ROT = CtrlMdp usually since Rotator<>
* is instansiated with Ctrl data structure.
* */
class Data : utils::NoCopy {
public:
    /* init, reset */
    explicit Data();

    /* calls close */
    ~Data();

    /* should open devices? or start()? */
    bool open(uint32_t fbnum, RotatorBase* rot);

    /* calls underlying mdp close */
    bool close();

    /* set the rotator */
    void setRot(RotatorBase* rot);

    /* set memory id in the mdp struct */
    void setMemoryId(int id);

    /* set overlay id in the mdp struct */
    void setId(int id);

    /* get overlay id in the mdp struct */
    int getId() const;

    /* queue buffer to the overlay */
    bool queueBuffer(uint32_t offset);

    /* wait for vsync to be done */
    bool waitForVsync();

    /* sump the state of the obj */
    void dump() const;
private:
    /* play wrapper */
    bool play();

    /* playWait wrapper */
    bool playWait();

    // mdp data struct
    MdpData mMdp;

    // Rotator
    RotatorBase* mRot;
};

/* This class just creates a Ctrl Data pair to be used by a pipe.
 * Although this was legacy design, this separation still makes sense, since we
 * need to use the Ctrl channel in hwc_prepare (i.e config stage) and Data
 * channel in hwc_set (i.e draw stage)
 */
struct CtrlData {
    Ctrl ctrl;
    Data data;
};

//-------------Inlines-------------------------------

inline Ctrl::Ctrl() : mRot(0), mOrient(utils::OVERLAY_TRANSFORM_0) {
    mMdp.reset();
}

inline Ctrl::~Ctrl() {
    close();
}

inline bool Ctrl::close() {
    // do not close the rotator
    if(!mMdp.close())
        return false;
    return true;
}

inline bool Ctrl::commit() {
    if(!mMdp.set()) {
        ALOGE("Ctrl commit failed set overlay");
        return false;
    }
    return true;
}

inline bool Ctrl::getScreenInfo(utils::ScreenInfo& info) {
    if(!mMdp.getScreenInfo(info)){
        ALOGE("Ctrl failed to get screen info");
        return false;
    }
    return true;
}

inline bool Ctrl::setInfo(const utils::PipeArgs& args)
{
    // FIXME set flags, zorder and wait separtly
    if(!mMdp.setInfo(mRot, args, mInfo)){
        ALOGE("Ctrl failed to setInfo wait=%d mdpflags=%d "
                "zorder=%d", args.wait, args.mdpFlags, args.zorder);
        return false;
    }
    return true;
}

inline int Ctrl::getId() const {
    // FIXME check channel up?
    return mMdp.getId();
}

inline int Ctrl::getFd() const {
    // FIXME check channel up?
    return mMdp.getFd();
}

inline bool Ctrl::getRotSessId(int& id) const {
    // FIXME check channel up?
    // should be -1 in case of no rot session active
    id = mRot->getSessId();
    return true;
}

inline utils::ScreenInfo Ctrl::getScreenInfo() const {
    return mInfo;
}

inline utils::Dim Ctrl::getCrop() const {
    return mCrop;
}



inline Data::Data() : mRot(0) {
    mMdp.reset();
}

inline Data::~Data() { close(); }

inline void Data::setRot(RotatorBase* rot) { mRot = rot; }

inline void Data::setMemoryId(int id) { mMdp.setMemoryId(id); }

// really a reqid
inline void Data::setId(int id) { mMdp.setId(id); }

inline int Data::getId() const { return mMdp.getId(); }

inline bool Data::open(uint32_t fbnum,
        RotatorBase* rot) {
    if(!mMdp.open(fbnum)) {
        ALOGE("Data cannot open mdp");
        return false;
    }

    OVASSERT(rot, "rot is null");
    mRot = rot;

    // rotator should be already opened here
    return true;
}

inline bool Data::close() {
    if(!mMdp.close()) {
        ALOGE("Data close failed");
        return false;
    }
    return true;
}

inline bool Data::queueBuffer(uint32_t offset) {
    // FIXME asserts on state validity

    mMdp.setOffset(offset);
    mRot->setRotDataSrcMemId(mMdp.getMemoryId());
    // will play if succeeded
    if(!mRot->prepareQueueBuf(offset)) {
        ALOGE("Data failed to prepareQueueBuf");
        return false;
    }
    // Play can go either from mdp or rot
    if(!this->play()){
        ALOGE("Data error in MDP/ROT play");
        return false;
    }

    return true;
}

inline bool Data::waitForVsync() {

    // Call mdp playWait
    if(!this->playWait()){
        ALOGE("Error in MDP playWait");
        return false;
    }

    return true;
}

inline bool Data::play() {
    int fd = mMdp.getFd();
    return mRot->enabled() ? mRot->play(fd) : mMdp.play();
}

inline bool Data::playWait() {
    return mMdp.playWait();
}

inline void Data::dump() const {
    ALOGE("== Dump Data MDP start ==");
    mMdp.dump();
    mRot->dump();
    ALOGE("== Dump Data MDP end ==");
}


} // overlay

#endif
