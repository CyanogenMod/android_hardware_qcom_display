/*
* Copyright (c) 2012, The Linux Foundation. All rights reserved.
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
*    * Neither the name of The Linux Foundation. nor the names of its
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

#ifndef OVERLAY_FLOATING_PIPE_H
#define OVERLAY_FLOATING_PIPE_H

#include "overlayGenPipe.h"
#include "overlayUtils.h"

namespace overlay {

class RotatorBase;

/* A specific impl of GenericPipe
* Whenever needed to have a pass through - we do it.
* If there is a special need for a different behavior - do it here */
class FloatingPipe : utils::NoCopy {
public:
    /* Please look at overlayGenPipe.h for info */
    explicit FloatingPipe();
    ~FloatingPipe();
    bool init(RotatorBase* rot);
    bool close();
    bool commit();
    bool queueBuffer(int fd, uint32_t offset);
    bool setCrop(const utils::Dim& dim);
    bool setPosition(const utils::Dim& dim);
    bool setTransform(const utils::eTransform& param);
    bool setSource(const utils::PipeArgs& args);
    void dump() const;
private:
    bool setupBorderfillPipe();
    bool mSessionOpen;
    int mBorderFillId;
    int mBorderFill_rect_w;
    int mBorderFill_rect_h;
    int mBorderFill_src_w;
    int mBorderFill_src_h;
    overlay::GenericPipe<ovutils::PRIMARY> mFloating;
};

//------------------Inlines -----------------------------

inline FloatingPipe::FloatingPipe(): mSessionOpen(false), mBorderFillId(-1) {}
inline FloatingPipe::~FloatingPipe() { close(); }
inline bool FloatingPipe::init(RotatorBase* rot) {
    ALOGD_IF(DEBUG_OVERLAY, "FloatingPipe init");
    return mFloating.init(rot);
}

inline bool FloatingPipe::setupBorderfillPipe() {
    if(mBorderFillId != -1)
        return true;

    mdp_overlay ovInfo;
    msmfb_overlay_data ovData;
    overlay::utils::memset0(ovInfo);
    overlay::utils::memset0(ovData);

    ovInfo.src.format = MDP_RGB_BORDERFILL;
    ovInfo.src.width = mBorderFill_src_w;
    ovInfo.src.height = mBorderFill_src_h;
    ovInfo.src_rect.w = mBorderFill_rect_w;
    ovInfo.src_rect.h = mBorderFill_rect_h;
    ovInfo.dst_rect.w = mBorderFill_rect_w;
    ovInfo.dst_rect.h = mBorderFill_rect_h;
    ovInfo.id = MSMFB_NEW_REQUEST;

    // FD ini
    OvFD fd;
    if(!utils::openDev(fd, utils::PRIMARY,
                Res::fbPath, O_RDWR)){
        ALOGE("%s: Failed to init fbnum=%d", __func__, 0);
        return false;
    }
    if(!mdp_wrapper::setOverlay(fd.getFD(), ovInfo)) {
        ALOGE("%s: Failed to setOverlay", __func__);
        fd.close();
        return false;
    }

    ovData.id = ovInfo.id;
    if(!mdp_wrapper::play(fd.getFD(), ovData)){
        ALOGE("%s: Failed to play", __func__);
        fd.close();
        return false;
    }

    fd.close();
    mBorderFillId = ovInfo.id;
    return true;
}

inline bool FloatingPipe::close() {
    if(!mSessionOpen)
        return true;

    if(!mFloating.close()) {
        ALOGE("%s: Failed to close floating pipe", __func__);
        return false;
    }

    if(mBorderFillId != -1) {
        OvFD fd;
        if(!utils::openDev(fd, utils::PRIMARY, Res::fbPath, O_RDWR)) {
            ALOGE("%s: Failed to init fbnum=%d", __func__, 0);
            return false;
        }

        if(!mdp_wrapper::unsetOverlay(fd.getFD(), mBorderFillId)) {
            ALOGE("%s: Failed during unsetOverlay",__func__);
            fd.close();
            return false;
        }
        fd.close();
        mBorderFillId = -1;
    }
    mSessionOpen = false;
    return true;
}
inline bool FloatingPipe::commit() {
    bool ret = setupBorderfillPipe();
    if(!ret) {
        ALOGE("%s: BorderFill setup failed!!", __func__);
        return false;
    }
    ret = mFloating.commit();
    if(!ret) {
        ALOGE("%s: Floating pipe commit failed!!",__func__);
        return false;
    }
    mSessionOpen = true;
    return true;
}
inline bool FloatingPipe::queueBuffer(int fd, uint32_t offset) {
    return mFloating.queueBuffer(fd, offset);
}
inline bool FloatingPipe::setCrop(const utils::Dim& dim) {
    mBorderFill_rect_w = dim.w;
    mBorderFill_rect_h = dim.h;
    return mFloating.setCrop(dim);
}
inline bool FloatingPipe::setPosition(const utils::Dim& dim) {
    return mFloating.setPosition(dim);
}
inline bool FloatingPipe::setTransform(const utils::eTransform& param) {
    return mFloating.setTransform(param);
}
inline bool FloatingPipe::setSource(const utils::PipeArgs& args) {
    utils::PipeArgs arg(args);
    //Cache unaligned values for
    //setting up BoarderFill pipe
    mBorderFill_src_w = arg.whf.w;
    mBorderFill_src_h = arg.whf.h;

    return mFloating.setSource(arg);
}
inline void FloatingPipe::dump() const {
    ALOGD("Floating Pipe");
    mFloating.dump();
}

} // overlay

#endif // OVERLAY_FLOATING_PIPE_H
