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

namespace overlay {

template <int PANEL>
class GenericPipe : utils::NoCopy {
public:
    /* ctor */
    explicit GenericPipe();
    /* dtor */
    ~GenericPipe();
    /* CTRL/DATA init. Not owning rotator, will not  init it */
    bool init(RotatorBase* rot);
    /* CTRL/DATA close. Not owning rotator, will not close it */
    bool close();

    /* Control APIs */
    /* set source using whf, orient and wait flag */
    bool setSource(const utils::PipeArgs& args);
    /* set crop a.k.a the region of interest */
    bool setCrop(const utils::Dim& d);
    /* set orientation*/
    bool setTransform(const utils::eTransform& param);
    /* set mdp posision using dim */
    bool setPosition(const utils::Dim& dim);
    /* commit changes to the overlay "set"*/
    bool commit();

    /* Data APIs */
    /* queue buffer to the overlay */
    bool queueBuffer(int fd, uint32_t offset);

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

    /* dump the state of the object */
    void dump() const;
private:
    /* set Closed pipe */
    bool setClosed();

    /* Ctrl/Data aggregator */
    CtrlData mCtrlData;

    /* rotator mdp base
     * Can point to NullRotator or to Rotator*/
    RotatorBase* mRot;

    //Whether rotator is used for 0-rot or otherwise
    bool mRotUsed;

    /* Pipe open or closed */
    enum ePipeState {
        CLOSED,
        OPEN
    };
    ePipeState pipeState;
};

//------------------------Inlines and Templates ----------------------

template <int PANEL>
GenericPipe<PANEL>::GenericPipe() : mRot(0), mRotUsed(false),
        pipeState(CLOSED) {
}

template <int PANEL>
GenericPipe<PANEL>::~GenericPipe() {
    close();
}

template <int PANEL>
bool GenericPipe<PANEL>::init(RotatorBase* rot)
{
    ALOGE_IF(DEBUG_OVERLAY, "GenericPipe init");
    OVASSERT(rot, "rot is null");

    // init ctrl and data
    uint32_t fbnum = utils::getFBForPanel(PANEL);

    if(!mCtrlData.ctrl.init(fbnum)) {
        ALOGE("GenericPipe failed to init ctrl");
        return false;
    }

    if(!mCtrlData.data.init(fbnum)) {
        ALOGE("GenericPipe failed to init data");
        return false;
    }

    //Cache the rot ref. Ownership is with OverlayImpl.
    mRot = rot;

    mRotUsed = false;

    // NOTE:init() on the rot is called by OverlayImpl
    // Pipes only have to worry about using rot, and not init or close.

    return true;
}

template <int PANEL>
bool GenericPipe<PANEL>::close() {
    if(isClosed())
        return true;

    bool ret = true;

    if(!mCtrlData.ctrl.close()) {
        ALOGE("GenericPipe failed to close ctrl");
        ret = false;
    }
    if (!mCtrlData.data.close()) {
        ALOGE("GenericPipe failed to close data");
        ret = false;
    }

    // NOTE:close() on the rot is called by OverlayImpl
    // Pipes only have to worry about using rot, and not init or close.

    setClosed();
    return ret;
}

template <int PANEL>
inline bool GenericPipe<PANEL>::setSource(
        const utils::PipeArgs& args)
{
    utils::PipeArgs newargs(args);
    //Interlace video handling.
    if(newargs.whf.format & INTERLACE_MASK) {
        setMdpFlags(newargs.mdpFlags, utils::OV_MDP_DEINTERLACE);
    }
    utils::Whf whf(newargs.whf);
    //Extract HAL format from lower bytes. Deinterlace if interlaced.
    whf.format = utils::getColorFormat(whf.format);
    //Get MDP equivalent of HAL format.
    whf.format = utils::getMdpFormat(whf.format);
    newargs.whf = whf;

    //Cache if user wants 0-rotation
    mRotUsed = newargs.rotFlags & utils::ROT_FLAG_ENABLED;
    mRot->setSource(newargs.whf);
    mRot->setFlags(newargs.mdpFlags);
    return mCtrlData.ctrl.setSource(newargs);
}

template <int PANEL>
inline bool GenericPipe<PANEL>::setCrop(
        const overlay::utils::Dim& d) {
    return mCtrlData.ctrl.setCrop(d);
}

template <int PANEL>
inline bool GenericPipe<PANEL>::setTransform(
        const utils::eTransform& orient)
{
    //Rotation could be enabled by user for zero-rot or the layer could have
    //some transform. Mark rotation enabled in either case.
    mRotUsed |= (orient != utils::OVERLAY_TRANSFORM_0);
    mRot->setTransform(orient, mRotUsed);

    return mCtrlData.ctrl.setTransform(orient, mRotUsed);
}

template <int PANEL>
inline bool GenericPipe<PANEL>::setPosition(const utils::Dim& d)
{
    return mCtrlData.ctrl.setPosition(d);
}

template <int PANEL>
inline bool GenericPipe<PANEL>::commit() {
    bool ret = false;
    //If wanting to use rotator, start it.
    if(mRotUsed) {
        if(!mRot->commit()) {
            ALOGE("GenPipe Rotator commit failed");
            return false;
        }
    }
    ret = mCtrlData.ctrl.commit();
    pipeState = ret ? OPEN : CLOSED;
    return ret;
}

template <int PANEL>
inline bool GenericPipe<PANEL>::queueBuffer(int fd, uint32_t offset) {
    //TODO Move pipe-id transfer to CtrlData class. Make ctrl and data private.
    OVASSERT(isOpen(), "State is closed, cannot queueBuffer");
    int pipeId = mCtrlData.ctrl.getPipeId();
    OVASSERT(-1 != pipeId, "Ctrl ID should not be -1");
    // set pipe id from ctrl to data
    mCtrlData.data.setPipeId(pipeId);

    int finalFd = fd;
    uint32_t finalOffset = offset;
    //If rotator is to be used, queue to it, so it can ROTATE.
    if(mRotUsed) {
        if(!mRot->queueBuffer(fd, offset)) {
            ALOGE("GenPipe Rotator play failed");
            return false;
        }
        //Configure MDP's source buffer as the current output buffer of rotator
        if(mRot->getDstMemId() != -1) {
            finalFd = mRot->getDstMemId();
            finalOffset = mRot->getDstOffset();
        } else {
            //Could be -1 for NullRotator, if queue above succeeds.
            //Need an actual rotator. Modify overlay State Traits.
            //Not fatal, keep queuing to MDP without rotation.
            ALOGE("Null rotator in use, where an actual is required");
        }
    }
    return mCtrlData.data.queueBuffer(finalFd, finalOffset);
}

template <int PANEL>
inline int GenericPipe<PANEL>::getCtrlFd() const {
    return mCtrlData.ctrl.getFd();
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
void GenericPipe<PANEL>::dump() const
{
    ALOGE("== Dump Generic pipe start ==");
    ALOGE("pipe state = %d", (int)pipeState);
    OVASSERT(mRot, "GenericPipe should have a valid Rot");
    mCtrlData.ctrl.dump();
    mCtrlData.data.dump();
    mRot->dump();
    ALOGE("== Dump Generic pipe end ==");
}

template <int PANEL>
inline bool GenericPipe<PANEL>::isClosed() const  {
    return (pipeState == CLOSED);
}

template <int PANEL>
inline bool GenericPipe<PANEL>::isOpen() const  {
    return (pipeState == OPEN);
}

template <int PANEL>
inline bool GenericPipe<PANEL>::setClosed() {
    pipeState = CLOSED;
    return true;
}

} //namespace overlay

#endif // OVERLAY_GENERIC_PIPE_H
