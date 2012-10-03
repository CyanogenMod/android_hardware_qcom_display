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

#include "overlayUtils.h"
#include "overlayImpl.h"
#include "overlay.h"

#include "overlayMdp.h"
#include "overlayCtrlData.h"

namespace overlay {

//Helper
bool isStateValid(const utils::eOverlayState& st) {
    switch (st) {
        case utils::OV_CLOSED:
            ALOGE("Overlay %s failed, state is OV_CLOSED; set state first",
                    __FUNCTION__);
            return false;
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
        case utils::OV_2D_VIDEO_ON_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
        case utils::OV_UI_MIRROR:
        case utils::OV_2D_TRUE_UI_MIRROR:
        case utils::OV_UI_VIDEO_TV:
        case utils::OV_BYPASS_1_LAYER:
        case utils::OV_BYPASS_2_LAYER:
        case utils::OV_BYPASS_3_LAYER:
        case utils::OV_DUAL_DISP:
            break;
        default:
            OVASSERT(false, "%s Unknown state %d", __FUNCTION__, st);
            return false;
    }
    return true;
}

Overlay::Overlay(): mOv(0) {
}

Overlay::~Overlay() {
    mOv = mState.handleEvent(utils::OV_CLOSED, mOv);
    delete mOv;
    mOv = 0;
}

bool Overlay::commit(utils::eDest dest)
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    utils::eOverlayState st = mState.state();
    if(isStateValid(st)) {
        if(!mOv->commit(dest)) {
            ALOGE("Overlay %s failed", __FUNCTION__);
            return false;
        }
    }
    return true;
}

bool Overlay::queueBuffer(int fd, uint32_t offset,
        utils::eDest dest)
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    utils::eOverlayState st = mState.state();
    if(isStateValid(st)) {
        if(!mOv->queueBuffer(fd, offset, dest)) {
            ALOGE("Overlay %s failed", __FUNCTION__);
            return false;
        }
    }
    return true;
}

bool Overlay::setCrop(const utils::Dim& d,
        utils::eDest dest)
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    utils::eOverlayState st = mState.state();
    if(isStateValid(st)) {
        if(!mOv->setCrop(d, dest)) {
            ALOGE("Overlay %s failed", __FUNCTION__);
            return false;
        }
    }
    return true;
}
bool Overlay::setPosition(const utils::Dim& d,
        utils::eDest dest)
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    utils::eOverlayState st = mState.state();
    if(isStateValid(st)) {
        if(!mOv->setPosition(d, dest)) {
            ALOGE("Overlay %s failed", __FUNCTION__);
            return false;
        }
    }
    return true;
}

bool Overlay::setTransform(const int orient,
        utils::eDest dest)
{
    utils::eTransform transform =
            static_cast<utils::eTransform>(orient);

    utils::eOverlayState st = mState.state();
    if(isStateValid(st)) {
        if(!mOv->setTransform(transform, dest)) {
            ALOGE("Overlay %s failed", __FUNCTION__);
            return false;
        }
    }
    return true;
}

bool Overlay::setSource(const utils::PipeArgs args[utils::MAX_PIPES],
        utils::eDest dest)
{
    utils::PipeArgs margs[utils::MAX_PIPES] = {
        args[0], args[1], args[2] };
    utils::eOverlayState st = mState.state();

    if(isStateValid(st)) {
        if (!mOv->setSource(margs, dest)) {
            ALOGE("Overlay %s failed", __FUNCTION__);
            return false;
        }
    }
    return true;
}

void Overlay::dump() const
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    ALOGE("== Dump Overlay start ==");
    mState.dump();
    mOv->dump();
    ALOGE("== Dump Overlay end ==");
}

void Overlay::setState(utils::eOverlayState s) {
    mOv = mState.handleEvent(s, mOv);
}

utils::eOverlayState Overlay::getState() const {
    return mState.state();
}

Overlay *Overlay::sInstance[] = {0};

Overlay* Overlay::getInstance(int disp) {
    if(sInstance[disp] == NULL) {
        sInstance[disp] = new Overlay();
    }
    return sInstance[disp];
}

void Overlay::initOverlay() {
    if(utils::initOverlay() == -1) {
        ALOGE("utils::initOverlay() ERROR!!");
    }
}

} // overlay
