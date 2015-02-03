/*
* Copyright (c) 2014 - 2015, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <utils/constants.h>
#include <utils/debug.h>

#include "display_primary.h"

#define __CLASS__ "DisplayPrimary"

namespace sde {

DisplayPrimary::DisplayPrimary(DisplayEventHandler *event_handler, HWInterface *hw_intf,
                               CompManager *comp_manager, OfflineCtrl *offline_ctrl)
  : DisplayBase(kPrimary, event_handler, kDevicePrimary, hw_intf, comp_manager, offline_ctrl) { }

DisplayError DisplayPrimary::SetDisplayState(DisplayState state) {
  DisplayError error = kErrorNone;

  DLOGI("Set state = %d", state);

  if (state == kStateOff) {
    SCOPE_LOCK(locker_);
    if (state == state_) {
      DLOGI("Same state transition is requested.");
      return kErrorNone;
    }
    error = hw_intf_->PowerOff(hw_device_);
    if (error == kErrorNone) {
      comp_manager_->Purge(display_comp_ctx_);
      state_ = state;
      hw_layers_.info.count = 0;
    }
  } else {
    error = DisplayBase::SetDisplayState(state);
  }

  return error;
}

}  // namespace sde

