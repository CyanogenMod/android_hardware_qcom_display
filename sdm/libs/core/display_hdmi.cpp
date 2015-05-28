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

#include "display_hdmi.h"
#include "hw_hdmi_interface.h"
#include "hw_info_interface.h"

#define __CLASS__ "DisplayHDMI"

namespace sdm {

DisplayHDMI::DisplayHDMI(DisplayEventHandler *event_handler, HWInfoInterface *hw_info_intf,
                         BufferSyncHandler *buffer_sync_handler, CompManager *comp_manager,
                         RotatorInterface *rotator_intf)
  : DisplayBase(kHDMI, event_handler, kDeviceHDMI, buffer_sync_handler, comp_manager,
                rotator_intf, hw_info_intf) {
}

DisplayError DisplayHDMI::Init() {
  SCOPE_LOCK(locker_);

  DisplayError error = HWHDMIInterface::Create(&hw_hdmi_intf_, hw_info_intf_,
                                               DisplayBase::buffer_sync_handler_);
  if (error != kErrorNone) {
    return error;
  }

  DisplayBase::hw_intf_ = hw_hdmi_intf_;
  error = hw_hdmi_intf_->Open(NULL);
  if (error != kErrorNone) {
    return error;
  }

  error = DisplayBase::Init();
  if (error != kErrorNone) {
    HWHDMIInterface::Destroy(hw_hdmi_intf_);
  }

  GetScanSupport();
  underscan_supported_ = (scan_support_ == kScanAlwaysUnderscanned) || (scan_support_ == kScanBoth);

  return error;
}

DisplayError DisplayHDMI::Deinit() {
  SCOPE_LOCK(locker_);

  DisplayError error = DisplayBase::Deinit();
  if (error != kErrorNone) {
    return error;
  }
  HWHDMIInterface::Destroy(hw_hdmi_intf_);

  return error;
}

DisplayError DisplayHDMI::Prepare(LayerStack *layer_stack) {
  SCOPE_LOCK(locker_);
  return DisplayBase::Prepare(layer_stack);
}

DisplayError DisplayHDMI::Commit(LayerStack *layer_stack) {
  SCOPE_LOCK(locker_);
  return DisplayBase::Commit(layer_stack);
}

DisplayError DisplayHDMI::Flush() {
  SCOPE_LOCK(locker_);
  return DisplayBase::Flush();
}

DisplayError DisplayHDMI::GetDisplayState(DisplayState *state) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetDisplayState(state);
}

DisplayError DisplayHDMI::GetNumVariableInfoConfigs(uint32_t *count) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetNumVariableInfoConfigs(count);
}

DisplayError DisplayHDMI::GetConfig(DisplayConfigFixedInfo *fixed_info) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetConfig(fixed_info);
}

DisplayError DisplayHDMI::GetConfig(uint32_t index, DisplayConfigVariableInfo *variable_info) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetConfig(index, variable_info);
}

DisplayError DisplayHDMI::GetActiveConfig(uint32_t *index) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetActiveConfig(index);
}

DisplayError DisplayHDMI::GetVSyncState(bool *enabled) {
  SCOPE_LOCK(locker_);
  return DisplayBase::GetVSyncState(enabled);
}

DisplayError DisplayHDMI::SetDisplayState(DisplayState state) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetDisplayState(state);
}

DisplayError DisplayHDMI::SetActiveConfig(DisplayConfigVariableInfo *variable_info) {
  SCOPE_LOCK(locker_);
  return kErrorNotSupported;
}

DisplayError DisplayHDMI::SetActiveConfig(uint32_t index) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetActiveConfig(index);
}

DisplayError DisplayHDMI::SetVSyncState(bool enable) {
  SCOPE_LOCK(locker_);
  return kErrorNotSupported;
}

void DisplayHDMI::SetIdleTimeoutMs(uint32_t timeout_ms) { }

DisplayError DisplayHDMI::SetMaxMixerStages(uint32_t max_mixer_stages) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetMaxMixerStages(max_mixer_stages);
}

DisplayError DisplayHDMI::SetDisplayMode(uint32_t mode) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetDisplayMode(mode);
}

DisplayError DisplayHDMI::IsScalingValid(const LayerRect &crop, const LayerRect &dst,
                                         bool rotate90) {
  SCOPE_LOCK(locker_);
  return DisplayBase::IsScalingValid(crop, dst, rotate90);
}

DisplayError DisplayHDMI::SetRefreshRate(uint32_t refresh_rate) {
  SCOPE_LOCK(locker_);
  return kErrorNotSupported;
}

bool DisplayHDMI::IsUnderscanSupported() {
  SCOPE_LOCK(locker_);
  return DisplayBase::IsUnderscanSupported();
}

int DisplayHDMI::GetBestConfig() {
  uint32_t best_config_mode = 0;
  HWDisplayAttributes *best = &display_attributes_[0];
  if (num_modes_ == 1) {
    return best_config_mode;
  }

  // From the available configs, select the best
  // Ex: 1920x1080@60Hz is better than 1920x1080@30 and 1920x1080@30 is better than 1280x720@60
  for (uint32_t index = 1; index < num_modes_; index++) {
    HWDisplayAttributes *current = &display_attributes_[index];
    // compare the two modes: in the order of Resolution followed by refreshrate
    if (current->y_pixels > best->y_pixels) {
      best_config_mode = index;
    } else if (current->y_pixels == best->y_pixels) {
      if (current->x_pixels > best->x_pixels) {
        best_config_mode = index;
      } else if (current->x_pixels == best->x_pixels) {
        if (current->vsync_period_ns < best->vsync_period_ns) {
          best_config_mode = index;
        }
      }
    }
    if (best_config_mode == index) {
      best = &display_attributes_[index];
    }
  }

  // Used for changing HDMI Resolution - override the best with user set config
  uint32_t user_config = Debug::GetHDMIResolution();
  if (user_config) {
    uint32_t config_index = -1;
    // For the config, get the corresponding index
    DisplayError error = hw_intf_->GetConfigIndex(user_config, &config_index);
    if (error == kErrorNone)
      return config_index;
  }

  return best_config_mode;
}

void DisplayHDMI::GetScanSupport() {
  DisplayError error = kErrorNone;
  uint32_t video_format = -1;
  uint32_t max_cea_format = -1;
  HWScanInfo scan_info = HWScanInfo();
  hw_hdmi_intf_->GetHWScanInfo(&scan_info);

  error = hw_hdmi_intf_->GetVideoFormat(active_mode_index_, &video_format);
  if (error != kErrorNone) {
    return;
  }

  error = hw_hdmi_intf_->GetMaxCEAFormat(&max_cea_format);
  if (error != kErrorNone) {
    return;
  }

  // The scan support for a given HDMI TV must be read from scan info corresponding to
  // Preferred Timing if the preferred timing of the display is currently active, and if it is
  // valid. In all other cases, we must read the scan support from CEA scan info if
  // the resolution is a CEA resolution, or from IT scan info for all other resolutions.
  if (active_mode_index_ == 0 && scan_info.pt_scan_support != kScanNotSupported) {
    scan_support_ = scan_info.pt_scan_support;
  } else if (video_format < max_cea_format) {
    scan_support_ = scan_info.cea_scan_support;
  } else {
    scan_support_ = scan_info.it_scan_support;
  }
}

void DisplayHDMI::AppendDump(char *buffer, uint32_t length) {
  SCOPE_LOCK(locker_);
  DisplayBase::AppendDump(buffer, length);
}

}  // namespace sdm

