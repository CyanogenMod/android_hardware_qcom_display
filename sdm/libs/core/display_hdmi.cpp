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
#include <map>
#include <utility>

#include "display_hdmi.h"
#include "hw_interface.h"
#include "hw_info_interface.h"
#include "fb/hw_hdmi.h"

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

  DisplayError error = HWHDMI::Create(&hw_intf_, hw_info_intf_,
                                      DisplayBase::buffer_sync_handler_);
  if (error != kErrorNone) {
    return error;
  }

  uint32_t active_mode_index;
  char value[64] = "0";
  Debug::GetProperty("sdm.hdmi.s3d_mode", value);
  HWS3DMode mode = (HWS3DMode)atoi(value);
  if (mode > kS3DModeNone && mode < kS3DModeMax) {
    active_mode_index = GetBestConfig(mode);
  } else {
    active_mode_index = GetBestConfig(kS3DModeNone);
  }

  error = hw_intf_->SetDisplayAttributes(active_mode_index);
  if (error != kErrorNone) {
    HWHDMI::Destroy(hw_intf_);
  }

  error = DisplayBase::Init();
  if (error != kErrorNone) {
    HWHDMI::Destroy(hw_intf_);
    return error;
  }

  GetScanSupport();
  underscan_supported_ = (scan_support_ == kScanAlwaysUnderscanned) || (scan_support_ == kScanBoth);

  s3d_format_to_mode_.insert(std::pair<LayerBufferS3DFormat, HWS3DMode>
                            (kS3dFormatNone, kS3DModeNone));
  s3d_format_to_mode_.insert(std::pair<LayerBufferS3DFormat, HWS3DMode>
                            (kS3dFormatLeftRight, kS3DModeLR));
  s3d_format_to_mode_.insert(std::pair<LayerBufferS3DFormat, HWS3DMode>
                            (kS3dFormatRightLeft, kS3DModeRL));
  s3d_format_to_mode_.insert(std::pair<LayerBufferS3DFormat, HWS3DMode>
                            (kS3dFormatTopBottom, kS3DModeTB));
  s3d_format_to_mode_.insert(std::pair<LayerBufferS3DFormat, HWS3DMode>
                            (kS3dFormatFramePacking, kS3DModeFP));

  /* currently FRC is only supported by HDMI as primary devices */
  if (Debug::IsFrcEnabled()) {
    frc_supported_ = hw_panel_info_.is_primary_panel;
  }

  error = HWEventsInterface::Create(INT(display_type_), this, &event_list_, &hw_events_intf_);
  if (error != kErrorNone) {
    DisplayBase::Deinit();
    HWHDMI::Destroy(hw_intf_);
    DLOGE("Failed to create hardware events interface. Error = %d", error);
  }

  return error;
}

DisplayError DisplayHDMI::Deinit() {
  SCOPE_LOCK(locker_);

  DisplayError error = DisplayBase::Deinit();
  HWHDMI::Destroy(hw_intf_);

  return error;
}

DisplayError DisplayHDMI::Prepare(LayerStack *layer_stack) {
  SCOPE_LOCK(locker_);

  SetS3DMode(layer_stack);

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
  return DisplayBase::SetVSyncState(enable);
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

DisplayError DisplayHDMI::GetRefreshRateRange(uint32_t *min_refresh_rate,
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

DisplayError DisplayHDMI::SetRefreshRate(uint32_t refresh_rate) {
  SCOPE_LOCK(locker_);

  if (!active_) {
    return kErrorPermission;
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

  if (display_attributes != display_attributes_) {
    error = comp_manager_->ReconfigureDisplay(display_comp_ctx_, display_attributes,
                                              hw_panel_info_);
    if (error != kErrorNone) {
      return error;
    }
    display_attributes_ = display_attributes;
  }
  return kErrorNone;
}

bool DisplayHDMI::IsUnderscanSupported() {
  SCOPE_LOCK(locker_);
  return DisplayBase::IsUnderscanSupported();
}

DisplayError DisplayHDMI::SetPanelBrightness(int level) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetPanelBrightness(level);
}

DisplayError DisplayHDMI::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  SCOPE_LOCK(locker_);
  return hw_intf_->OnMinHdcpEncryptionLevelChange(min_enc_level);
}

int DisplayHDMI::GetBestConfig(HWS3DMode s3d_mode) {
  uint32_t best_index = 0, index;
  uint32_t num_modes = 0;
  HWDisplayAttributes best_attrib;

  hw_intf_->GetNumDisplayAttributes(&num_modes);

  // Get display attribute for each mode
  HWDisplayAttributes *attrib = new HWDisplayAttributes[num_modes];
  for (index = 0; index < num_modes; index++) {
    hw_intf_->GetDisplayAttributes(index, &attrib[index]);
  }

  // Select best config for s3d_mode. If s3d is not enabled, s3d_mode is kS3DModeNone
  for (index = 0; index < num_modes; index ++) {
    if (IS_BIT_SET(attrib[index].s3d_config, s3d_mode)) {
      break;
    }
  }
  if (index < num_modes) {
    best_index = index;
    for (size_t index = best_index + 1; index < num_modes; index ++) {
      if (!IS_BIT_SET(attrib[index].s3d_config, s3d_mode))
        continue;

      // From the available configs, select the best
      // Ex: 1920x1080@60Hz is better than 1920x1080@30 and 1920x1080@30 is better than 1280x720@60
      if (attrib[index].y_pixels > attrib[best_index].y_pixels) {
          best_index = index;
      } else if (attrib[index].y_pixels == attrib[best_index].y_pixels) {
        if (attrib[index].x_pixels > attrib[best_index].x_pixels) {
          best_index = index;
        } else if (attrib[index].x_pixels == attrib[best_index].x_pixels) {
          if (attrib[index].vsync_period_ns < attrib[best_index].vsync_period_ns) {
            best_index = index;
          }
        }
      }
    }
  } else {
    DLOGW("%s, could not support S3D mode from EDID info. S3D mode is %d",
          __FUNCTION__, s3d_mode);
  }
  delete[] attrib;

  // Used for changing HDMI Resolution - override the best with user set config
  uint32_t user_config = Debug::GetHDMIResolution();
  if (user_config) {
    uint32_t config_index = -1;
    // For the config, get the corresponding index
    DisplayError error = hw_intf_->GetConfigIndex(user_config, &config_index);
    if (error == kErrorNone)
      return config_index;
  }

  return best_index;
}

void DisplayHDMI::GetScanSupport() {
  DisplayError error = kErrorNone;
  uint32_t video_format = -1;
  uint32_t max_cea_format = -1;
  HWScanInfo scan_info = HWScanInfo();
  hw_intf_->GetHWScanInfo(&scan_info);

  uint32_t active_mode_index = 0;
  hw_intf_->GetActiveConfig(&active_mode_index);

  error = hw_intf_->GetVideoFormat(active_mode_index, &video_format);
  if (error != kErrorNone) {
    return;
  }

  error = hw_intf_->GetMaxCEAFormat(&max_cea_format);
  if (error != kErrorNone) {
    return;
  }

  // The scan support for a given HDMI TV must be read from scan info corresponding to
  // Preferred Timing if the preferred timing of the display is currently active, and if it is
  // valid. In all other cases, we must read the scan support from CEA scan info if
  // the resolution is a CEA resolution, or from IT scan info for all other resolutions.
  if (active_mode_index == 0 && scan_info.pt_scan_support != kScanNotSupported) {
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

DisplayError DisplayHDMI::SetCursorPosition(int x, int y) {
  SCOPE_LOCK(locker_);
  return DisplayBase::SetCursorPosition(x, y);
}

void DisplayHDMI::SetS3DMode(LayerStack *layer_stack) {
  uint32_t s3d_layer_count = 0;
  HWS3DMode s3d_mode = kS3DModeNone;
  HWPanelInfo panel_info;
  HWDisplayAttributes display_attributes;
  uint32_t active_index = 0;
  uint32_t layer_count = layer_stack->layer_count;

  // S3D mode is supported for the following scenarios:
  // 1. Layer stack containing only one s3d layer which is not skip
  // 2. Layer stack containing only one secure layer along with one s3d layer
  for (uint32_t i = 0; i < layer_count; i++) {
    Layer &layer = layer_stack->layers[i];
    LayerBuffer *layer_buffer = layer.input_buffer;

    if (layer_buffer->s3d_format != kS3dFormatNone) {
      s3d_layer_count++;
      if (s3d_layer_count > 1 || layer.flags.skip) {
        s3d_mode = kS3DModeNone;
        break;
      }

      std::map<LayerBufferS3DFormat, HWS3DMode>::iterator it =
                s3d_format_to_mode_.find(layer_buffer->s3d_format);
      if (it != s3d_format_to_mode_.end()) {
        s3d_mode = it->second;
      }
    } else if (layer_buffer->flags.secure && layer_count > 2) {
        s3d_mode = kS3DModeNone;
        break;
    }
  }

  if (hw_intf_->SetS3DMode(s3d_mode) != kErrorNone) {
    hw_intf_->SetS3DMode(kS3DModeNone);
    layer_stack->flags.s3d_mode_present = false;
  } else if (s3d_mode != kS3DModeNone) {
    layer_stack->flags.s3d_mode_present = true;
  }

  hw_intf_->GetHWPanelInfo(&panel_info);
  hw_intf_->GetActiveConfig(&active_index);
  hw_intf_->GetDisplayAttributes(active_index, &display_attributes);

  if (panel_info != hw_panel_info_ || display_attributes != display_attributes_) {
    comp_manager_->ReconfigureDisplay(display_comp_ctx_, display_attributes, panel_info);
    hw_panel_info_ = panel_info;
    display_attributes_ = display_attributes;
  }
}

void DisplayHDMI::CECMessage(char *message) {
  event_handler_->CECMessage(message);
}

DisplayError DisplayHDMI::VSync(int64_t timestamp) {
  if (vsync_enable_) {
    DisplayEventVSync vsync;
    vsync.timestamp = timestamp;
    event_handler_->VSync(vsync);
  }

  return kErrorNone;
}

}  // namespace sdm

