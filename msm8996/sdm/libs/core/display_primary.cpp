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
  SCOPE_LOCK(locker_);

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
  SCOPE_LOCK(locker_);

  DisplayError error = DisplayBase::Deinit();
  HWPrimary::Destroy(hw_intf_);

  return error;
}

DisplayError DisplayPrimary::PrepareLocked(LayerStack *layer_stack) {
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

  return DisplayBase::PrepareLocked(layer_stack);
}

DisplayError DisplayPrimary::CommitLocked(LayerStack *layer_stack) {
  DisplayError error = kErrorNone;

  // Enabling auto refresh is async and needs to happen before commit ioctl
  if (hw_panel_info_.mode == kModeCommand) {
    hw_intf_->SetAutoRefresh(layer_stack->flags.single_buffered_layer_present);
  }

  bool set_idle_timeout = comp_manager_->CanSetIdleTimeout(display_comp_ctx_);

  error = DisplayBase::CommitLocked(layer_stack);
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

DisplayError DisplayPrimary::SetVSyncState(bool enable) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetVSyncState(enable);
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
    ControlPartialUpdateLocked(false /* enable */, &pending);
    hw_intf_->SetIdleTimeoutMs(idle_timeout_ms_);
  } else if (mode == kModeCommand) {
    ControlPartialUpdateLocked(true /* enable */, &pending);
    hw_intf_->SetIdleTimeoutMs(0);
  }

  return error;
}

DisplayError DisplayPrimary::SetPanelBrightness(int level) {
  SCOPE_LOCK(locker_);
  return hw_intf_->SetPanelBrightness(level);
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

  return DisplayBase::ReconfigureDisplay();
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

DisplayError DisplayPrimary::ControlPartialUpdateLocked(bool enable, uint32_t *pending) {
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

DisplayError DisplayPrimary::DisablePartialUpdateOneFrameLocked() {
  disable_pu_one_frame_ = true;

  return kErrorNone;
}

DisplayError DisplayPrimary::SetMixerResolutionLocked(uint32_t width, uint32_t height) {
  return ReconfigureMixer(width, height);
}

DisplayError DisplayPrimary::ReconfigureMixer(uint32_t width, uint32_t height) {
  DisplayError error = kErrorNone;

  HWMixerAttributes mixer_attributes;
  mixer_attributes.width = width;
  mixer_attributes.height = height;

  error = hw_intf_->SetMixerAttributes(mixer_attributes);
  if (error != kErrorNone) {
    return error;
  }

  return DisplayBase::ReconfigureDisplay();
}

bool DisplayPrimary::NeedsMixerReconfiguration(LayerStack *layer_stack, uint32_t *new_mixer_width,
                                               uint32_t *new_mixer_height) {
  uint32_t layer_count = UINT32(layer_stack->layers.size());

  uint32_t fb_width  = fb_config_.x_pixels;
  uint32_t fb_height  = fb_config_.y_pixels;
  uint32_t fb_area = fb_width * fb_height;
  LayerRect fb_rect = (LayerRect) {0.0f, 0.0f, FLOAT(fb_width), FLOAT(fb_height)};
  uint32_t mixer_width = mixer_attributes_.width;
  uint32_t mixer_height = mixer_attributes_.height;

  RectOrientation fb_orientation = GetOrientation(fb_rect);
  uint32_t max_layer_area = 0;
  uint32_t max_area_layer_index = 0;
  std::vector<Layer *> layers = layer_stack->layers;

  for (uint32_t i = 0; i < layer_count; i++) {
    Layer *layer = layers.at(i);
    LayerBuffer *layer_buffer = layer->input_buffer;

    if (!layer_buffer->flags.video) {
      continue;
    }

    uint32_t layer_width = UINT32(layer->src_rect.right - layer->src_rect.left);
    uint32_t layer_height = UINT32(layer->src_rect.bottom - layer->src_rect.top);
    uint32_t layer_area = layer_width * layer_height;

    if (layer_area > max_layer_area) {
      max_layer_area = layer_area;
      max_area_layer_index = i;
    }
  }

  if (max_layer_area > fb_area) {
    Layer *layer = layers.at(max_area_layer_index);

    uint32_t layer_width = UINT32(layer->src_rect.right - layer->src_rect.left);
    uint32_t layer_height = UINT32(layer->src_rect.bottom - layer->src_rect.top);
    LayerRect layer_rect = (LayerRect){0.0f, 0.0f, FLOAT(layer_width), FLOAT(layer_height)};

    RectOrientation layer_orientation = GetOrientation(layer_rect);
    if (layer_orientation != kOrientationUnknown &&
        fb_orientation != kOrientationUnknown) {
      if (layer_orientation != fb_orientation) {
        Swap(layer_width, layer_height);
      }
    }

    // Align the width and height according to fb's aspect ratio
    layer_width = UINT32((FLOAT(fb_width) / FLOAT(fb_height)) * layer_height);

    *new_mixer_width = layer_width;
    *new_mixer_height = layer_height;

    return true;
  } else {
    if (fb_width != mixer_width || fb_height != mixer_height) {
      *new_mixer_width = fb_width;
      *new_mixer_height = fb_height;

      return true;
    }
  }

  return false;
}

DisplayError DisplayPrimary::SetDetailEnhancerDataLocked(const DisplayDetailEnhancerData &de_data) {
  DisplayError error = comp_manager_->SetDetailEnhancerData(display_comp_ctx_, de_data);
  if (error != kErrorNone) {
    return error;
  }

  DisablePartialUpdateOneFrameLocked();

  return kErrorNone;
}

}  // namespace sdm

