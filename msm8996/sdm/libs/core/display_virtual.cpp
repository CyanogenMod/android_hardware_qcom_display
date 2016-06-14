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

#include "display_virtual.h"
#include "hw_interface.h"
#include "hw_info_interface.h"
#include "fb/hw_virtual.h"

#define __CLASS__ "DisplayVirtual"

namespace sdm {

DisplayVirtual::DisplayVirtual(DisplayEventHandler *event_handler, HWInfoInterface *hw_info_intf,
                               BufferSyncHandler *buffer_sync_handler, CompManager *comp_manager,
                               RotatorInterface *rotator_intf)
  : DisplayBase(kVirtual, event_handler, kDeviceVirtual, buffer_sync_handler, comp_manager,
                rotator_intf, hw_info_intf) {
}

DisplayError DisplayVirtual::Init() {
  SCOPE_LOCK(locker_);

  DisplayError error = HWVirtual::Create(&hw_intf_, hw_info_intf_,
                                         DisplayBase::buffer_sync_handler_);
  if (error != kErrorNone) {
    return error;
  }

  hw_intf_->GetDisplayAttributes(0 /* active_index */, &display_attributes_);

  error = DisplayBase::Init();
  if (error != kErrorNone) {
    HWVirtual::Destroy(hw_intf_);
  }

  return error;
}

DisplayError DisplayVirtual::Deinit() {
  SCOPE_LOCK(locker_);

  DisplayError error = DisplayBase::Deinit();
  HWVirtual::Destroy(hw_intf_);

  return error;
}

DisplayError DisplayVirtual::Prepare(LayerStack *layer_stack) {
  SCOPE_LOCK(locker_);
  return DisplayBase::Prepare(layer_stack);
}

DisplayError DisplayVirtual::Commit(LayerStack *layer_stack) {
  SCOPE_LOCK(locker_);
  return DisplayBase::Commit(layer_stack);
}

DisplayError DisplayVirtual::Flush() {
  SCOPE_LOCK(locker_);
  return DisplayBase::Flush();
}

DisplayError DisplayVirtual::GetDisplayState(DisplayState *state) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetDisplayState(state);
}

DisplayError DisplayVirtual::GetNumVariableInfoConfigs(uint32_t *count) {
  SCOPE_LOCK(locker_);
  *count = 1;
  return kErrorNone;
}

DisplayError DisplayVirtual::GetConfig(uint32_t index, DisplayConfigVariableInfo *variable_info) {
  SCOPE_LOCK(locker_);
  *variable_info = display_attributes_;
  return kErrorNone;
}

DisplayError DisplayVirtual::GetActiveConfig(uint32_t *index) {
  SCOPE_LOCK(locker_);
  *index = 0;
  return kErrorNone;
}

DisplayError DisplayVirtual::GetVSyncState(bool *enabled) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetVSyncState(enabled);
}

bool DisplayVirtual::IsUnderscanSupported() {
  SCOPE_LOCK(locker_);
  return DisplayBase::IsUnderscanSupported();
}

DisplayError DisplayVirtual::SetDisplayState(DisplayState state) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetDisplayState(state);
}

DisplayError DisplayVirtual::SetActiveConfig(DisplayConfigVariableInfo *variable_info) {
  SCOPE_LOCK(locker_);
  DisplayError error = kErrorNone;

  if (!variable_info) {
    return kErrorParameters;
  }

  display_attributes_.x_pixels = variable_info->x_pixels;
  display_attributes_.y_pixels = variable_info->y_pixels;
  display_attributes_.fps = variable_info->fps;

  // if display is already connected, unregister display from composition manager and register
  // the display with new configuration.
  if (display_comp_ctx_) {
    comp_manager_->UnregisterDisplay(display_comp_ctx_);
  }

  error = comp_manager_->RegisterDisplay(display_type_, display_attributes_, hw_panel_info_,
                                         &display_comp_ctx_);
  if (error != kErrorNone) {
    return error;
  }

  return error;
}

DisplayError DisplayVirtual::SetActiveConfig(uint32_t index) {
  SCOPE_LOCK(locker_);
  return kErrorNotSupported;
}

DisplayError DisplayVirtual::SetVSyncState(bool enable) {
  SCOPE_LOCK(locker_);
  return kErrorNotSupported;
}

void DisplayVirtual::SetIdleTimeoutMs(uint32_t timeout_ms) { }

DisplayError DisplayVirtual::SetMaxMixerStages(uint32_t max_mixer_stages) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetMaxMixerStages(max_mixer_stages);
}

DisplayError DisplayVirtual::SetDisplayMode(uint32_t mode) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetDisplayMode(mode);
}

DisplayError DisplayVirtual::IsScalingValid(const LayerRect &crop, const LayerRect &dst,
                                            bool rotate90) {
  SCOPE_LOCK(locker_);
  return DisplayBase::IsScalingValid(crop, dst, rotate90);
}

DisplayError DisplayVirtual::GetRefreshRateRange(uint32_t *min_refresh_rate,
                                                 uint32_t *max_refresh_rate) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetRefreshRateRange(min_refresh_rate, max_refresh_rate);
}

DisplayError DisplayVirtual::SetRefreshRate(uint32_t refresh_rate) {
  SCOPE_LOCK(locker_);
  return kErrorNotSupported;
}

DisplayError DisplayVirtual::SetPanelBrightness(int level) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetPanelBrightness(level);
}

void DisplayVirtual::AppendDump(char *buffer, uint32_t length) {
  SCOPE_LOCK(locker_);
  DisplayBase::AppendDump(buffer, length);
}

DisplayError DisplayVirtual::SetCursorPosition(int x, int y) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetCursorPosition(x, y);
}

}  // namespace sdm

