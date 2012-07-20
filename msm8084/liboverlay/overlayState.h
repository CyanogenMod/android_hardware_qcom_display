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

#ifndef OVERLAY_STATE_H
#define OVERLAY_STATE_H

#include "overlayUtils.h"
#include "overlayImpl.h"
#include "overlayRotator.h"
#include "pipes/overlayGenPipe.h"
#include "pipes/overlayVideoExtPipe.h"
#include "pipes/overlayUIMirrorPipe.h"
#include "pipes/overlay3DPipe.h"

namespace overlay {

/*
* Used by Overlay class. Invokes each event
* */

class OverlayState : utils::NoCopy {
public:
    /**/
    explicit OverlayState();

    /**/
    ~OverlayState();

    /* return current state */
    utils::eOverlayState state() const;

    /* Overlay Event */

    /* Hard reset to a new state. If the state is the same
     * as the current one, it would be a no-op */
    OverlayImplBase* reset(utils::eOverlayState s);

    /* Caller pass the state to the handleEvent function.
     * The input is the current OverlayImplBase*, and output is
     * a pointer to (possibly new) instance of OverlayImplBase
     * The eFormat can be 2D/3D etc. */
    OverlayImplBase* handleEvent(utils::eOverlayState s,
            OverlayImplBase* ov);

    /* Transitions from XXX to XXX */
    OverlayImplBase* handle_closed(utils::eOverlayState s);
    OverlayImplBase* handle_2D_2DPanel(utils::eOverlayState s,
            OverlayImplBase* ov);
    OverlayImplBase* handle_2D_2DTV(utils::eOverlayState s,
            OverlayImplBase* ov);
    OverlayImplBase* handle_3D_2DPanel(utils::eOverlayState s,
            OverlayImplBase* ov);
    OverlayImplBase* handle_3D_3DPanel(utils::eOverlayState s,
            OverlayImplBase* ov);
    OverlayImplBase* handle_3D_3DTV(utils::eOverlayState s,
            OverlayImplBase* ov);
    OverlayImplBase* handle_3D_2DTV(utils::eOverlayState s,
            OverlayImplBase* ov);
    OverlayImplBase* handle_UI_Mirror(utils::eOverlayState s,
            OverlayImplBase* ov);
    OverlayImplBase* handle_2D_trueUI_Mirror(utils::eOverlayState s,
            OverlayImplBase* ov);
    OverlayImplBase* handle_bypass(utils::eOverlayState s,
            OverlayImplBase* ov);

    /* Transition from any state to 2D video on 2D panel */
    OverlayImplBase* handle_xxx_to_2D_2DPanel(OverlayImplBase* ov);

    /* Transition from any state to 2D video on 2D panel and 2D TV */
    OverlayImplBase* handle_xxx_to_2D_2DTV(OverlayImplBase* ov);

    /* Transition from any state to 3D video on 2D panel */
    OverlayImplBase* handle_xxx_to_3D_2DPanel(OverlayImplBase* ov);

    /* Transition from any state to 3D video on 2D panel and 2D TV */
    OverlayImplBase* handle_xxx_to_3D_2DTV(OverlayImplBase* ov);

    /* Transition from any state to 2D video true UI mirroring (2D video + UI) */
    OverlayImplBase* handle_xxx_to_2D_trueUI_Mirror(OverlayImplBase* ov);

    /* Transitions from any state to 1 layer composition bypass */
    OverlayImplBase* handle_xxx_to_bypass1(OverlayImplBase* ov);

    /* Transitions from any state to 2 layers composition bypass */
    OverlayImplBase* handle_xxx_to_bypass2(OverlayImplBase* ov);

    /* Transitions from any state to 3 layers composition bypass */
    OverlayImplBase* handle_xxx_to_bypass3(OverlayImplBase* ov);

    /* Dump */
    void dump() const;
private:
    /* States here */
    utils::eOverlayState mState;
};

//------------------------State Traits --------------------------

// primary has nothing
template <int STATE> struct StateTraits {};

/*
 * For 3D_xxx we need channel ID besides the FBx since
 * get crop/position 3D need that to determine pos/crop
 * info.
 * */

template <> struct StateTraits<utils::OV_2D_VIDEO_ON_PANEL>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0;
    typedef overlay::NullPipe pipe1;   // place holder
    typedef overlay::NullPipe pipe2;   // place holder

    typedef Rotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;

    typedef overlay::OverlayImpl<pipe0> ovimpl;
};

template <> struct StateTraits<utils::OV_2D_VIDEO_ON_PANEL_TV>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0;
    typedef overlay::VideoExtPipe pipe1;
    typedef overlay::NullPipe pipe2;   // place holder

    typedef Rotator rot0;
    typedef Rotator rot1;
    typedef NullRotator rot2;

    typedef overlay::OverlayImpl<pipe0, pipe1> ovimpl;
};

template <> struct StateTraits<utils::OV_3D_VIDEO_ON_2D_PANEL>
{
    typedef overlay::M3DPrimaryPipe<utils::OV_PIPE0> pipe0;
    typedef overlay::NullPipe pipe1;   // place holder
    typedef overlay::NullPipe pipe2;   // place holder

    typedef Rotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;

    typedef overlay::OverlayImpl<pipe0> ovimpl;
};

template <> struct StateTraits<utils::OV_3D_VIDEO_ON_3D_PANEL>
{
    typedef overlay::S3DPrimaryPipe<utils::OV_PIPE0> pipe0;
    typedef overlay::S3DPrimaryPipe<utils::OV_PIPE1> pipe1;
    typedef overlay::NullPipe pipe2;   // place holder

    typedef Rotator rot0;
    typedef Rotator rot1;
    typedef NullRotator rot2;

    typedef overlay::OverlayImpl<pipe0, pipe1> ovimpl;
};

template <> struct StateTraits<utils::OV_3D_VIDEO_ON_3D_TV>
{
    typedef overlay::S3DExtPipe<utils::OV_PIPE0> pipe0;
    typedef overlay::S3DExtPipe<utils::OV_PIPE1> pipe1;
    typedef overlay::NullPipe pipe2;   // place holder

    typedef NullRotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;

    typedef overlay::OverlayImpl<pipe0, pipe1> ovimpl;
};

template <> struct StateTraits<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>
{
    typedef overlay::M3DPrimaryPipe<utils::OV_PIPE0> pipe0;
    typedef overlay::M3DExtPipe<utils::OV_PIPE1> pipe1;
    typedef overlay::NullPipe pipe2;   // place holder

    typedef Rotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;

    typedef overlay::OverlayImpl<pipe0, pipe1> ovimpl;
};

template <> struct StateTraits<utils::OV_UI_MIRROR>
{
    typedef overlay::UIMirrorPipe pipe0;
    typedef overlay::NullPipe pipe1;   // place holder
    typedef overlay::NullPipe pipe2;   // place holder

    typedef Rotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;

    typedef overlay::OverlayImpl<pipe0> ovimpl;
};

template <> struct StateTraits<utils::OV_2D_TRUE_UI_MIRROR>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0;
    typedef overlay::VideoExtPipe pipe1;
    typedef overlay::UIMirrorPipe pipe2;

    typedef Rotator rot0;
    typedef Rotator rot1;
    typedef Rotator rot2;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2> ovimpl;
};

template <> struct StateTraits<utils::OV_BYPASS_1_LAYER>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0;
    typedef overlay::NullPipe pipe1;   // place holder
    typedef overlay::NullPipe pipe2;   // place holder

    typedef NullRotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;

    typedef overlay::OverlayImpl<pipe0> ovimpl;
};

template <> struct StateTraits<utils::OV_BYPASS_2_LAYER>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0;
    typedef overlay::GenericPipe<utils::PRIMARY> pipe1;
    typedef overlay::NullPipe pipe2;   // place holder

    typedef NullRotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;

    typedef overlay::OverlayImpl<pipe0, pipe1> ovimpl;
};

template <> struct StateTraits<utils::OV_BYPASS_3_LAYER>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0;
    typedef overlay::GenericPipe<utils::PRIMARY> pipe1;
    typedef overlay::GenericPipe<utils::PRIMARY> pipe2;

    typedef NullRotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2> ovimpl;
};


//------------------------Inlines --------------------------------

inline OverlayState::OverlayState() : mState(utils::OV_CLOSED)
{}

inline OverlayState::~OverlayState() {}

inline utils::eOverlayState OverlayState::state() const
{
    return mState;
}

inline OverlayImplBase* OverlayState::reset(utils::eOverlayState s)
{
    return handleEvent(s, 0);
}

inline void OverlayState::dump() const
{
    ALOGE("== Dump state %d start/end ==", mState);
}

template <int STATE>
inline OverlayImplBase* handle_closed_to_xxx()
{
    OverlayImplBase* ov = new typename StateTraits<STATE>::ovimpl;
    RotatorBase* rot0 = new typename StateTraits<STATE>::rot0;
    RotatorBase* rot1 = new typename StateTraits<STATE>::rot1;
    RotatorBase* rot2 = new typename StateTraits<STATE>::rot2;
    if(!ov->init(rot0, rot1, rot2)) {
        ALOGE("Overlay failed to init in state %d", STATE);
        return 0;
    }
    return ov;
}

inline OverlayImplBase* handle_xxx_to_closed(OverlayImplBase* ov)
{
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);

    if(!ov->close()) {
        ALOGE("%s: Failed to ov close", __FUNCTION__);
    }
    delete ov;
    ov = 0;
    return 0;
}

/* Hard transitions from any state to any state will close and then init */
template <int STATE>
inline OverlayImplBase* handle_xxx_to_xxx(OverlayImplBase* ov)
{
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);

    handle_xxx_to_closed(ov);
    return handle_closed_to_xxx<STATE>();
}

inline OverlayImplBase* OverlayState::handleEvent(utils::eOverlayState newState,
        OverlayImplBase* ov)
{
    OverlayImplBase* newov = ov; // at least, we return the same
    if (mState != newState) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: state changed %s-->%s",
                __FUNCTION__, getStateString(mState), getStateString(newState));
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: no state change, state=%s",
                __FUNCTION__, getStateString(newState));
        return newov;
    }

    switch(mState)
    {
        case utils::OV_CLOSED:
            newov = handle_closed(newState);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            newov = handle_2D_2DPanel(newState, ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            newov = handle_2D_2DTV(newState, ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            newov = handle_3D_2DPanel(newState, ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            newov = handle_3D_3DPanel(newState, ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            newov = handle_3D_3DTV(newState, ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            newov = handle_3D_2DTV(newState, ov);
            break;
        case utils::OV_UI_MIRROR:
            newov = handle_UI_Mirror(newState, ov);
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            newov = handle_2D_trueUI_Mirror(newState, ov);
            break;
        case utils::OV_BYPASS_1_LAYER:
        case utils::OV_BYPASS_2_LAYER:
        case utils::OV_BYPASS_3_LAYER:
            newov = handle_bypass(newState, ov);
            break;
        default:
            ALOGE("%s: unknown state=%d", __FUNCTION__, mState);
    }

    // FIXME, how to communicate bad transition?
    // Should we have bool returned from transition func?
    // This is also a very good interview question.

    return newov;
}

// Transitions from closed to XXX
inline OverlayImplBase* OverlayState::handle_closed(utils::eOverlayState s)
{
    OverlayImplBase* ov = 0;
    switch(s)
    {
        case utils::OV_CLOSED:
            // no state change
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            ov = handle_closed_to_xxx<utils::OV_2D_VIDEO_ON_PANEL>();
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            ov = handle_closed_to_xxx<utils::OV_2D_VIDEO_ON_PANEL_TV>();
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            ov = handle_closed_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL>();
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            ov = handle_closed_to_xxx<utils::OV_3D_VIDEO_ON_3D_PANEL>();
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            ov = handle_closed_to_xxx<utils::OV_3D_VIDEO_ON_3D_TV>();
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            ov = handle_closed_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>();
            break;
        case utils::OV_UI_MIRROR:
            ov = handle_closed_to_xxx<utils::OV_UI_MIRROR>();
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            ov = handle_closed_to_xxx<utils::OV_2D_TRUE_UI_MIRROR>();
            break;
        case utils::OV_BYPASS_1_LAYER:
            ov = handle_closed_to_xxx<utils::OV_BYPASS_1_LAYER>();
            break;
        case utils::OV_BYPASS_2_LAYER:
            ov = handle_closed_to_xxx<utils::OV_BYPASS_2_LAYER>();
            break;
        case utils::OV_BYPASS_3_LAYER:
            ov = handle_closed_to_xxx<utils::OV_BYPASS_3_LAYER>();
            break;
        default:
            ALOGE("%s: unknown state=%d", __FUNCTION__, s);
    }
    mState = s;
    return ov;
}

// Transitions from 2D video on 2D panel to XXX
inline OverlayImplBase* OverlayState::handle_2D_2DPanel(
        utils::eOverlayState s,
        OverlayImplBase* ov)
{
    OverlayImplBase* newov = ov;
    switch(s)
    {
        case utils::OV_CLOSED:
            newov = handle_xxx_to_closed(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            // no state change
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            newov = handle_xxx_to_2D_2DTV(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>(ov);
            break;
        case utils::OV_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_UI_MIRROR>(ov);
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            newov = handle_xxx_to_2D_trueUI_Mirror(ov);
            break;
        case utils::OV_BYPASS_1_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_1_LAYER>(ov);
            break;
        case utils::OV_BYPASS_2_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_2_LAYER>(ov);
            break;
        case utils::OV_BYPASS_3_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_3_LAYER>(ov);
            break;
        default:
            ALOGE("%s: unknown state=%d", __FUNCTION__, s);
    }
    mState = s;
    return newov;
}

// Transitions from 2D video on 2D panel and 2D TV to XXX
inline OverlayImplBase* OverlayState::handle_2D_2DTV(
        utils::eOverlayState s,
        OverlayImplBase* ov)
{
    OverlayImplBase* newov = ov;
    switch(s)
    {
        case utils::OV_CLOSED:
            newov = handle_xxx_to_closed(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            newov = handle_xxx_to_2D_2DPanel(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            // no state change
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>(ov);
            break;
        case utils::OV_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_UI_MIRROR>(ov);
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            newov = handle_xxx_to_2D_trueUI_Mirror(ov);
            break;
        case utils::OV_BYPASS_1_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_1_LAYER>(ov);
            break;
        case utils::OV_BYPASS_2_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_2_LAYER>(ov);
            break;
        case utils::OV_BYPASS_3_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_3_LAYER>(ov);
            break;
        default:
            ALOGE("%s: unknown state=%d", __FUNCTION__, s);
    }
    mState = s;
    return newov;
}

// Transitions from 3D video on 2D panel to XXX
inline OverlayImplBase* OverlayState::handle_3D_2DPanel(
        utils::eOverlayState s,
        OverlayImplBase* ov)
{
    OverlayImplBase* newov = ov;
    switch(s)
    {
        case utils::OV_CLOSED:
            newov = handle_xxx_to_closed(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_2D_VIDEO_ON_PANEL>(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            newov = handle_xxx_to_xxx<utils::OV_2D_VIDEO_ON_PANEL_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            // no state change
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            newov = handle_xxx_to_3D_2DTV(ov);
            break;
        case utils::OV_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_UI_MIRROR>(ov);
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_2D_TRUE_UI_MIRROR>(ov);
            break;
        case utils::OV_BYPASS_1_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_1_LAYER>(ov);
            break;
        case utils::OV_BYPASS_2_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_2_LAYER>(ov);
            break;
        case utils::OV_BYPASS_3_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_3_LAYER>(ov);
            break;
        default:
            ALOGE("%s: unknown state=%d", __FUNCTION__, s);
    }
    mState = s;
    return newov;
}

// Transitions from 3D video on 3D panel to XXX
inline OverlayImplBase* OverlayState::handle_3D_3DPanel(
        utils::eOverlayState s,
        OverlayImplBase* ov)
{
    OverlayImplBase* newov = ov;
    switch(s)
    {
        case utils::OV_CLOSED:
            newov = handle_xxx_to_closed(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_2D_VIDEO_ON_PANEL>(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            newov = handle_xxx_to_xxx<utils::OV_2D_VIDEO_ON_PANEL_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            // no state change
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>(ov);
            break;
        case utils::OV_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_UI_MIRROR>(ov);
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_2D_TRUE_UI_MIRROR>(ov);
            break;
        case utils::OV_BYPASS_1_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_1_LAYER>(ov);
            break;
        case utils::OV_BYPASS_2_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_2_LAYER>(ov);
            break;
        case utils::OV_BYPASS_3_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_3_LAYER>(ov);
            break;
        default:
            ALOGE("%s: unknown state=%d", __FUNCTION__, s);
    }
    mState = s;
    return newov;
}

// Transitions from 3D video on 3D TV to XXX
inline OverlayImplBase* OverlayState::handle_3D_3DTV(
        utils::eOverlayState s,
        OverlayImplBase* ov)
{
    OverlayImplBase* newov = ov;
    switch(s)
    {
        case utils::OV_CLOSED:
            newov = handle_xxx_to_closed(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_2D_VIDEO_ON_PANEL>(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            newov = handle_xxx_to_xxx<utils::OV_2D_VIDEO_ON_PANEL_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            // no state change
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>(ov);
            break;
        case utils::OV_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_UI_MIRROR>(ov);
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_2D_TRUE_UI_MIRROR>(ov);
            break;
        case utils::OV_BYPASS_1_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_1_LAYER>(ov);
            break;
        case utils::OV_BYPASS_2_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_2_LAYER>(ov);
            break;
        case utils::OV_BYPASS_3_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_3_LAYER>(ov);
            break;
        default:
            ALOGE("%s: unknown state=%d", __FUNCTION__, s);
    }
    mState = s;
    return newov;
}

// Transitions from 3D video on 2D panel and 2D TV to XXX
inline OverlayImplBase* OverlayState::handle_3D_2DTV(
        utils::eOverlayState s,
        OverlayImplBase* ov)
{
    OverlayImplBase* newov = ov;
    switch(s)
    {
        case utils::OV_CLOSED:
            newov = handle_xxx_to_closed(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_2D_VIDEO_ON_PANEL>(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            newov = handle_xxx_to_xxx<utils::OV_2D_VIDEO_ON_PANEL_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            newov = handle_xxx_to_3D_2DPanel(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            // no state change
            break;
        case utils::OV_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_UI_MIRROR>(ov);
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_2D_TRUE_UI_MIRROR>(ov);
            break;
        case utils::OV_BYPASS_1_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_1_LAYER>(ov);
            break;
        case utils::OV_BYPASS_2_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_2_LAYER>(ov);
            break;
        case utils::OV_BYPASS_3_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_3_LAYER>(ov);
            break;
        default:
            ALOGE("%s: unknown state=%d", __FUNCTION__, s);
    }
    mState = s;
    return newov;
}

// Transitions from UI mirroring to XXX
inline OverlayImplBase* OverlayState::handle_UI_Mirror(utils::eOverlayState s,
        OverlayImplBase* ov)
{
    OverlayImplBase* newov = ov;
    switch(s)
    {
        case utils::OV_CLOSED:
            newov = handle_xxx_to_closed(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_2D_VIDEO_ON_PANEL>(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            newov = handle_xxx_to_xxx<utils::OV_2D_VIDEO_ON_PANEL_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>(ov);
            break;
        case utils::OV_UI_MIRROR:
            // no state change
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_2D_TRUE_UI_MIRROR>(ov);
            break;
        case utils::OV_BYPASS_1_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_1_LAYER>(ov);
            break;
        case utils::OV_BYPASS_2_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_2_LAYER>(ov);
            break;
        case utils::OV_BYPASS_3_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_3_LAYER>(ov);
            break;
        default:
            ALOGE("%s: unknown state=%d", __FUNCTION__, s);
    }
    mState = s;
    return newov;
}

// Transitions from 2D video true UI mirroring (2D video + UI) to XXX
inline OverlayImplBase* OverlayState::handle_2D_trueUI_Mirror(utils::eOverlayState s,
        OverlayImplBase* ov)
{
    OverlayImplBase* newov = ov;
    switch(s)
    {
        case utils::OV_CLOSED:
            newov = handle_xxx_to_closed(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            newov = handle_xxx_to_2D_2DPanel(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            newov = handle_xxx_to_2D_2DTV(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>(ov);
            break;
        case utils::OV_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_UI_MIRROR>(ov);
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            // no state change
            break;
        case utils::OV_BYPASS_1_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_1_LAYER>(ov);
            break;
        case utils::OV_BYPASS_2_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_2_LAYER>(ov);
            break;
        case utils::OV_BYPASS_3_LAYER:
            newov = handle_xxx_to_xxx<utils::OV_BYPASS_3_LAYER>(ov);
            break;
        default:
            ALOGE("%s: unknown state=%d", __FUNCTION__, s);
    }
    mState = s;
    return newov;
}

// Transitions from composition bypass to XXX
inline OverlayImplBase* OverlayState::handle_bypass(utils::eOverlayState s,
        OverlayImplBase* ov)
{
    OverlayImplBase* newov = ov;
    switch(s)
    {
        case utils::OV_CLOSED:
            newov = handle_xxx_to_closed(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_2D_VIDEO_ON_PANEL>(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            newov = handle_xxx_to_xxx<utils::OV_2D_VIDEO_ON_PANEL_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_3D_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            newov = handle_xxx_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>(ov);
            break;
        case utils::OV_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_UI_MIRROR>(ov);
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            newov = handle_xxx_to_xxx<utils::OV_2D_TRUE_UI_MIRROR>(ov);
            break;
        case utils::OV_BYPASS_1_LAYER:
            newov = handle_xxx_to_bypass1(ov);
            break;
        case utils::OV_BYPASS_2_LAYER:
            newov = handle_xxx_to_bypass2(ov);
            break;
        case utils::OV_BYPASS_3_LAYER:
            newov = handle_xxx_to_bypass3(ov);
            break;
        default:
            ALOGE("%s: unknown state=%d", __FUNCTION__, s);
    }
    mState = s;
    return newov;
}


} // overlay

#endif // OVERLAY_STATE_H
