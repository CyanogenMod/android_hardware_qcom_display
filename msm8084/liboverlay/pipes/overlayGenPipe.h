/*
* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
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

#ifndef OVERLAY_GENERIC_PIPE_H
#define OVERLAY_GENERIC_PIPE_H

#include "overlayUtils.h"
#include "overlayRotator.h"
#include "overlayCtrlData.h"

// FIXME make int to be uint32 whenever possible

namespace overlay {

template <int PANEL>
class GenericPipe : utils::NoCopy {
public:
    /* ctor init */
    explicit GenericPipe();

    /* dtor close */
    ~GenericPipe();

    /* CTRL/DATA/ROT open */
    bool open(RotatorBase* rot);

    /* CTRL/DATA close. Not owning rotator, will not close it */
    bool close();

    /* commit changes to the overlay "set"*/
    bool commit();

    /* "Data" related interface */

    /* set ID directly to data channel */
    void setId(int id);

    /* Set FD / memid */
    void setMemoryId(int id);

    /* queue buffer to the overlay */
    bool queueBuffer(uint32_t offset);

    /* dequeue buffer to the overlay NOTSUPPORTED */
    bool dequeueBuffer(void*& buf);

    /* wait for vsync to be done */
    bool waitForVsync();

    /* set crop data FIXME setROI (Region Of Intrest) */
    bool setCrop(const utils::Dim& d);

    /* "Ctrl" related interface */

    /*
     * Start a session, opens the rotator
     * FIXME, we might want to open the rotator separately
     */
    bool start(const utils::PipeArgs& args);

    /* set mdp posision using dim */
    bool setPosition(const utils::Dim& dim);

    /* set param using Params (param,value pair) */
    bool setParameter(const utils::Params& param);

    /* set source using whf, orient and wait flag */
    bool setSource(const utils::PipeArgs& args);

    /* return cached startup args */
    const utils::PipeArgs& getArgs() const;

    /* retrieve screen info */
    utils::ScreenInfo getScreenInfo() const;

    /* retrieve cached crop data */
    utils::Dim getCrop() const;

    /* return aspect ratio from ctrl data */
    utils::Dim getAspectRatio(const utils::Whf& whf) const;

    /* return aspect ratio from ctrl data for true UI mirroring */
    utils::Dim getAspectRatio(const utils::Dim& dim) const;

    /* is closed */
    bool isClosed() const;

    /* is open */
    bool isOpen() const;

    /* return Ctrl fd. Used for S3D */
    int getCtrlFd() const;

    /* Get the overlay pipe type */
    utils::eOverlayPipeType getOvPipeType() const;

    /* dump the state of the object */
    void dump() const;
private:
    /* set Closed channel */
    bool setClosed();
    // kick off rotator.
    bool startRotator();

    /* Ctrl/Data aggregator */
    CtrlData mCtrlData;

    /* caching startup params. useful when need
     * to have the exact copy of that pipe.
     * For example when HDMI is connected, and we would
     * like to open/start the pipe with the args */
    utils::PipeArgs mArgs;

    /* rotator mdp base
     * Can point to NullRotator or to Rotator*/
    RotatorBase* mRot;

    /* my flags */
    enum { CLOSED = 1<<0 };
    uint32_t mFlags;
};

//------------------------Inlines and Templates ----------------------

template <int PANEL>
GenericPipe<PANEL>::GenericPipe() : mRot(0), mFlags(CLOSED) {}

template <int PANEL>
GenericPipe<PANEL>::~GenericPipe() {
    close();
}

template <int PANEL>
bool GenericPipe<PANEL>::open(RotatorBase* rot)
{
    OVASSERT(rot, "rot is null");
    // open ctrl and data
    uint32_t fbnum = utils::getFBForPanel(PANEL);
    ALOGE_IF(DEBUG_OVERLAY, "GenericPipe open");
    if(!mCtrlData.ctrl.open(fbnum, rot)) {
        ALOGE("GenericPipe failed to open ctrl");
        return false;
    }
    if(!mCtrlData.data.open(fbnum, rot)) {
        ALOGE("GenericPipe failed to open data");
        return false;
    }
    mRot = rot;

    // NOTE: we won't have the flags as non CLOSED since we
    // consider the pipe opened for business only when we call
    // start()

    return true;
}

template <int PANEL>
bool GenericPipe<PANEL>::close() {
    if(isClosed()) return true;
    bool ret = true;
    if(!mCtrlData.ctrl.close()) {
        ALOGE("GenericPipe failed to close ctrl");
        ret = false;
    }
    if (!mCtrlData.data.close()) {
        ALOGE("GenericPipe failed to close data");
        ret = false;
    }
    setClosed();
    return ret;
}

template <int PANEL>
inline bool GenericPipe<PANEL>::commit(){
    OVASSERT(isOpen(), "State is closed, cannot commit");
    return mCtrlData.ctrl.commit();
}

template <int PANEL>
inline void GenericPipe<PANEL>::setMemoryId(int id) {
    OVASSERT(isOpen(), "State is closed, cannot setMemoryId");
    mCtrlData.data.setMemoryId(id);
}

template <int PANEL>
inline void GenericPipe<PANEL>::setId(int id) {
    mCtrlData.data.setId(id); }

template <int PANEL>
inline int GenericPipe<PANEL>::getCtrlFd() const {
    return mCtrlData.ctrl.getFd();
}

template <int PANEL>
inline bool GenericPipe<PANEL>::setCrop(
        const overlay::utils::Dim& d) {
    OVASSERT(isOpen(), "State is closed, cannot setCrop");
    return mCtrlData.ctrl.setCrop(d);
}

template <int PANEL>
bool GenericPipe<PANEL>::start(const utils::PipeArgs& args)
{
    /* open before your start control rotator */
    uint32_t sz = args.whf.size; //utils::getSizeByMdp(args.whf);
    OVASSERT(sz, "GenericPipe sz=%d", sz);
    if(!mRot->open()) {
        ALOGE("GenericPipe start failed to open rot");
        return false;
    }

    if(!mCtrlData.ctrl.start(args)){
        ALOGE("GenericPipe failed to start");
        return false;
    }

    int ctrlId = mCtrlData.ctrl.getId();
    OVASSERT(-1 != ctrlId, "Ctrl ID should not be -1");
    // set ID requeset to assoc ctrl to data
    setId(ctrlId);
    // set ID request to assoc MDP data to ROT MDP data
    mRot->setDataReqId(mCtrlData.data.getId());

    // cache the args for future reference.
    mArgs = args;

    // we got here so we are open+start and good to go
    mFlags = 0; // clear flags from CLOSED
    // TODO make it more robust when more flags
    // are added

    return true;
}

template <int PANEL>
inline const utils::PipeArgs& GenericPipe<PANEL>::getArgs() const
{
    return mArgs;
}

template <int PANEL>
bool GenericPipe<PANEL>::startRotator() {
    // kick off rotator
    if(!mRot->start()) {
        ALOGE("GenericPipe failed to start rotator");
        return false;
    }
    return true;
}

template <int PANEL>
inline bool GenericPipe<PANEL>::queueBuffer(uint32_t offset) {
    OVASSERT(isOpen(), "State is closed, cannot queueBuffer");
    return mCtrlData.data.queueBuffer(offset);
}

template <int PANEL>
inline bool GenericPipe<PANEL>::dequeueBuffer(void*&) {
    OVASSERT(isOpen(), "State is closed, cannot dequeueBuffer");
    // can also set error to NOTSUPPORTED in the future
    return false;
}

template <int PANEL>
inline bool GenericPipe<PANEL>::waitForVsync() {
    OVASSERT(isOpen(), "State is closed, cannot waitForVsync");

    return mCtrlData.data.waitForVsync();
}

template <int PANEL>
inline bool GenericPipe<PANEL>::setPosition(const utils::Dim& dim)
{
    OVASSERT(isOpen(), "State is closed, cannot setPosition");
    return mCtrlData.ctrl.setPosition(dim);
}

template <int PANEL>
inline bool GenericPipe<PANEL>::setParameter(
        const utils::Params& param)
{
    OVASSERT(isOpen(), "State is closed, cannot setParameter");
    // Currently setParameter would start rotator
    if(!mCtrlData.ctrl.setParameter(param)) {
        ALOGE("GenericPipe failed to setparam");
        return false;
    }
    // if rot flags are ENABLED it means we would always
    // like to have rot. Even with 0 rot. (solves tearing)
    if(utils::ROT_FLAG_ENABLED == mArgs.rotFlags) {
        mRot->setEnable();
    }
    return startRotator();
}

template <int PANEL>
inline bool GenericPipe<PANEL>::setSource(
        const utils::PipeArgs& args)
{
    // cache the recent args.
    mArgs = args;
    // setSource is the 1st thing that is being called on a pipe.
    // If pipe is closed, we should start everything.
    // we assume it is being opened with the correct FDs.
    if(isClosed()) {
        if(!this->start(args)) {
            ALOGE("GenericPipe setSource failed to start");
            return false;
        }
        return true;
    }

    return mCtrlData.ctrl.setSource(args);
}

template <int PANEL>
inline utils::Dim GenericPipe<PANEL>::getAspectRatio(
        const utils::Whf& whf) const
{
    return mCtrlData.ctrl.getAspectRatio(whf);
}

template <int PANEL>
inline utils::Dim GenericPipe<PANEL>::getAspectRatio(
        const utils::Dim& dim) const
{
    return mCtrlData.ctrl.getAspectRatio(dim);
}

template <int PANEL>
inline utils::ScreenInfo GenericPipe<PANEL>::getScreenInfo() const
{
    return mCtrlData.ctrl.getScreenInfo();
}

template <int PANEL>
inline utils::Dim GenericPipe<PANEL>::getCrop() const
{
    return mCtrlData.ctrl.getCrop();
}

template <int PANEL>
inline utils::eOverlayPipeType GenericPipe<PANEL>::getOvPipeType() const {
    return utils::OV_PIPE_TYPE_GENERIC;
}

template <int PANEL>
void GenericPipe<PANEL>::dump() const
{
    ALOGE("== Dump Generic pipe start ==");
    ALOGE("flags=0x%x", mFlags);
    OVASSERT(mRot, "GenericPipe should have a valid Rot");
    mCtrlData.ctrl.dump();
    mCtrlData.data.dump();
    mRot->dump();
    ALOGE("== Dump Generic pipe end ==");
}

template <int PANEL>
inline bool GenericPipe<PANEL>::isClosed() const  {
    return utils::getBit(mFlags, CLOSED);
}

template <int PANEL>
inline bool GenericPipe<PANEL>::isOpen() const  {
    return !isClosed();
}

template <int PANEL>
inline bool GenericPipe<PANEL>::setClosed() {
    return utils::setBit(mFlags, CLOSED);
}


} //namespace overlay

#endif // OVERLAY_GENERIC_PIPE_H
