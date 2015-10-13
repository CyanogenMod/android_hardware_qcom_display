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
#include "hw_interface.h"
#include "hw_info_interface.h"
#include "fb/hw_primary.h"

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

  DisplayError error = HWPrimary::Create(&hw_intf_, hw_info_intf_,
                                         DisplayBase::buffer_sync_handler_, this);

  if (error != kErrorNone) {
    return error;
  }

  error = DisplayBase::Init();
  if (error != kErrorNone) {
    HWPrimary::Destroy(hw_intf_);
    return error;
  }

  idle_timeout_ms_ = Debug::GetIdleTimeoutMs();

  if (hw_panel_info_.mode == kModeCommand && Debug::IsVideoModeEnabled()) {
    error = hw_intf_->SetDisplayMode(kModeVideo);
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
  HWPrimary::Destroy(hw_intf_);

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
  uint32_t active_index = 0;

  // Enabling auto refresh is async and needs to happen before commit ioctl
  if (hw_panel_info_.mode == kModeCommand) {
    hw_intf_->SetAutoRefresh(layer_stack->flags.single_buffered_layer_present);
  }

  bool set_idle_timeout = comp_manager_->CanSetIdleTimeout(display_comp_ctx_);

  error = DisplayBase::Commit(layer_stack);
  if (error != kErrorNone) {
    return error;
  }

  hw_intf_->GetHWPanelInfo(&panel_info);
  hw_intf_->GetActiveConfig(&active_index);
  hw_intf_->GetDisplayAttributes(active_index, &display_attributes);

  if (panel_info != hw_panel_info_) {
    error = comp_manager_->ReconfigureDisplay(display_comp_ctx_, display_attributes, panel_info);
    hw_panel_info_ = panel_info;
  }

  if (hw_panel_info_.mode == kModeVideo) {
    if (set_idle_timeout && !layer_stack->flags.single_buffered_layer_present) {
      hw_intf_->SetIdleTimeoutMs(idle_timeout_ms_);
    } else {
      hw_intf_->SetIdleTimeoutMs(0);
    }
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
  DisplayError error = kErrorNone;
  error = DisplayBase::SetDisplayState(state);
  if (error != kErrorNone) {
    return error;
  }

  // Set vsync enable state to false, as driver disables vsync during display power off.
  if (state == kStateOff) {
    vsync_enable_ = false;
  }

  return kErrorNone;
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
    error = hw_intf_->SetVSyncState(enable);
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
    hw_intf_->SetIdleTimeoutMs(timeout_ms);
  }
  idle_timeout_ms_ = timeout_ms;
}

DisplayError DisplayPrimary::SetMaxMixerStages(uint32_t max_mixer_stages) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetMaxMixerStages(max_mixer_stages);
}

DisplayError DisplayPrimary::SetDisplayMode(uint32_t mode) {
  SCOPE_LOCK(locker_);
  DisplayError error = kErrorNone;
  HWDisplayMode hw_display_mode = kModeDefault;

  if (!active_) {
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

  error = hw_intf_->SetDisplayMode(hw_display_mode);
  if (error != kErrorNone) {
    DLOGW("Retaining current display mode. Current = %d, Requested = %d", hw_panel_info_.mode,
          hw_display_mode);
    return error;
  }

  // Disable PU if the previous PU state is on when switching to video mode, and re-enable PU when
  // switching back to command mode.
  bool toggle_partial_update = !(hw_display_mode == kModeVideo);
  if (partial_update_control_) {
    comp_manager_->ControlPartialUpdate(display_comp_ctx_, toggle_partial_update);
  }

  if (hw_display_mode == kModeVideo) {
    hw_intf_->SetIdleTimeoutMs(idle_timeout_ms_);
  } else if (hw_display_mode == kModeCommand) {
    hw_intf_->SetIdleTimeoutMs(0);
  }

  return error;
}

DisplayError DisplayPrimary::SetPanelBrightness(int level) {
  SCOPE_LOCK(locker_);
  return hw_intf_->SetPanelBrightness(level);
}

DisplayError DisplayPrimary::IsScalingValid(const LayerRect &crop, const LayerRect &dst,
                                            bool rotate90) {
  SCOPE_LOCK(locker_);
  return DisplayBase::IsScalingValid(crop, dst, rotate90);
}

DisplayError DisplayPrimary::GetRefreshRateRange(uint32_t *min_refresh_rate,
                                                 uint32_t *max_refresh_rate) {
  SCOPE_LOCK(locker_);
  DisplayError error = kErrorNone;

  if (hw_panel_info_.min_fps && hw_panel_info_.max_fps) {
    *min_refresh_rate = hw_panel_info_.min_fps;
    *max_refresh_rate = hw_panel_info_.max_fps;
  } else {
    error = DisplayBase::GetRefreshRateRange(min_refresh_rate, max_refresh_rate);
  }

  return error;
}

DisplayError DisplayPrimary::SetRefreshRate(uint32_t refresh_rate) {
  SCOPE_LOCK(locker_);

  if (!active_ || !hw_panel_info_.dynamic_fps) {
    return kErrorNotSupported;
  }

  if (refresh_rate < hw_panel_info_.min_fps || refresh_rate > hw_panel_info_.max_fps) {
    DLOGE("Invalid Fps = %d request", refresh_rate);
    return kErrorParameters;
  }

  DisplayError error = hw_intf_->SetRefreshRate(refresh_rate);
  if (error != kErrorNone) {
    return error;
  }

  HWDisplayAttributes display_attributes;
  uint32_t active_index = 0;
  error = hw_intf_->GetActiveConfig(&active_index);
  if (error != kErrorNone) {
    return error;
  }

  error = hw_intf_->GetDisplayAttributes(active_index, &display_attributes);
  if (error != kErrorNone) {
    return error;
  }

  comp_manager_->ReconfigureDisplay(display_comp_ctx_, display_attributes, hw_panel_info_);

  return kErrorNone;
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

DisplayError DisplayPrimary::SetCursorPosition(int x, int y) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetCursorPosition(x, y);
}

DisplayError DisplayPrimary::Blank(bool blank) {
  SCOPE_LOCK(locker_);
  return kErrorNone;
}

void DisplayPrimary::IdleTimeout() {
  event_handler_->Refresh();
  comp_manager_->ProcessIdleTimeout(display_comp_ctx_);
}

void DisplayPrimary::ThermalEvent(int64_t thermal_level) {
  SCOPE_LOCK(locker_);
  comp_manager_->ProcessThermalEvent(display_comp_ctx_, thermal_level);
}

DisplayError DisplayPrimary::GetPanelBrightness(int *level) {
  SCOPE_LOCK(locker_);
  return hw_intf_->GetPanelBrightness(level);
}

}  // namespace sdm

