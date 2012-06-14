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

// MDP related FIXME move to state
#include "overlayMdp.h"
#include "overlayCtrlData.h"
#include "overlayRotator.h"

namespace overlay {

Overlay::Overlay(): mOv(0) {
}

Overlay::~Overlay() {
    if(mState.state() == utils::OV_CLOSED) return;
    close();
    delete mOv;
    mOv = 0;
}

bool Overlay::open() {
    // We need an empty open to just open the bare minimum for business
    return true;
}

void Overlay::reset(){
    if(mOv && !mOv->close()) {
        ALOGE("%s Overlay failed", __FUNCTION__);
    }

    delete mOv;
    mOv = 0;
}

bool Overlay::close()
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    // FIXME that one needs to move to the state machine class
    utils::eOverlayState st = mState.state();
    switch (st) {
        case utils::OV_CLOSED:
            // try to close any partially opened items
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
        case utils::OV_UI_MIRROR:
        case utils::OV_2D_TRUE_UI_MIRROR:
        case utils::OV_BYPASS_1_LAYER:
        case utils::OV_BYPASS_2_LAYER:
        case utils::OV_BYPASS_3_LAYER:
            mOv = mState.handleEvent(utils::OV_CLOSED, mOv);
            this->reset();
            break;
        default:
            OVASSERT(false, "close Unknown state %d", st);
            return false;
    }
    return true;
}

bool Overlay::commit(utils::eDest dest)
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    // FIXME that one needs to move to the state machine class
    utils::eOverlayState st = mState.state();
    switch (st) {
        case utils::OV_2D_VIDEO_ON_PANEL:
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
        case utils::OV_UI_MIRROR:
        case utils::OV_2D_TRUE_UI_MIRROR:
        case utils::OV_BYPASS_1_LAYER:
        case utils::OV_BYPASS_2_LAYER:
        case utils::OV_BYPASS_3_LAYER:
            if(!mOv->commit(dest)) {
                ALOGE("Overlay %s failed", __FUNCTION__);
                return false;
            }
            break;
        default:
            OVASSERT(false, "%s Unknown state %d", __FUNCTION__, st);
            return false;
    }
    return true;
}

bool Overlay::queueBuffer(uint32_t offset,
        utils::eDest dest)
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    // FIXME that one needs to move to the state machine class
    utils::eOverlayState st = mState.state();
    switch (st) {
        case utils::OV_2D_VIDEO_ON_PANEL:
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
        case utils::OV_UI_MIRROR:
        case utils::OV_2D_TRUE_UI_MIRROR:
        case utils::OV_BYPASS_1_LAYER:
        case utils::OV_BYPASS_2_LAYER:
        case utils::OV_BYPASS_3_LAYER:
            if(!mOv->queueBuffer(offset, dest)) {
                ALOGE("Overlay %s failed", __FUNCTION__);
                return false;
            }
            break;
        default:
            OVASSERT(false, "%s Unknown state %d", __FUNCTION__, st);
            return false;
    }
    return true;
}

bool Overlay::dequeueBuffer(void*& buf,
        utils::eDest dest)
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    // FIXME that one needs to move to the state machine class
    utils::eOverlayState st = mState.state();
    switch (st) {
        case utils::OV_2D_VIDEO_ON_PANEL:
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
        case utils::OV_UI_MIRROR:
        case utils::OV_2D_TRUE_UI_MIRROR:
        case utils::OV_BYPASS_1_LAYER:
        case utils::OV_BYPASS_2_LAYER:
        case utils::OV_BYPASS_3_LAYER:
            if(!mOv->dequeueBuffer(buf, dest)) {
                ALOGE("Overlay %s failed", __FUNCTION__);
                return false;
            }
            break;
        default:
            OVASSERT(false, "%s Unknown state %d", __FUNCTION__, st);
            return false;
    }
    return true;
}

bool Overlay::waitForVsync(utils::eDest dest)
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    // FIXME that one needs to move to the state machine class
    utils::eOverlayState st = mState.state();
    switch (st) {
        case utils::OV_2D_VIDEO_ON_PANEL:
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
        case utils::OV_UI_MIRROR:
        case utils::OV_2D_TRUE_UI_MIRROR:
        case utils::OV_BYPASS_1_LAYER:
        case utils::OV_BYPASS_2_LAYER:
        case utils::OV_BYPASS_3_LAYER:
            if(!mOv->waitForVsync(dest)) {
                ALOGE("Overlay %s failed", __FUNCTION__);
                return false;
            }
            break;
        default:
            OVASSERT(false, "%s Unknown state %d", __FUNCTION__, st);
            return false;
    }
    return true;
}

bool Overlay::setCrop(const utils::Dim& d,
        utils::eDest dest)
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    // FIXME that one needs to move to the state machine class
    utils::eOverlayState st = mState.state();
    switch (st) {
        case utils::OV_2D_VIDEO_ON_PANEL:
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
        case utils::OV_UI_MIRROR:
        case utils::OV_2D_TRUE_UI_MIRROR:
        case utils::OV_BYPASS_1_LAYER:
        case utils::OV_BYPASS_2_LAYER:
        case utils::OV_BYPASS_3_LAYER:
            if(!mOv->setCrop(d, dest)) {
                ALOGE("Overlay %s failed", __FUNCTION__);
                return false;
            }
            break;
        default:
            OVASSERT(false, "%s Unknown state %d", __FUNCTION__, st);
            return false;
    }
    return true;
}
bool Overlay::setPosition(const utils::Dim& d,
        utils::eDest dest)
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    // FIXME that one needs to move to the state machine class
    utils::eOverlayState st = mState.state();
    switch (st) {
        case utils::OV_2D_VIDEO_ON_PANEL:
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
        case utils::OV_UI_MIRROR:
        case utils::OV_2D_TRUE_UI_MIRROR:
        case utils::OV_BYPASS_1_LAYER:
        case utils::OV_BYPASS_2_LAYER:
        case utils::OV_BYPASS_3_LAYER:
            if(!mOv->setPosition(d, dest)) {
                ALOGE("Overlay %s failed", __FUNCTION__);
                return false;
            }
            break;
        default:
            OVASSERT(false, "setPos Unknown state %d", st);
            return false;
    }
    return true;
}
bool Overlay::setParameter(const utils::Params& param,
        utils::eDest dest)
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    // FIXME that one needs to move to the state machine class
    utils::eOverlayState st = mState.state();
    switch (st) {
        case utils::OV_2D_VIDEO_ON_PANEL:
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
        case utils::OV_UI_MIRROR:
        case utils::OV_2D_TRUE_UI_MIRROR:
        case utils::OV_BYPASS_1_LAYER:
        case utils::OV_BYPASS_2_LAYER:
        case utils::OV_BYPASS_3_LAYER:
            if(!mOv->setParameter(param, dest)) {
                ALOGE("Overlay %s failed", __FUNCTION__);
                return false;
            }
            break;
        default:
            OVASSERT(false, "%s Unknown state %d", __FUNCTION__ , st);
            return false;
    }
    return true;
}
bool Overlay::setSource(const utils::PipeArgs args[utils::MAX_PIPES],
        utils::eDest dest)
{
    // FIXME that one needs to move to the state machine class
    utils::PipeArgs margs[utils::MAX_PIPES] = {
        args[0], args[1], args[2] };
    utils::eOverlayState st = mState.state();

    switch (st) {
        case utils::OV_CLOSED:
            // if we get setSource when we are closed, then
            // we will assume tranistion to OV_2D_VIDEO_ON_PANEL
            // returns overlay
            mOv = mState.handle_closed(utils::OV_2D_VIDEO_ON_PANEL);
            if (!mOv) {
                ALOGE("Overlay %s failed", __FUNCTION__);
                this->reset(); // cleanup
                return false;
            }
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
        case utils::OV_UI_MIRROR:
        case utils::OV_BYPASS_1_LAYER:
        case utils::OV_BYPASS_2_LAYER:
        case utils::OV_BYPASS_3_LAYER:
            // no tweaking
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_TV:
            margs[utils::CHANNEL_1].zorder = utils::ZORDER_1;
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            // If displaying on both, external VG pipe set to be no wait
            margs[utils::CHANNEL_1].wait = utils::NO_WAIT;
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            // Set zorder -- external VG pipe (video) gets 0, RGB pipe (UI) gets 1
            margs[utils::CHANNEL_1].zorder = utils::ZORDER_0;
            margs[utils::CHANNEL_2].zorder = utils::ZORDER_1;
            // External VG (video) and RGB (UI) pipe set to be no wait
            margs[utils::CHANNEL_0].wait = utils::WAIT;
            margs[utils::CHANNEL_1].wait = utils::NO_WAIT;
            margs[utils::CHANNEL_2].wait = utils::NO_WAIT;
            break;
        default:
            OVASSERT(false, "%s Unknown state %d", __FUNCTION__, st);
            return false;
    }

    if (!mOv->setSource(margs, dest)) {
        ALOGE("Overlay %s failed", __FUNCTION__);
        return false;
    }

    return true;
}
void Overlay::setMemoryId(int id, utils::eDest dest)
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    // FIXME that one needs to move to the state machine class
    utils::eOverlayState st = mState.state();
    switch (st) {
        case utils::OV_2D_VIDEO_ON_PANEL:
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
        case utils::OV_3D_VIDEO_ON_3D_TV:
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
        case utils::OV_UI_MIRROR:
        case utils::OV_2D_TRUE_UI_MIRROR:
        case utils::OV_BYPASS_1_LAYER:
        case utils::OV_BYPASS_2_LAYER:
        case utils::OV_BYPASS_3_LAYER:
            mOv->setMemoryId(id, dest);
            break;
        default:
            OVASSERT(false, "setMemId Unknown state %d", st);
    }
}


void Overlay::dump() const
{
    OVASSERT(mOv,
            "%s Overlay and Rotator should be init at this point",
            __FUNCTION__);
    // FIXME dump tate object, factory
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

Overlay *Overlay::sInstance = 0;

Overlay* Overlay::getInstance() {
    if(sInstance == NULL)
        sInstance = new Overlay();
    return sInstance;
}

/****  NullPipe  ****/

bool NullPipe::open(RotatorBase*) {
    ALOGE_IF(DEBUG_OVERLAY, "NullPipe open");
    return true;
}
bool NullPipe::close() { return true; }
bool NullPipe::commit() { return true; }
bool NullPipe::start(const utils::PipeArgs&) { return true; }
bool NullPipe::setCrop(const utils::Dim&) { return true; }
bool NullPipe::setPosition(const utils::Dim&) { return true; }
bool NullPipe::setParameter(const utils::Params&) { return true; }
bool NullPipe::setSource(const utils::PipeArgs&) { return true; }
bool NullPipe::queueBuffer(uint32_t offset) { return true; }
bool NullPipe::dequeueBuffer(void*&) { return true; }
bool NullPipe::waitForVsync() { return true; }
void NullPipe::setMemoryId(int) {}
// NullPipe will return by val here as opposed to other Pipes.
utils::PipeArgs NullPipe::getArgs() const { return utils::PipeArgs(); }
utils::eOverlayPipeType NullPipe::getOvPipeType() const {
    return utils::OV_PIPE_TYPE_NULL;
}
void NullPipe::dump() const {
    ALOGE("== NullPipe (null) start/end ==");
}

} // overlay
