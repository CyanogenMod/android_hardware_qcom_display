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

#ifndef OVERLAY_M3D_EXT_PIPE_H
#define OVERLAY_M3D_EXT_PIPE_H

#include "overlayGenPipe.h"
#include "overlayUtils.h"

namespace overlay {

/////////////  M3DExt Pipe ////////////////////////////
/**
* A specific impl of GenericPipe for 3D.
* Whenever needed to have a pass through - we do it.
* If there is a special need for special/diff behavior
* do it here
* PANEL is always EXTERNAL for this pipe.
* CHAN = 0,1 it's either Channel 1 or channel 2 needed for
* 3D crop and position */
template <int CHAN>
class M3DExtPipe : utils::NoCopy {
public:
    /* Please look at overlayGenPipe.h for info */
    explicit M3DExtPipe();
    ~M3DExtPipe();
    bool open(RotatorBase* rot);
    bool close();
    bool commit();
    void setId(int id);
    void setMemoryId(int id);
    bool queueBuffer(uint32_t offset);
    bool dequeueBuffer(void*& buf);
    bool waitForVsync();
    bool setCrop(const utils::Dim& d);
    bool start(const utils::PipeArgs& args);
    bool setPosition(const utils::Dim& dim);
    bool setParameter(const utils::Params& param);
    bool setSource(const utils::PipeArgs& args);
    const utils::PipeArgs& getArgs() const;
    utils::eOverlayPipeType getOvPipeType() const;
    void dump() const;
private:
    overlay::GenericPipe<utils::EXTERNAL> mM3d;
    // Cache the M3D format
    uint32_t mM3Dfmt;
};

/////////////  M3DPrimary Pipe ////////////////////////////
/**
* A specific impl of GenericPipe for 3D.
* Whenever needed to have a pass through - we do it.
* If there is a special need for special/diff behavior
* do it here
* PANEL is always PRIMARY for this pipe.
* CHAN = 0,1 it's either Channel 1 or channel 2 needed for
* 3D crop and position */
template <int CHAN>
class M3DPrimaryPipe : utils::NoCopy {
public:
    /* Please look at overlayGenPipe.h for info */
    explicit M3DPrimaryPipe();
    ~M3DPrimaryPipe();
    bool open(RotatorBase* rot);
    bool close();
    bool commit();
    void setId(int id);
    void setMemoryId(int id);
    bool queueBuffer(uint32_t offset);
    bool dequeueBuffer(void*& buf);
    bool waitForVsync();
    bool setCrop(const utils::Dim& d);
    bool start(const utils::PipeArgs& args);
    bool setPosition(const utils::Dim& dim);
    bool setParameter(const utils::Params& param);
    bool setSource(const utils::PipeArgs& args);
    const utils::PipeArgs& getArgs() const;
    utils::eOverlayPipeType getOvPipeType() const;
    void dump() const;
private:
    overlay::GenericPipe<utils::PRIMARY> mM3d;
    // Cache the M3D format
    uint32_t mM3Dfmt;
};

/////////////  S3DExt Pipe ////////////////////////////////
/**
* A specific impl of GenericPipe for 3D.
* Whenever needed to have a pass through - we do it.
* If there is a special need for special/diff behavior
* do it here.
* PANEL is always EXTERNAL for this pipe.
* CHAN = 0,1 it's either Channel 1 or channel 2 needed for
* 3D crop and position */
template <int CHAN>
class S3DExtPipe : utils::NoCopy {
public:
    /* Please look at overlayGenPipe.h for info */
    explicit S3DExtPipe();
    ~S3DExtPipe();
    bool open(RotatorBase* rot);
    bool close();
    bool commit();
    void setId(int id);
    void setMemoryId(int id);
    bool queueBuffer(uint32_t offset);
    bool dequeueBuffer(void*& buf);
    bool waitForVsync();
    bool setCrop(const utils::Dim& d);
    bool start(const utils::PipeArgs& args);
    bool setPosition(const utils::Dim& dim);
    bool setParameter(const utils::Params& param);
    bool setSource(const utils::PipeArgs& args);
    const utils::PipeArgs& getArgs() const;
    utils::eOverlayPipeType getOvPipeType() const;
    void dump() const;
private:
    overlay::GenericPipe<utils::EXTERNAL> mS3d;
    // Cache the 3D format
    uint32_t mS3Dfmt;
};

/////////////  S3DPrimary Pipe ////////////////////////////
/**
* A specific impl of GenericPipe for 3D.
* Whenever needed to have a pass through - we do it.
* If there is a special need for special/diff behavior
* do it here
* PANEL is always PRIMARY for this pipe.
* CHAN = 0,1 it's either Channel 1 or channel 2 needed for
* 3D crop and position */
template <int CHAN>
class S3DPrimaryPipe : utils::NoCopy {
public:
    /* Please look at overlayGenPipe.h for info */
    explicit S3DPrimaryPipe();
    ~S3DPrimaryPipe();
    bool open(RotatorBase* rot);
    bool close();
    bool commit();
    void setId(int id);
    void setMemoryId(int id);
    bool queueBuffer(uint32_t offset);
    bool dequeueBuffer(void*& buf);
    bool waitForVsync();
    bool setCrop(const utils::Dim& d);
    bool start(const utils::PipeArgs& args);
    bool setPosition(const utils::Dim& dim);
    bool setParameter(const utils::Params& param);
    bool setSource(const utils::PipeArgs& args);
    const utils::PipeArgs& getArgs() const;
    utils::eOverlayPipeType getOvPipeType() const;
    void dump() const;
private:
    /* needed for 3D related IOCTL */
    MdpCtrl3D mCtrl3D;
    overlay::GenericPipe<utils::PRIMARY> mS3d;
    // Cache the 3D format
    uint32_t mS3Dfmt;
};




//------------------------Inlines and Templates--------------------------


/////////////  M3DExt Pipe ////////////////////////////
template <int CHAN>
inline M3DExtPipe<CHAN>::M3DExtPipe() : mM3Dfmt(0) {}
template <int CHAN>
inline M3DExtPipe<CHAN>::~M3DExtPipe() { close(); }
template <int CHAN>
inline bool M3DExtPipe<CHAN>::open(RotatorBase* rot) {
    ALOGE_IF(DEBUG_OVERLAY, "M3DExtPipe open");
    if(!mM3d.open(rot)) {
        ALOGE("3Dpipe failed to open");
        return false;
    }
    return true;
}
template <int CHAN>
inline bool M3DExtPipe<CHAN>::close() {
    return mM3d.close();
}
template <int CHAN>
inline bool M3DExtPipe<CHAN>::commit() { return mM3d.commit(); }
template <int CHAN>
inline void M3DExtPipe<CHAN>::setId(int id) { mM3d.setId(id); }
template <int CHAN>
inline void M3DExtPipe<CHAN>::setMemoryId(int id) { mM3d.setMemoryId(id); }
template <int CHAN>
inline bool M3DExtPipe<CHAN>::queueBuffer(uint32_t offset) {
    return mM3d.queueBuffer(offset); }
template <int CHAN>
inline bool M3DExtPipe<CHAN>::dequeueBuffer(void*& buf) {
    return mM3d.dequeueBuffer(buf); }
template <int CHAN>
inline bool M3DExtPipe<CHAN>::waitForVsync() {
    return mM3d.waitForVsync(); }
template <int CHAN>
inline bool M3DExtPipe<CHAN>::setCrop(const utils::Dim& d) {
    utils::Dim _dim;
    if(!utils::getCropS3D<CHAN>(d, _dim, mM3Dfmt)){
        ALOGE("M3DExtPipe setCrop failed to getCropS3D");
        _dim = d;
    }
    return mM3d.setCrop(_dim);
}
template <int CHAN>
inline bool M3DExtPipe<CHAN>::start(const utils::PipeArgs& args) {
    if(!mM3d.start(args)) {
        ALOGE("M3DExtPipe start failed");
        return false;
    }
    return true;
}
template <int CHAN>
inline bool M3DExtPipe<CHAN>::setPosition(const utils::Dim& d) {
    utils::Dim _dim;
    // original setPositionHandleState has getPositionS3D(...,true)
    // which means format is HAL_3D_OUT_SBS_MASK
    // HAL_3D_OUT_SBS_MASK is 0x1000 >> 12 == 0x1 as the orig
    // code suggets
    utils::Whf _whf(mM3d.getScreenInfo().mFBWidth,
            mM3d.getScreenInfo().mFBHeight,
            mM3Dfmt);
    if(!utils::getPositionS3D<CHAN>(_whf, _dim)) {
        ALOGE("S3DPrimaryPipe setPosition err in getPositionS3D");
        _dim = d;
    }
    return mM3d.setPosition(_dim);
}
template <int CHAN>
inline bool M3DExtPipe<CHAN>::setParameter(const utils::Params& param) {
    return mM3d.setParameter(param);
}
template <int CHAN>
inline bool M3DExtPipe<CHAN>::setSource(const utils::PipeArgs& args)
{
    // extract 3D fmt
    mM3Dfmt = utils::format3DInput(utils::getS3DFormat(args.whf.format)) |
            utils::HAL_3D_OUT_MONOS_MASK;
    if(mM3d.isClosed()){
        if(!this->start(args)) {
            ALOGE("M3DExtPipe setSource failed to start");
            return false;
        }
    }
    return mM3d.setSource(args);
}
template <int CHAN>
inline const utils::PipeArgs& M3DExtPipe<CHAN>::getArgs() const {
    return mM3d.getArgs();
}
template <int CHAN>
inline utils::eOverlayPipeType M3DExtPipe<CHAN>::getOvPipeType() const {
    return utils::OV_PIPE_TYPE_M3D_EXTERNAL;
}
template <int CHAN>
inline void M3DExtPipe<CHAN>::dump() const {
    ALOGE("M3DExtPipe Pipe fmt=%d", mM3Dfmt);
    mM3d.dump();
}


/////////////  M3DPrimary Pipe ////////////////////////////
template <int CHAN>
inline M3DPrimaryPipe<CHAN>::M3DPrimaryPipe() : mM3Dfmt(0) {}
template <int CHAN>
inline M3DPrimaryPipe<CHAN>::~M3DPrimaryPipe() { close(); }
template <int CHAN>
inline bool M3DPrimaryPipe<CHAN>::open(RotatorBase* rot) {
    ALOGE_IF(DEBUG_OVERLAY, "M3DPrimaryPipe open");
    if(!mM3d.open(rot)) {
        ALOGE("3Dpipe failed to open");
        return false;
    }
    return true;
}
template <int CHAN>
inline bool M3DPrimaryPipe<CHAN>::close() {
    return mM3d.close();
}
template <int CHAN>
inline bool M3DPrimaryPipe<CHAN>::commit() { return mM3d.commit(); }
template <int CHAN>
inline void M3DPrimaryPipe<CHAN>::setId(int id) { mM3d.setId(id); }
template <int CHAN>
inline void M3DPrimaryPipe<CHAN>::setMemoryId(int id) { mM3d.setMemoryId(id); }
template <int CHAN>
inline bool M3DPrimaryPipe<CHAN>::queueBuffer(uint32_t offset) {
    return mM3d.queueBuffer(offset); }
template <int CHAN>
inline bool M3DPrimaryPipe<CHAN>::dequeueBuffer(void*& buf) {
    return mM3d.dequeueBuffer(buf); }
template <int CHAN>
inline bool M3DPrimaryPipe<CHAN>::waitForVsync() {
    return mM3d.waitForVsync(); }
template <int CHAN>
inline bool M3DPrimaryPipe<CHAN>::setCrop(const utils::Dim& d) {
    utils::Dim _dim;
    if(!utils::getCropS3D<CHAN>(d, _dim, mM3Dfmt)){
        ALOGE("M3DPrimaryPipe setCrop failed to getCropS3D");
        _dim = d;
    }
    return mM3d.setCrop(_dim);
}
template <int CHAN>
inline bool M3DPrimaryPipe<CHAN>::start(const utils::PipeArgs& args) {
    if(!mM3d.start(args)) {
        ALOGE("M3DPrimaryPipe start failed");
        return false;
    }
    return true;
}
template <int CHAN>
inline bool M3DPrimaryPipe<CHAN>::setPosition(const utils::Dim& d) {
    return mM3d.setPosition(d);
}
template <int CHAN>
inline bool M3DPrimaryPipe<CHAN>::setParameter(const utils::Params& param) {
    return mM3d.setParameter(param);
}
template <int CHAN>
inline bool M3DPrimaryPipe<CHAN>::setSource(const utils::PipeArgs& args)
{
    // extract 3D fmt
    mM3Dfmt = utils::format3DInput(utils::getS3DFormat(args.whf.format)) |
            utils::HAL_3D_OUT_MONOS_MASK;
    if (mM3d.isClosed()) {
        if (!this->start(args)) {
            ALOGE("M3DPrimaryPipe setSource failed to start");
            return false;
        }
    }
    return mM3d.setSource(args);
}
template <int CHAN>
inline const utils::PipeArgs& M3DPrimaryPipe<CHAN>::getArgs() const {
    return mM3d.getArgs();
}
template <int CHAN>
inline utils::eOverlayPipeType M3DPrimaryPipe<CHAN>::getOvPipeType() const {
    return utils::OV_PIPE_TYPE_M3D_PRIMARY;
}
template <int CHAN>
inline void M3DPrimaryPipe<CHAN>::dump() const {
    ALOGE("M3DPrimaryPipe Pipe fmt=%d", mM3Dfmt);
    mM3d.dump();
}

/////////////  S3DExt Pipe ////////////////////////////////
template <int CHAN>
inline S3DExtPipe<CHAN>::S3DExtPipe() : mS3Dfmt(0) {}
template <int CHAN>
inline S3DExtPipe<CHAN>::~S3DExtPipe() { close(); }
template <int CHAN>
inline bool S3DExtPipe<CHAN>::open(RotatorBase* rot) {
    ALOGE_IF(DEBUG_OVERLAY, "S3DExtPipe open");
    if(!mS3d.open(rot)) {
        ALOGE("3Dpipe failed to open");
        return false;
    }
    return true;
}
template <int CHAN>
inline bool S3DExtPipe<CHAN>::close() {
    if(!utils::send3DInfoPacket(0)) {
        ALOGE("S3DExtPipe close failed send3D info packet");
    }
    return mS3d.close();
}
template <int CHAN>
inline bool S3DExtPipe<CHAN>::commit() { return mS3d.commit(); }
template <int CHAN>
inline void S3DExtPipe<CHAN>::setId(int id) { mS3d.setId(id); }
template <int CHAN>
inline void S3DExtPipe<CHAN>::setMemoryId(int id) { mS3d.setMemoryId(id); }
template <int CHAN>
inline bool S3DExtPipe<CHAN>::queueBuffer(uint32_t offset) {
    //this->dump();
    return mS3d.queueBuffer(offset); }
template <int CHAN>
inline bool S3DExtPipe<CHAN>::dequeueBuffer(void*& buf) {
    return mS3d.dequeueBuffer(buf); }
template <int CHAN>
inline bool S3DExtPipe<CHAN>::waitForVsync() {
    return mS3d.waitForVsync(); }
template <int CHAN>
inline bool S3DExtPipe<CHAN>::setCrop(const utils::Dim& d) {
    utils::Dim _dim;
    if(!utils::getCropS3D<CHAN>(d, _dim, mS3Dfmt)){
        ALOGE("S3DExtPipe setCrop failed to getCropS3D");
        _dim = d;
    }
    return mS3d.setCrop(_dim);
}
template <int CHAN>
inline bool S3DExtPipe<CHAN>::start(const utils::PipeArgs& args) {
    OVASSERT(mS3Dfmt, "S3DExtPipe mS3Dfmt should not be 0 here");
    if(!mS3d.start(args)) {
        ALOGE("S3DExtPipe start failed");
        return false;
    }
    uint32_t fmt = mS3Dfmt & utils::OUTPUT_3D_MASK;
    if(!utils::send3DInfoPacket(fmt)){
        ALOGE("Error S3DExtPipe start error send3DInfoPacket %d", fmt);
        return false;
    }
    return true;
}
template <int CHAN>
inline bool S3DExtPipe<CHAN>::setPosition(const utils::Dim& d)
{
    utils::Dim _dim;
    utils::Whf _whf(mS3d.getScreenInfo().mFBWidth,
            mS3d.getScreenInfo().mFBHeight,
            mS3Dfmt);
    if(!utils::getPositionS3D<CHAN>(_whf, _dim)) {
        ALOGE("S3DExtPipe setPosition err in getPositionS3D");
        _dim = d;
    }
    return mS3d.setPosition(_dim);
}
template <int CHAN>
inline bool S3DExtPipe<CHAN>::setParameter(const utils::Params& param) {
    return mS3d.setParameter(param);
}
template <int CHAN>
inline bool S3DExtPipe<CHAN>::setSource(const utils::PipeArgs& args) {
    mS3Dfmt = utils::getS3DFormat(args.whf.format);
    if(mS3d.isClosed()){
        if(!this->start(args)) {
            ALOGE("S3DExtPipe setSource failed to start");
            return false;
        }
    }
    return mS3d.setSource(args);
}
template <int CHAN>
inline const utils::PipeArgs& S3DExtPipe<CHAN>::getArgs() const {
    return mS3d.getArgs();
}
template <int CHAN>
inline utils::eOverlayPipeType S3DExtPipe<CHAN>::getOvPipeType() const {
    return utils::OV_PIPE_TYPE_S3D_EXTERNAL;
}
template <int CHAN>
inline void S3DExtPipe<CHAN>::dump() const {
    ALOGE("S3DExtPipe Pipe fmt=%d", mS3Dfmt);
    mS3d.dump();
}

/////////////  S3DPrimary Pipe ////////////////////////////
template <int CHAN>
inline S3DPrimaryPipe<CHAN>::S3DPrimaryPipe() : mS3Dfmt(0) {}
template <int CHAN>
inline S3DPrimaryPipe<CHAN>::~S3DPrimaryPipe() { close(); }
template <int CHAN>
inline bool S3DPrimaryPipe<CHAN>::open(RotatorBase* rot) {
    ALOGE_IF(DEBUG_OVERLAY, "S3DPrimaryPipe open");
    if(!mS3d.open(rot)) {
        ALOGE("3Dpipe failed to open");
        return false;
    }
    // set the ctrl fd
    mCtrl3D.setFd(mS3d.getCtrlFd());
    return true;
}
template <int CHAN>
inline bool S3DPrimaryPipe<CHAN>::close() {
    if(!utils::enableBarrier(0)) {
        ALOGE("S3DExtPipe close failed enable barrier");
    }
    mCtrl3D.close();
    return mS3d.close();
}
template <int CHAN>
inline bool S3DPrimaryPipe<CHAN>::commit() { return mS3d.commit(); }
template <int CHAN>
inline void S3DPrimaryPipe<CHAN>::setId(int id) { mS3d.setId(id); }
template <int CHAN>
inline void S3DPrimaryPipe<CHAN>::setMemoryId(int id) { mS3d.setMemoryId(id); }
template <int CHAN>
inline bool S3DPrimaryPipe<CHAN>::queueBuffer(uint32_t offset) {
    return mS3d.queueBuffer(offset); }
template <int CHAN>
inline bool S3DPrimaryPipe<CHAN>::dequeueBuffer(void*& buf) {
    return mS3d.dequeueBuffer(buf); }
template <int CHAN>
inline bool S3DPrimaryPipe<CHAN>::waitForVsync() {
    return mS3d.waitForVsync(); }
template <int CHAN>
inline bool S3DPrimaryPipe<CHAN>::setCrop(const utils::Dim& d) {
    utils::Dim _dim;
    if(!utils::getCropS3D<CHAN>(d, _dim, mS3Dfmt)){
        ALOGE("S3DPrimaryPipe setCrop failed to getCropS3D");
        _dim = d;
    }
    return mS3d.setCrop(_dim);
}
template <int CHAN>
inline bool S3DPrimaryPipe<CHAN>::start(const utils::PipeArgs& args) {
    if(!mS3d.start(args)) {
        ALOGE("S3DPrimaryPipe start failed");
        return false;
    }
    return true;
}
template <int CHAN>
inline bool S3DPrimaryPipe<CHAN>::setPosition(const utils::Dim& d)
{
    utils::Whf fbwhf(mS3d.getScreenInfo().mFBWidth,
            mS3d.getScreenInfo().mFBHeight,
            0 /* fmt dont care*/);
    mCtrl3D.setWh(fbwhf);
    if(!mCtrl3D.useVirtualFB()) {
        ALOGE("Failed to use VFB on %d (non fatal)", utils::FB0);
        return false;
    }
    utils::Dim _dim;
    // original setPositionHandleState has getPositionS3D(...,true)
    // which means format is HAL_3D_OUT_SBS_MASK
    // HAL_3D_OUT_SBS_MASK is 0x1000 >> 12 == 0x1 as the orig
    // code suggets
    utils::Whf _whf(d.w, d.h, utils::HAL_3D_OUT_SBS_MASK);
    if(!utils::getPositionS3D<CHAN>(_whf, _dim)) {
        ALOGE("S3DPrimaryPipe setPosition err in getPositionS3D");
        _dim = d;
    }
    return mS3d.setPosition(_dim);
}

/* for S3DPrimaryPipe, we need to have barriers once
* So the easiest way to achieve it, is to make sure FB0 is having it before
* setParam is running */
template <>
inline bool S3DPrimaryPipe<utils::OV_PIPE0>::setParameter(
        const utils::Params& param) {
    if(utils::OVERLAY_TRANSFORM == param.param){
        uint32_t barrier=0;
        switch(param.value) {
            case HAL_TRANSFORM_ROT_90:
            case HAL_TRANSFORM_ROT_270:
                barrier = utils::BARRIER_LAND;
                break;
            default:
                barrier = utils::BARRIER_PORT;
                break;
        }
        if(!utils::enableBarrier(barrier)) {
            ALOGE("S3DPrimaryPipe setParameter failed to enable barrier");
        }
    }
    return mS3d.setParameter(param);
}

template <int CHAN>
inline bool S3DPrimaryPipe<CHAN>::setParameter(const utils::Params& param) {
    return mS3d.setParameter(param);
}
template <int CHAN>
inline bool S3DPrimaryPipe<CHAN>::setSource(const utils::PipeArgs& args)
{
    mS3Dfmt = utils::getS3DFormat(args.whf.format);
    if(mS3d.isClosed()){
        if(!this->start(args)) {
            ALOGE("S3DPrimaryPipe setSource failed to start");
            return false;
        }
    }
    return mS3d.setSource(args);
}
template <int CHAN>
inline const utils::PipeArgs& S3DPrimaryPipe<CHAN>::getArgs() const {
    return mS3d.getArgs();
}
template <int CHAN>
inline utils::eOverlayPipeType S3DPrimaryPipe<CHAN>::getOvPipeType() const {
    return utils::OV_PIPE_TYPE_S3D_PRIMARY;
}
template <int CHAN>
inline void S3DPrimaryPipe<CHAN>::dump() const {
    ALOGE("S3DPrimaryPipe Pipe fmt=%d", mS3Dfmt);
    mS3d.dump();
}

} // overlay

#endif // OVERLAY_M3D_EXT_PIPE_H
