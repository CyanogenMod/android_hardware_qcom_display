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
#include "hw_virtual_interface.h"
#include "hw_info_interface.h"

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

  DisplayError error = HWVirtualInterface::Create(&hw_virtual_intf_, hw_info_intf_,
                                                  DisplayBase::buffer_sync_handler_);
  if (error != kErrorNone) {
    return error;
  }

  DisplayBase::hw_intf_ = hw_virtual_intf_;
  error = hw_virtual_intf_->Open(NULL);
  if (error != kErrorNone) {
    return error;
  }

  error = DisplayBase::Init();
  if (error != kErrorNone) {
    HWVirtualInterface::Destroy(hw_virtual_intf_);
  }

  return error;
}

DisplayError DisplayVirtual::Deinit() {
  SCOPE_LOCK(locker_);

  DisplayError error = DisplayBase::Deinit();
  if (error != kErrorNone) {
    return error;
  }
  HWVirtualInterface::Destroy(hw_virtual_intf_);

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
  return DisplayBase::GetNumVariableInfoConfigs(count);
}

DisplayError DisplayVirtual::GetConfig(DisplayConfigFixedInfo *fixed_info) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetConfig(fixed_info);
}

DisplayError DisplayVirtual::GetConfig(uint32_t index, DisplayConfigVariableInfo *variable_info) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetConfig(index, variable_info);
}

DisplayError DisplayVirtual::GetActiveConfig(uint32_t *index) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetActiveConfig(index);
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

  HWDisplayAttributes display_attributes = display_attributes_[active_mode_index_];

  display_attributes.x_pixels = variable_info->x_pixels;
  display_attributes.y_pixels = variable_info->y_pixels;
  display_attributes.fps = variable_info->fps;

  // if display is already connected, unregister display from composition manager and register
  // the display with new configuration.
  if (display_comp_ctx_) {
    comp_manager_->UnregisterDisplay(display_comp_ctx_);
  }

  error = comp_manager_->RegisterDisplay(display_type_, display_attributes, hw_panel_info_,
                                         &display_comp_ctx_);
  if (error != kErrorNone) {
    return error;
  }

  display_attributes_[active_mode_index_] = display_attributes;

  return error;
}

DisplayError DisplayVirtual::SetActiveConfig(uint32_t index) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetActiveConfig(index);
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

DisplayError DisplayVirtual::SetRefreshRate(uint32_t refresh_rate) {
  SCOPE_LOCK(locker_);
  return kErrorNotSupported;
}

void DisplayVirtual::AppendDump(char *buffer, uint32_t length) {
  SCOPE_LOCK(locker_);
  DisplayBase::AppendDump(buffer, length);
}

}  // namespace sdm

