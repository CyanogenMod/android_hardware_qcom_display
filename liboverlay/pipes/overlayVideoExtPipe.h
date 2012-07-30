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

#ifndef OVERLAY_VIDEO_EXT_PIPE_H
#define OVERLAY_VIDEO_EXT_PIPE_H

#include "overlayGenPipe.h"
#include "overlayUtils.h"
#include "overlayCtrlData.h"
#include "overlayMdp.h"
#include "overlayRotator.h"

namespace overlay {

/* A specific impl of GenericPipe
* Whenever needed to have a pass through - we do it.
* If there is a special need for a different behavior - do it here */
class VideoExtPipe : utils::NoCopy {
public:
    /* Please look at overlayGenPipe.h for info */
    explicit VideoExtPipe();
    ~VideoExtPipe();
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
    overlay::GenericPipe<ovutils::EXTERNAL> mVideoExt;
};

//------------------Inlines -----------------------------

inline VideoExtPipe::VideoExtPipe() {}
inline VideoExtPipe::~VideoExtPipe() { close(); }
inline bool VideoExtPipe::init(RotatorBase* rot) {
    ALOGE_IF(DEBUG_OVERLAY, "VideoExtPipe init");
    return mVideoExt.init(rot);
}
inline bool VideoExtPipe::close() { return mVideoExt.close(); }
inline bool VideoExtPipe::commit() { return mVideoExt.commit(); }
inline bool VideoExtPipe::queueBuffer(int fd, uint32_t offset) {
    return mVideoExt.queueBuffer(fd, offset);
}
inline bool VideoExtPipe::setCrop(const utils::Dim& dim) {
    return mVideoExt.setCrop(dim);
}
inline bool VideoExtPipe::setPosition(const utils::Dim& dim)
{
    utils::Dim d;
    // Need to change dim to aspect ratio
    if (utils::FrameBufferInfo::getInstance()->supportTrueMirroring()) {
        // Use dim info to calculate aspect ratio for true UI mirroring
        d = mVideoExt.getAspectRatio(dim);
    } else {
        // Use cached crop data to get aspect ratio
        utils::Dim crop = mVideoExt.getCrop();
        utils::Whf whf(crop.w, crop.h, 0);
        d = mVideoExt.getAspectRatio(whf);
    }
    ALOGE_IF(DEBUG_OVERLAY, "Calculated aspect ratio for EXT: x=%d, y=%d, w=%d,"
            "h=%d, o=%d",
            d.x, d.y, d.w, d.h, d.o);
    return mVideoExt.setPosition(d);
}
inline bool VideoExtPipe::setTransform(const utils::eTransform& param) {
    return mVideoExt.setTransform(param);
}
inline bool VideoExtPipe::setSource(const utils::PipeArgs& args) {
    utils::PipeArgs arg(args);
    return mVideoExt.setSource(arg);
}
inline void VideoExtPipe::dump() const {
    ALOGE("Video Ext Pipe");
    mVideoExt.dump();
}


} // overlay

#endif // OVERLAY_VIDEO_EXT_PIPE_H
