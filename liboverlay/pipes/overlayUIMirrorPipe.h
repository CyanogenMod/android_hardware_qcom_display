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

#ifndef OVERLAY_UI_PIPE_H
#define OVERLAY_UI_PIPE_H

#include "overlayGenPipe.h"
#include "overlayUtils.h"
#include "overlayCtrlData.h"
#include "overlayMdp.h"
#include "overlayRotator.h"

namespace overlay {

/* A specific impl of GenericPipe
* Whenever needed to have a pass through - we do it.
* If there is a special need for a different behavior - do it here */
class UIMirrorPipe : utils::NoCopy {
public:
    /* Please look at overlayGenPipe.h for info */
    explicit UIMirrorPipe();
    ~UIMirrorPipe();
    bool open(RotatorBase* rot);
    bool close();
    bool commit();
    void setId(int id);
    void setMemoryId(int id);
    bool queueBuffer(uint32_t offset);
    bool dequeueBuffer(void*& buf);
    bool waitForVsync();
    bool setCrop(const utils::Dim& dim);
    bool start(const utils::PipeArgs& args);
    bool setPosition(const utils::Dim& dim);
    bool setParameter(const utils::Params& param);
    bool setSource(const utils::PipeArgs& args);
    const utils::PipeArgs& getArgs() const;
    utils::eOverlayPipeType getOvPipeType() const;
    void dump() const;
private:
    overlay::GenericPipe<ovutils::EXTERNAL> mUI;
};

//----------------------------Inlines -----------------------------

inline UIMirrorPipe::UIMirrorPipe() {}
inline UIMirrorPipe::~UIMirrorPipe() { close(); }
inline bool UIMirrorPipe::open(RotatorBase* rot) {
    ALOGE_IF(DEBUG_OVERLAY, "UIMirrorPipe open");
    bool ret = mUI.open(rot);
    //If source to rotator is framebuffer, which is the case we UI Mirror pipe,
    //we need to inform driver during playback. Since FB does not use ION.
    rot->setSrcFB(true);
    return ret;
}
inline bool UIMirrorPipe::close() { return mUI.close(); }
inline bool UIMirrorPipe::commit() { return mUI.commit(); }
inline void UIMirrorPipe::setId(int id) { mUI.setId(id); }
inline void UIMirrorPipe::setMemoryId(int id) { mUI.setMemoryId(id); }
inline bool UIMirrorPipe::queueBuffer(uint32_t offset) {
    return mUI.queueBuffer(offset); }
inline bool UIMirrorPipe::dequeueBuffer(void*& buf) {
    return mUI.dequeueBuffer(buf); }
inline bool UIMirrorPipe::waitForVsync() {
    return mUI.waitForVsync(); }
inline bool UIMirrorPipe::setCrop(const utils::Dim& dim) {
    return mUI.setCrop(dim); }
inline bool UIMirrorPipe::start(const utils::PipeArgs& args) {
    if(!mUI.start(args)) {
        ALOGE("%s failed to start", __FUNCTION__);
        return false;
    }
    return true;
}
inline bool UIMirrorPipe::setPosition(const utils::Dim& dim) {

    ovutils::Dim pdim;
    switch (dim.o) {
        case 0:
        case HAL_TRANSFORM_ROT_180:
            {
                ovutils::Whf whf(dim.x, dim.y, 0);
                pdim = mUI.getAspectRatio(whf);
                break;
            }
        case HAL_TRANSFORM_ROT_90:
        case HAL_TRANSFORM_ROT_270:
            {
                // Calculate the Aspectratio for the UI in the landscape mode
                // Width and height will be swapped as there is rotation
                ovutils::Whf whf(dim.y, dim.x, 0);
                pdim = mUI.getAspectRatio(whf);
                break;
            }
        default:
            ALOGE("%s: Unknown orientation %d", __FUNCTION__, dim.o);
            return false;
    }

    ovutils::even_out(pdim.x);
    ovutils::even_out(pdim.y);
    ovutils::even_out(pdim.w);
    ovutils::even_out(pdim.h);
    return mUI.setPosition(pdim);
}
inline bool UIMirrorPipe::setParameter(const utils::Params& param) {

    OVASSERT(utils::OVERLAY_TRANSFORM_UI == param.param,
            "%p Expecting OVERLAY_TRANSFORM_UI", __FUNCTION__);

    int orientation = param.value;

    // Figure out orientation to transform to
    switch (param.value) {
        case 0:
            orientation = 0;
            break;
        case HAL_TRANSFORM_ROT_180:
            orientation = HAL_TRANSFORM_ROT_180;
            break;
        case HAL_TRANSFORM_ROT_90:
            orientation = HAL_TRANSFORM_ROT_270;
            break;
        case HAL_TRANSFORM_ROT_270:
            orientation = HAL_TRANSFORM_ROT_90;
            break;
        default:
            ALOGE("%s: Unknown orientation %d", __FUNCTION__, param.value);
            return false;
    }

    ovutils::eTransform transform =
            static_cast<ovutils::eTransform>(orientation);
    const ovutils::Params prms (ovutils::OVERLAY_TRANSFORM, transform);
    return mUI.setParameter(prms);
}

inline bool UIMirrorPipe::setSource(const utils::PipeArgs& args) {
    utils::PipeArgs arg(args);

    // Rotator flag enabled because buffer comes from fb
    arg.rotFlags = utils::ROT_FLAG_ENABLED;

    // For true UI mirroring, want the UI to go through available RGB pipe
    // so do not set the PIPE SHARE flag which allocates VG pipe
    if (utils::FrameBufferInfo::getInstance()->supportTrueMirroring()) {
        arg.mdpFlags = static_cast<utils::eMdpFlags>(
                arg.mdpFlags & ~utils::OV_MDP_PIPE_SHARE);
    }

    return mUI.setSource(arg);
}
inline const utils::PipeArgs& UIMirrorPipe::getArgs() const {
    return mUI.getArgs();
}
inline utils::eOverlayPipeType UIMirrorPipe::getOvPipeType() const {
    return utils::OV_PIPE_TYPE_UI_MIRROR;
}
inline void UIMirrorPipe::dump() const {
    ALOGE("UI Mirror Pipe");
    mUI.dump();
}


} // overlay

#endif // OVERLAY_UI_PIPE_H
