/*
* Copyright (c) 2014 - 2016, The Linux Foundation. All rights reserved.
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
#include <utils/rect.h>
#include <map>
#include <algorithm>
#include <functional>
#include <vector>

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
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  DisplayError error = HWPrimary::Create(&hw_intf_, hw_info_intf_,
                                         DisplayBase::buffer_sync_handler_);

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

  error = HWEventsInterface::Create(INT(display_type_), this, &event_list_, &hw_events_intf_);
  if (error != kErrorNone) {
    DLOGE("Failed to create hardware events interface. Error = %d", error);
    DisplayBase::Deinit();
    HWPrimary::Destroy(hw_intf_);
  }

  return error;
}

DisplayError DisplayPrimary::Deinit() {
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  DisplayError error = DisplayBase::Deinit();
  HWPrimary::Destroy(hw_intf_);

  return error;
}

DisplayError DisplayPrimary::Prepare(LayerStack *layer_stack) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  DisplayError error = kErrorNone;
  uint32_t new_mixer_width = 0;
  uint32_t new_mixer_height = 0;
  uint32_t display_width = display_attributes_.x_pixels;
  uint32_t display_height = display_attributes_.y_pixels;

  if (NeedsMixerReconfiguration(layer_stack, &new_mixer_width, &new_mixer_height)) {
    error = ReconfigureMixer(new_mixer_width, new_mixer_height);
    if (error != kErrorNone) {
      ReconfigureMixer(display_width, display_height);
    }
  }

  return DisplayBase::Prepare(layer_stack);
}

DisplayError DisplayPrimary::Commit(LayerStack *layer_stack) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  DisplayError error = kErrorNone;

  // Enabling auto refresh is async and needs to happen before commit ioctl
  if (hw_panel_info_.mode == kModeCommand) {
    hw_intf_->SetAutoRefresh(layer_stack->flags.single_buffered_layer_present);
  }

  bool set_idle_timeout = comp_manager_->CanSetIdleTimeout(display_comp_ctx_);

  error = DisplayBase::Commit(layer_stack);
  if (error != kErrorNone) {
    return error;
  }

  DisplayBase::ReconfigureDisplay();

  if (hw_panel_info_.mode == kModeVideo) {
    if (set_idle_timeout && !layer_stack->flags.single_buffered_layer_present) {
      hw_intf_->SetIdleTimeoutMs(idle_timeout_ms_);
    } else {
      hw_intf_->SetIdleTimeoutMs(0);
    }
  }

  return error;
}

DisplayError DisplayPrimary::SetDisplayState(DisplayState state) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
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

void DisplayPrimary::SetIdleTimeoutMs(uint32_t timeout_ms) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  // Idle fallback feature is supported only for video mode panel.
  if (hw_panel_info_.mode == kModeVideo) {
    hw_intf_->SetIdleTimeoutMs(timeout_ms);
  }
  idle_timeout_ms_ = timeout_ms;
}

DisplayError DisplayPrimary::SetDisplayMode(uint32_t mode) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  DisplayError error = kErrorNone;
  HWDisplayMode hw_display_mode = static_cast<HWDisplayMode>(mode);
  uint32_t pending = 0;

  if (!active_) {
    DLOGW("Invalid display state = %d. Panel must be on.", state_);
    return kErrorNotSupported;
  }

  if (hw_display_mode != kModeCommand && hw_display_mode != kModeVideo) {
    DLOGW("Invalid panel mode parameters. Requested = %d", hw_display_mode);
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

  if (mode == kModeVideo) {
    ControlPartialUpdate(false /* enable */, &pending);
    hw_intf_->SetIdleTimeoutMs(idle_timeout_ms_);
  } else if (mode == kModeCommand) {
    ControlPartialUpdate(true /* enable */, &pending);
    hw_intf_->SetIdleTimeoutMs(0);
  }

  return error;
}

DisplayError DisplayPrimary::SetPanelBrightness(int level) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  return hw_intf_->SetPanelBrightness(level);
}

DisplayError DisplayPrimary::GetRefreshRateRange(uint32_t *min_refresh_rate,
                                                 uint32_t *max_refresh_rate) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
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
  lock_guard<recursive_mutex> obj(recursive_mutex_);

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

  return DisplayBase::ReconfigureDisplay();
}

DisplayError DisplayPrimary::VSync(int64_t timestamp) {
  if (vsync_enable_) {
    DisplayEventVSync vsync;
    vsync.timestamp = timestamp;
    event_handler_->VSync(vsync);
  }

  return kErrorNone;
}

void DisplayPrimary::IdleTimeout() {
  event_handler_->Refresh();
  comp_manager_->ProcessIdleTimeout(display_comp_ctx_);
}

void DisplayPrimary::ThermalEvent(int64_t thermal_level) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  comp_manager_->ProcessThermalEvent(display_comp_ctx_, thermal_level);
}

DisplayError DisplayPrimary::GetPanelBrightness(int *level) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  return hw_intf_->GetPanelBrightness(level);
}

DisplayError DisplayPrimary::ControlPartialUpdate(bool enable, uint32_t *pending) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  if (!pending) {
    return kErrorParameters;
  }

  if (!hw_panel_info_.partial_update) {
    // Nothing to be done.
    DLOGI("partial update is not applicable for display=%d", display_type_);
    return kErrorNotSupported;
  }

  *pending = 0;
  if (enable == partial_update_control_) {
    DLOGI("Same state transition is requested.");
    return kErrorNone;
  }

  partial_update_control_ = enable;

  if (!enable) {
    // If the request is to turn off feature, new draw call is required to have
    // the new setting into effect.
    *pending = 1;
  }

  return kErrorNone;
}

DisplayError DisplayPrimary::DisablePartialUpdateOneFrame() {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  disable_pu_one_frame_ = true;

  return kErrorNone;
}

}  // namespace sdm

