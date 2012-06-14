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

#ifndef OVERLAY_BYPASS_PIPE_H
#define OVERLAY_BYPASS_PIPE_H

#include "overlayGenPipe.h"
#include "overlayUtils.h"
#include "overlayCtrlData.h"
#include "overlayMdp.h"
#include "overlayRotator.h"

namespace overlay {

/* A specific impl of GenericPipe
* Whenever needed to have a pass through - we do it.
* If there is a special need for a different behavior - do it here
* PipeType = 0 (RGB), 1 (VG) */
template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
class BypassPipe : utils::NoCopy {
public:
    /* Please look at overlayGenPipe.h for info */
    explicit BypassPipe();
    ~BypassPipe();
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
    overlay::GenericPipe<ovutils::PRIMARY> mBypass;
};

//------------------Inlines and Templates---------------------

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline BypassPipe<PipeType, IsFg, Wait, Zorder>::BypassPipe() {}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline BypassPipe<PipeType, IsFg, Wait, Zorder>::~BypassPipe() {
    close();
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline bool BypassPipe<PipeType, IsFg, Wait, Zorder>::open(RotatorBase* rot) {
    ALOGE_IF(DEBUG_OVERLAY, "BypassPipe open");
    return mBypass.open(rot);
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline bool BypassPipe<PipeType, IsFg, Wait, Zorder>::close() {
    return mBypass.close();
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline bool BypassPipe<PipeType, IsFg, Wait, Zorder>::commit() {
    return mBypass.commit();
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline void BypassPipe<PipeType, IsFg, Wait, Zorder>::setId(int id) {
    mBypass.setId(id);
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline void BypassPipe<PipeType, IsFg, Wait, Zorder>::setMemoryId(int id) {
    mBypass.setMemoryId(id);
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline bool BypassPipe<PipeType, IsFg, Wait, Zorder>::queueBuffer(
        uint32_t offset) {
    return mBypass.queueBuffer(offset);
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline bool BypassPipe<PipeType, IsFg, Wait, Zorder>::dequeueBuffer(
        void*& buf) {
    return mBypass.dequeueBuffer(buf);
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline bool BypassPipe<PipeType, IsFg, Wait, Zorder>::waitForVsync() {
    return mBypass.waitForVsync();
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline bool BypassPipe<PipeType, IsFg, Wait, Zorder>::setCrop(
        const utils::Dim& dim) {
    return mBypass.setCrop(dim);
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline bool BypassPipe<PipeType, IsFg, Wait, Zorder>::start(
        const utils::PipeArgs& args) {
    return mBypass.start(args);
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline bool BypassPipe<PipeType, IsFg, Wait, Zorder>::setPosition(
        const utils::Dim& dim) {
    return mBypass.setPosition(dim);
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline bool BypassPipe<PipeType, IsFg, Wait, Zorder>::setParameter(
        const utils::Params& param) {
    return mBypass.setParameter(param);
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline bool BypassPipe<PipeType, IsFg, Wait, Zorder>::setSource(
        const utils::PipeArgs& args) {
    utils::PipeArgs arg(args);

    // Stride aligned to 32
    arg.whf.w = utils::align(arg.whf.w, 32);
    arg.whf.h = utils::align(arg.whf.h, 32);

    // VG or RG pipe
    if (PipeType == utils::OV_MDP_PIPE_VG) {
        setMdpFlags(arg.mdpFlags, utils::OV_MDP_PIPE_SHARE);
    }

    // Set is_fg flag
    arg.isFg = IsFg;

    // Wait or no wait
    arg.wait = Wait;

    // Z-order
    arg.zorder = Zorder;

    return mBypass.setSource(arg);
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline const utils::PipeArgs& BypassPipe<PipeType, IsFg, Wait,
        Zorder>::getArgs() const {
    return mBypass.getArgs();
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline utils::eOverlayPipeType BypassPipe<PipeType, IsFg, Wait,
        Zorder>::getOvPipeType() const {
    return utils::OV_PIPE_TYPE_BYPASS;
}

template <utils::eMdpPipeType PipeType, utils::eIsFg IsFg, utils::eWait Wait,
    utils::eZorder Zorder>
inline void BypassPipe<PipeType, IsFg, Wait, Zorder>::dump() const {
    ALOGE("Bypass VG Pipe");
    mBypass.dump();
}

} // overlay

#endif // OVERLAY_BYPASS_PIPE_H
