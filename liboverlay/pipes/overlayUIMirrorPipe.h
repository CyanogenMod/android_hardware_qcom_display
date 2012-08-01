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
    overlay::GenericPipe<ovutils::EXTERNAL> mUI;
    utils::eTransform mPrimFBOr; //Primary FB's orientation
};

//----------------------------Inlines -----------------------------

inline UIMirrorPipe::UIMirrorPipe() { mPrimFBOr = utils::OVERLAY_TRANSFORM_0; }
inline UIMirrorPipe::~UIMirrorPipe() { close(); }
inline bool UIMirrorPipe::init(RotatorBase* rot) {
    ALOGE_IF(DEBUG_OVERLAY, "UIMirrorPipe init");
    bool ret = mUI.init(rot);
    //If source to rotator is FB, which is the case with UI Mirror pipe,
    //we need to inform driver during playback, since FB does not use ION.
    rot->setSrcFB();
    return ret;
}
inline bool UIMirrorPipe::close() { return mUI.close(); }
inline bool UIMirrorPipe::commit() { return mUI.commit(); }
inline bool UIMirrorPipe::queueBuffer(int fd, uint32_t offset) {
    return mUI.queueBuffer(fd, offset);
}
inline bool UIMirrorPipe::setCrop(const utils::Dim& dim) {
    return mUI.setCrop(dim); }

inline bool UIMirrorPipe::setPosition(const utils::Dim& dim) {
    ovutils::Dim pdim;
    //using utils::eTransform;
    switch (mPrimFBOr) {
        case utils::OVERLAY_TRANSFORM_0:
        case utils::OVERLAY_TRANSFORM_ROT_180:
            {
                ovutils::Whf whf(dim.w, dim.h, 0);
                pdim = mUI.getAspectRatio(whf);
                break;
            }
        case utils::OVERLAY_TRANSFORM_ROT_90:
        case utils::OVERLAY_TRANSFORM_ROT_270:
            {
                // Calculate the Aspectratio for the UI in the landscape mode
                // Width and height will be swapped as there is rotation
                ovutils::Whf whf(dim.h, dim.w, 0);
                pdim = mUI.getAspectRatio(whf);
                break;
            }
        default:
            ALOGE("%s: Unknown orientation %d", __FUNCTION__, dim.o);
            return false;
    }
    return mUI.setPosition(pdim);
}

inline bool UIMirrorPipe::setTransform(const utils::eTransform& param) {

    //Cache the primary FB orientation, since the TV's will be 0, we need this
    //info to translate later.
    mPrimFBOr = param;
    utils::eTransform transform = param;

    // Figure out orientation to transform to
    switch (param) {
        case utils::OVERLAY_TRANSFORM_0:
            transform = utils::OVERLAY_TRANSFORM_0;
            break;
        case utils::OVERLAY_TRANSFORM_ROT_180:
        //If prim FB is drawn 180 rotated, rotate by additional 180 to make
        //it to 0, which is TV's orientation.
            transform = utils::OVERLAY_TRANSFORM_ROT_180;
            break;
        case utils::OVERLAY_TRANSFORM_ROT_90:
        //If prim FB is drawn 90 rotated, rotate by additional 270 to make
        //it to 0, which is TV's orientation.
            transform = utils::OVERLAY_TRANSFORM_ROT_270;
            break;
        case utils::OVERLAY_TRANSFORM_ROT_270:
        //If prim FB is drawn 270 rotated, rotate by additional 90 to make
        //it to 0, which is TV's orientation.
            transform = utils::OVERLAY_TRANSFORM_ROT_90;
            break;
        default:
            ALOGE("%s: Unknown orientation %d", __FUNCTION__,
                    static_cast<int>(param));
            return false;
    }

    return mUI.setTransform(transform);
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
inline void UIMirrorPipe::dump() const {
    ALOGE("UI Mirror Pipe");
    mUI.dump();
}


} // overlay

#endif // OVERLAY_UI_PIPE_H
