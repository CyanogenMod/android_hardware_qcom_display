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

#ifndef OVERLAY_H
#define OVERLAY_H

#include "overlayUtils.h"
#include "overlayState.h"
#include "overlayImpl.h"

namespace overlay {
/**/
class Overlay : utils::NoCopy {
public:
    /* dtor close */
    ~Overlay();

    /* Overlay related func */

    /* Following is the same as the pure virt interface in ov impl  */

    bool setSource(const utils::PipeArgs args[utils::MAX_PIPES],
            utils::eDest dest = utils::OV_PIPE_ALL);
    bool setCrop(const utils::Dim& d,
            utils::eDest dest = utils::OV_PIPE_ALL);
    bool setTransform(const int orientation,
            utils::eDest dest = utils::OV_PIPE_ALL);
    bool setPosition(const utils::Dim& dim,
            utils::eDest dest = utils::OV_PIPE_ALL);
    bool commit(utils::eDest dest = utils::OV_PIPE_ALL);

    bool queueBuffer(int fd, uint32_t offset,
            utils::eDest dest = utils::OV_PIPE_ALL);

    void dump() const;

    /* state related functions */
    void setState(utils::eOverlayState s);

    /* expose state */
    utils::eOverlayState getState() const;

    /* Closes open pipes */
    static void initOverlay();

    /* Returns the per-display singleton instance of overlay */
    static Overlay* getInstance(int disp);

private:
    /* Ctor setup */
    Overlay();

    /* reset all pointers */
    void reset();

    /* Holds the state, state transition logic
     * In the meantime, using simple enum rather than
     * a class */
    OverlayState mState;

    /* Holds the actual overlay impl, set when changing state*/
    OverlayImplBase *mOv;

    /* Per-display Singleton Instance HWC_NUM_DISPLAY_TYPES */
    static Overlay *sInstance[2];
};

} // overlay

#endif // OVERLAY_H
