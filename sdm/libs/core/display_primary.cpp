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
#include "hw_primary_interface.h"
#include "hw_info_interface.h"

#define __CLASS__ "DisplayPrimary"

namespace sdm {

DisplayPrimary::DisplayPrimary(DisplayEventHandler *event_handler, HWInfoInterface *hw_info_intf,
                               BufferSyncHandler *buffer_sync_handler, CompManager *comp_manager,
                               RotatorInterface *rotator_intf)
  : DisplayBase(kPrimary, event_handler, kDevicePrimary, buffer_sync_handler, comp_manager,
                rotator_intf, hw_info_intf) {
}

DisplayError DisplayPrimary::Init() {
  SCOPE_LOCK(locker_);

  DisplayError error = HWPrimaryInterface::Create(&hw_primary_intf_, hw_info_intf_,
                                                  DisplayBase::buffer_sync_handler_);
  if (error != kErrorNone) {
    return error;
  }
  DisplayBase::hw_intf_ = hw_primary_intf_;

  error = hw_primary_intf_->Open(this);
  if (error != kErrorNone) {
    return error;
  }

  error = DisplayBase::Init();
  if (error != kErrorNone) {
    HWPrimaryInterface::Destroy(hw_primary_intf_);
  }

  // Idle fallback feature is supported only for video mode panel.
  if (hw_panel_info_.mode == kModeVideo) {
    hw_primary_intf_->SetIdleTimeoutMs(Debug::GetIdleTimeoutMs());
  }

  if (hw_panel_info_.mode == kModeCommand && Debug::IsVideoModeEnabled()) {
    error = hw_primary_intf_->SetDisplayMode(kModeVideo);
    if (error != kErrorNone) {
      DLOGW("Retaining current display mode. Current = %d, Requested = %d", hw_panel_info_.mode,
            kModeVideo);
    }
  }

  return error;
}

DisplayError DisplayPrimary::Deinit() {
  SCOPE_LOCK(locker_);

  DisplayError error = DisplayBase::Deinit();
  if (error != kErrorNone) {
    return error;
  }
  HWPrimaryInterface::Destroy(hw_primary_intf_);

  return error;
}

DisplayError DisplayPrimary::Prepare(LayerStack *layer_stack) {
  SCOPE_LOCK(locker_);
  return DisplayBase::Prepare(layer_stack);
}

DisplayError DisplayPrimary::Commit(LayerStack *layer_stack) {
  SCOPE_LOCK(locker_);
  DisplayError error = kErrorNone;
  HWPanelInfo panel_info;
  HWDisplayAttributes display_attributes;

  error = DisplayBase::Commit(layer_stack);
  if (error != kErrorNone) {
    return error;
  }

  hw_primary_intf_->GetHWPanelInfo(&panel_info);

  hw_primary_intf_->GetDisplayAttributes(&display_attributes, active_mode_index_);

  if (panel_info != hw_panel_info_ ||
      display_attributes != display_attributes_[active_mode_index_]) {
    comp_manager_->ReconfigureDisplay(display_comp_ctx_, display_attributes, panel_info);

    hw_panel_info_ = panel_info;
    display_attributes_[active_mode_index_] = display_attributes;
  }

  return error;
}

DisplayError DisplayPrimary::Flush() {
  SCOPE_LOCK(locker_);
  return DisplayBase::Flush();
}

DisplayError DisplayPrimary::GetDisplayState(DisplayState *state) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetDisplayState(state);
}

DisplayError DisplayPrimary::GetNumVariableInfoConfigs(uint32_t *count) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetNumVariableInfoConfigs(count);
}

DisplayError DisplayPrimary::GetConfig(DisplayConfigFixedInfo *fixed_info) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetConfig(fixed_info);
}

DisplayError DisplayPrimary::GetConfig(uint32_t index, DisplayConfigVariableInfo *variable_info) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetConfig(index, variable_info);
}

DisplayError DisplayPrimary::GetActiveConfig(uint32_t *index) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetActiveConfig(index);
}

DisplayError DisplayPrimary::GetVSyncState(bool *enabled) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetVSyncState(enabled);
}

bool DisplayPrimary::IsUnderscanSupported() {
  SCOPE_LOCK(locker_);
  return DisplayBase::IsUnderscanSupported();
}

DisplayError DisplayPrimary::SetDisplayState(DisplayState state) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetDisplayState(state);
}

DisplayError DisplayPrimary::SetActiveConfig(DisplayConfigVariableInfo *variable_info) {
  SCOPE_LOCK(locker_);
  return kErrorNotSupported;
}

DisplayError DisplayPrimary::SetActiveConfig(uint32_t index) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetActiveConfig(index);
}

DisplayError DisplayPrimary::SetVSyncState(bool enable) {
  SCOPE_LOCK(locker_);
  DisplayError error = kErrorNone;
  if (vsync_enable_ != enable) {
    error = hw_primary_intf_->SetVSyncState(enable);
    if (error == kErrorNone) {
      vsync_enable_ = enable;
    }
  }

  return error;
}

void DisplayPrimary::SetIdleTimeoutMs(uint32_t timeout_ms) {
  SCOPE_LOCK(locker_);
  // Idle fallback feature is supported only for video mode panel.
  if (hw_panel_info_.mode == kModeVideo) {
    hw_primary_intf_->SetIdleTimeoutMs(timeout_ms);
  }
}

DisplayError DisplayPrimary::SetMaxMixerStages(uint32_t max_mixer_stages) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetMaxMixerStages(max_mixer_stages);
}

DisplayError DisplayPrimary::SetDisplayMode(uint32_t mode) {
  SCOPE_LOCK(locker_);
  DisplayError error = kErrorNone;
  HWDisplayMode hw_display_mode = kModeDefault;

  if (state_ != kStateOn) {
    DLOGW("Invalid display state = %d. Panel must be on.", state_);
    return kErrorNotSupported;
  }

  switch (mode) {
  case kModeVideo:
    hw_display_mode = kModeVideo;
    break;
  case kModeCommand:
    hw_display_mode = kModeCommand;
    break;
  default:
    DLOGW("Invalid panel mode parameters. Requested = %d", mode);
    return kErrorParameters;
  }

  if (hw_display_mode == hw_panel_info_.mode) {
    DLOGW("Same display mode requested. Current = %d, Requested = %d", hw_panel_info_.mode,
          hw_display_mode);
    return kErrorNone;
  }

  error = hw_primary_intf_->SetDisplayMode(hw_display_mode);
  if (error != kErrorNone) {
    DLOGW("Retaining current display mode. Current = %d, Requested = %d", hw_panel_info_.mode,
          hw_display_mode);
    return error;
  }

  return error;
}

DisplayError DisplayPrimary::IsScalingValid(const LayerRect &crop, const LayerRect &dst,
                                            bool rotate90) {
  SCOPE_LOCK(locker_);
  return DisplayBase::IsScalingValid(crop, dst, rotate90);
}

DisplayError DisplayPrimary::SetRefreshRate(uint32_t refresh_rate) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  if (!hw_panel_info_.dynamic_fps) {
    DLOGW("Dynamic fps feature is not supported");
    return kErrorNotSupported;
  }

  if (refresh_rate > hw_panel_info_.max_fps) {
    refresh_rate = hw_panel_info_.max_fps;
  } else if (refresh_rate < hw_panel_info_.min_fps) {
    refresh_rate = hw_panel_info_.min_fps;
  }

  return hw_primary_intf_->SetRefreshRate(refresh_rate);
}

void DisplayPrimary::AppendDump(char *buffer, uint32_t length) {
  SCOPE_LOCK(locker_);
  DisplayBase::AppendDump(buffer, length);
}

DisplayError DisplayPrimary::VSync(int64_t timestamp) {
  if (vsync_enable_) {
    DisplayEventVSync vsync;
    vsync.timestamp = timestamp;
    event_handler_->VSync(vsync);
  }

  return kErrorNone;
}

DisplayError DisplayPrimary::Blank(bool blank) {
  SCOPE_LOCK(locker_);
  return kErrorNone;
}

void DisplayPrimary::IdleTimeout() {
  SCOPE_LOCK(locker_);
  bool need_refresh = comp_manager_->ProcessIdleTimeout(display_comp_ctx_);
  if (need_refresh) {
    event_handler_->Refresh();
  }
}

void DisplayPrimary::ThermalEvent(int64_t thermal_level) {
  SCOPE_LOCK(locker_);
  comp_manager_->ProcessThermalEvent(display_comp_ctx_, thermal_level);
}

}  // namespace sdm

