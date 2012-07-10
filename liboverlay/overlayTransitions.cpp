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

#include "overlayState.h"

namespace overlay {

/*
 * Transition from any state to 2D video on 2D panel
 */
OverlayImplBase* OverlayState::handle_xxx_to_2D_2DPanel(
        OverlayImplBase* ov)
{
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGE("%s", __FUNCTION__);

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_2D_VIDEO_ON_PANEL> NewState;
    OverlayImplBase* newov = new NewState::ovimpl();

    //===========================================================
    // For each pipe:
    //    - If pipe matches, copy from previous into new ovimpl
    //    - Otherwise init for new and delete from previous ovimpl
    //===========================================================

    // pipe0/rot0 (GenericPipe)
    if (ov->getOvPipeType(utils::OV_PIPE0) == utils::OV_PIPE_TYPE_GENERIC) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe0 (GenericPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE0);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe0 (GenericPipe)", __FUNCTION__);
        ov->closePipe(utils::OV_PIPE0);
        RotatorBase* rot0 = new NewState::rot0;
        newov->initPipe(rot0, utils::OV_PIPE0);
    }

    // pipe1/rot1 (NullPipe)
    if (ov->getOvPipeType(utils::OV_PIPE1) == utils::OV_PIPE_TYPE_NULL) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe1 (NullPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE1);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe1 (NullPipe)", __FUNCTION__);
        ov->closePipe(utils::OV_PIPE1);
        RotatorBase* rot1 = new NewState::rot1;
        newov->initPipe(rot1, utils::OV_PIPE1);
    }

    // pipe2/rot2 (NullPipe)
    if (ov->getOvPipeType(utils::OV_PIPE2) == utils::OV_PIPE_TYPE_NULL) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe2 (NullPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE2);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe2 (NullPipe)", __FUNCTION__);
        ov->closePipe(utils::OV_PIPE2);
        RotatorBase* rot2 = new NewState::rot2;
        newov->initPipe(rot2, utils::OV_PIPE2);
    }

    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;

    return newov;
}

/*
 * Transition from any state to 2D video on 2D panel and 2D TV
 */
OverlayImplBase* OverlayState::handle_xxx_to_2D_2DTV(
        OverlayImplBase* ov)
{
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGE("%s", __FUNCTION__);

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_2D_VIDEO_ON_PANEL_TV> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //===========================================================
    // For each pipe:
    //    - If pipe matches, copy from previous into new ovimpl
    //    - Otherwise init for new and delete from previous ovimpl
    //===========================================================

    // pipe0/rot0 (GenericPipe)
    if (ov->getOvPipeType(utils::OV_PIPE0) == utils::OV_PIPE_TYPE_GENERIC) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe0 (GenericPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE0);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe0 (GenericPipe)", __FUNCTION__);
        RotatorBase* rot0 = new NewState::rot0;
        ov->closePipe(utils::OV_PIPE0);
        newov->initPipe(rot0, utils::OV_PIPE0);
    }

    // pipe1/rot1 (VideoExtPipe)
    if (ov->getOvPipeType(utils::OV_PIPE1) == utils::OV_PIPE_TYPE_VIDEO_EXT) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe1 (VideoExtPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE1);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe1 (VideoExtPipe)", __FUNCTION__);
        RotatorBase* rot1 = new NewState::rot1;
        ov->closePipe(utils::OV_PIPE1);
        newov->initPipe(rot1, utils::OV_PIPE1);
    }

    // pipe2/rot2 (NullPipe)
    if (ov->getOvPipeType(utils::OV_PIPE2) == utils::OV_PIPE_TYPE_NULL) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe2 (NullPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE2);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe2 (NullPipe)", __FUNCTION__);
        RotatorBase* rot2 = new NewState::rot2;
        ov->closePipe(utils::OV_PIPE2);
        newov->initPipe(rot2, utils::OV_PIPE2);
    }

    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;

    return newov;
}

/*
 * Transition from any state to 3D video on 2D panel
 */
OverlayImplBase* OverlayState::handle_xxx_to_3D_2DPanel(
        OverlayImplBase* ov)
{
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGE("%s", __FUNCTION__);

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_3D_VIDEO_ON_2D_PANEL> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //=================================================================
    // For each pipe:
    //    - If pipe matches, copy from previous into new ovimpl.
    //      (which also makes previous pipe ref 0, so nobody can use)
    //    - Otherwise init pipe for new ovimpl and delete from previous
    //=================================================================

    // pipe0/rot0 (M3DPrimaryPipe)
    if (ov->getOvPipeType(utils::OV_PIPE0) == utils::OV_PIPE_TYPE_M3D_PRIMARY) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe0 (M3DPrimaryPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE0);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe0 (M3DPrimaryPipe)", __FUNCTION__);
        RotatorBase* rot0 = new NewState::rot0;
        ov->closePipe(utils::OV_PIPE0);
        newov->initPipe(rot0, utils::OV_PIPE0);
    }

    // pipe1/rot1 (NullPipe)
    if (ov->getOvPipeType(utils::OV_PIPE1) == utils::OV_PIPE_TYPE_NULL) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe1 (NullPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE1);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe1 (NullPipe)", __FUNCTION__);
        RotatorBase* rot1 = new NewState::rot1;
        ov->closePipe(utils::OV_PIPE1);
        newov->initPipe(rot1, utils::OV_PIPE1);
    }

    // pipe2/rot2 (NullPipe)
    if (ov->getOvPipeType(utils::OV_PIPE2) == utils::OV_PIPE_TYPE_NULL) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe2 (NullPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE2);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe2 (NullPipe)", __FUNCTION__);
        RotatorBase* rot2 = new NewState::rot2;
        ov->closePipe(utils::OV_PIPE2);
        newov->initPipe(rot2, utils::OV_PIPE2);
    }

    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;

    return newov;
}

/*
 * Transition from any state to 3D video on 2D panel and 2D TV
 */
OverlayImplBase* OverlayState::handle_xxx_to_3D_2DTV(
        OverlayImplBase* ov)
{
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGE("%s", __FUNCTION__);

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //===========================================================
    // For each pipe:
    //    - If pipe matches, copy from previous into new ovimpl
    //    - Otherwise init for new and delete from previous ovimpl
    //===========================================================

    // pipe0/rot0 (M3DPrimaryPipe)
    if (ov->getOvPipeType(utils::OV_PIPE0) == utils::OV_PIPE_TYPE_M3D_PRIMARY) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe0 (M3DPrimaryPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE0);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe0 (M3DPrimaryPipe)", __FUNCTION__);
        RotatorBase* rot0 = new NewState::rot0;
        ov->closePipe(utils::OV_PIPE0);
        newov->initPipe(rot0, utils::OV_PIPE0);
    }

    // pipe1/rot1 (M3DExtPipe)
    if (ov->getOvPipeType(utils::OV_PIPE1) == utils::OV_PIPE_TYPE_M3D_EXTERNAL) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe1 (M3DExtPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE1);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe1 (M3DExtPipe)", __FUNCTION__);
        RotatorBase* rot1 = new NewState::rot1;
        ov->closePipe(utils::OV_PIPE1);
        newov->initPipe(rot1, utils::OV_PIPE1);
    }

    // pipe2/rot2 (NullPipe)
    if (ov->getOvPipeType(utils::OV_PIPE2) == utils::OV_PIPE_TYPE_NULL) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe2 (NullPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE2);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe2 (NullPipe)", __FUNCTION__);
        RotatorBase* rot2 = new NewState::rot2;
        ov->closePipe(utils::OV_PIPE2);
        newov->initPipe(rot2, utils::OV_PIPE2);
    }

    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;

    return newov;
}

/*
 * Transition from any state to 2D true UI mirroring (2D video + UI)
 */
OverlayImplBase* OverlayState::handle_xxx_to_2D_trueUI_Mirror(
        OverlayImplBase* ov)
{
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGE("%s", __FUNCTION__);

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_2D_TRUE_UI_MIRROR> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //===========================================================
    // For each pipe:
    //    - If pipe matches, copy from previous into new ovimpl
    //    - Otherwise init for new and delete from previous ovimpl
    //===========================================================

    // pipe0/rot0 (GenericPipe)
    if (ov->getOvPipeType(utils::OV_PIPE0) == utils::OV_PIPE_TYPE_GENERIC) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe0 (GenericPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE0);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe0 (GenericPipe)", __FUNCTION__);
        RotatorBase* rot0 = new NewState::rot0;
        ov->closePipe(utils::OV_PIPE0);
        newov->initPipe(rot0, utils::OV_PIPE0);
    }

    // pipe1/rot1 (VideoExtPipe)
    if (ov->getOvPipeType(utils::OV_PIPE1) == utils::OV_PIPE_TYPE_VIDEO_EXT) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe1 (VideoExtPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE1);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe1 (VideoExtPipe)", __FUNCTION__);
        RotatorBase* rot1 = new NewState::rot1;
        ov->closePipe(utils::OV_PIPE1);
        newov->initPipe(rot1, utils::OV_PIPE1);
    }

    // pipe2/rot2 (UIMirrorPipe)
    if (ov->getOvPipeType(utils::OV_PIPE2) == utils::OV_PIPE_TYPE_UI_MIRROR) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe2 (UIMirrorPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE2);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe2 (UIMirrorPipe)", __FUNCTION__);
        RotatorBase* rot2 = new NewState::rot2;
        ov->closePipe(utils::OV_PIPE2);
        newov->initPipe(rot2, utils::OV_PIPE2);
    }

    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;

    return newov;
}

/*
 * Transitions from any state to 1 layer composition bypass
 */
OverlayImplBase* OverlayState::handle_xxx_to_bypass1(OverlayImplBase* ov)
{
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGE("%s", __FUNCTION__);

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_BYPASS_1_LAYER> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //===========================================================
    // For each pipe:
    //    - If pipe matches, copy from previous into new ovimpl
    //    - Otherwise init for new and delete from previous ovimpl
    //===========================================================

    // pipe0/rot0 (BypassPipe)
    if (ov->getOvPipeType(utils::OV_PIPE0) == utils::OV_PIPE_TYPE_BYPASS) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe0 (BypassPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE0);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe0 (BypassPipe)", __FUNCTION__);
        RotatorBase* rot0 = new NewState::rot0;
        ov->closePipe(utils::OV_PIPE0);
        newov->initPipe(rot0, utils::OV_PIPE0);
    }

    // pipe1/rot1 (NullPipe)
    if (ov->getOvPipeType(utils::OV_PIPE1) == utils::OV_PIPE_TYPE_NULL) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe1 (NullPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE1);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe1 (NullPipe)", __FUNCTION__);
        RotatorBase* rot1 = new NewState::rot1;
        ov->closePipe(utils::OV_PIPE1);
        newov->initPipe(rot1, utils::OV_PIPE1);
    }

    // pipe2/rot2 (NullPipe)
    if (ov->getOvPipeType(utils::OV_PIPE2) == utils::OV_PIPE_TYPE_NULL) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe2 (NullPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE2);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe2 (NullPipe)", __FUNCTION__);
        RotatorBase* rot2 = new NewState::rot2;
        ov->closePipe(utils::OV_PIPE2);
        newov->initPipe(rot2, utils::OV_PIPE2);
    }

    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;

    return newov;
}

/*
 * Transitions from any state to 2 layers composition bypass
 */
OverlayImplBase* OverlayState::handle_xxx_to_bypass2(OverlayImplBase* ov)
{
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGE("%s", __FUNCTION__);

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_BYPASS_2_LAYER> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //===========================================================
    // For each pipe:
    //    - If pipe matches, copy from previous into new ovimpl
    //    - Otherwise init for new and delete from previous ovimpl
    //===========================================================

    // pipe0/rot0 (BypassPipe)
    if (ov->getOvPipeType(utils::OV_PIPE0) == utils::OV_PIPE_TYPE_BYPASS) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe0 (BypassPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE0);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe0 (BypassPipe)", __FUNCTION__);
        RotatorBase* rot0 = new NewState::rot0;
        ov->closePipe(utils::OV_PIPE0);
        newov->initPipe(rot0, utils::OV_PIPE0);
    }

    // pipe1/rot1 (BypassPipe)
    if (ov->getOvPipeType(utils::OV_PIPE1) == utils::OV_PIPE_TYPE_BYPASS) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe1 (BypassPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE1);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe1 (BypassPipe)", __FUNCTION__);
        RotatorBase* rot1 = new NewState::rot1;
        ov->closePipe(utils::OV_PIPE1);
        newov->initPipe(rot1, utils::OV_PIPE1);
    }

    // pipe2/rot2 (NullPipe)
    if (ov->getOvPipeType(utils::OV_PIPE2) == utils::OV_PIPE_TYPE_NULL) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe2 (NullPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE2);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe2 (NullPipe)", __FUNCTION__);
        RotatorBase* rot2 = new NewState::rot2;
        ov->closePipe(utils::OV_PIPE2);
        newov->initPipe(rot2, utils::OV_PIPE2);
    }

    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;

    return newov;
}

/*
 * Transitions from any state to 3 layers composition bypass
 */
OverlayImplBase* OverlayState::handle_xxx_to_bypass3(OverlayImplBase* ov)
{
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGE("%s", __FUNCTION__);

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_BYPASS_3_LAYER> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //===========================================================
    // For each pipe:
    //    - If pipe matches, copy from previous into new ovimpl
    //    - Otherwise init for new and delete from previous ovimpl
    //===========================================================

    // pipe0/rot0 (BypassPipe)
    if (ov->getOvPipeType(utils::OV_PIPE0) == utils::OV_PIPE_TYPE_BYPASS) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe0 (BypassPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE0);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe0 (BypassPipe)", __FUNCTION__);
        RotatorBase* rot0 = new NewState::rot0;
        ov->closePipe(utils::OV_PIPE0);
        newov->initPipe(rot0, utils::OV_PIPE0);
    }

    // pipe1/rot1 (BypassPipe)
    if (ov->getOvPipeType(utils::OV_PIPE1) == utils::OV_PIPE_TYPE_BYPASS) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe1 (BypassPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE1);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe1 (BypassPipe)", __FUNCTION__);
        RotatorBase* rot1 = new NewState::rot1;
        ov->closePipe(utils::OV_PIPE1);
        newov->initPipe(rot1, utils::OV_PIPE1);
    }

    // pipe2/rot2 (BypassPipe)
    if (ov->getOvPipeType(utils::OV_PIPE2) == utils::OV_PIPE_TYPE_BYPASS) {
        ALOGE_IF(DEBUG_OVERLAY, "%s: Copy pipe2 (BypassPipe)", __FUNCTION__);
        newov->copyOvPipe(ov, utils::OV_PIPE2);
    } else {
        ALOGE_IF(DEBUG_OVERLAY, "%s: init pipe2 (BypassPipe)", __FUNCTION__);
        RotatorBase* rot2 = new NewState::rot2;
        ov->closePipe(utils::OV_PIPE2);
        newov->initPipe(rot2, utils::OV_PIPE2);
    }

    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;

    return newov;
}

} // overlay
