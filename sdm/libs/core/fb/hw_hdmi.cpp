/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
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

#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <utils/debug.h>
#include <fcntl.h>
#include "hw_hdmi.h"

#define __CLASS__ "HWHDMI"

namespace sdm {

static int ParseLine(char *input, char *tokens[], const uint32_t max_token, uint32_t *count) {
  char *tmp_token = NULL;
  char *temp_ptr;
  uint32_t index = 0;
  const char *delim = ", =\n";
  if (!input) {
    return -1;
  }
  tmp_token = strtok_r(input, delim, &temp_ptr);
  while (tmp_token && index < max_token) {
    tokens[index++] = tmp_token;
    tmp_token = strtok_r(NULL, delim, &temp_ptr);
  }
  *count = index;

  return 0;
}

static bool MapHDMIDisplayTiming(const msm_hdmi_mode_timing_info *mode,
                                 fb_var_screeninfo *info) {
  if (!mode || !info) {
    return false;
  }

  info->reserved[0] = 0;
  info->reserved[1] = 0;
  info->reserved[2] = 0;
  info->reserved[3] = (info->reserved[3] & 0xFFFF) | (mode->video_format << 16);
  info->xoffset = 0;
  info->yoffset = 0;
  info->xres = mode->active_h;
  info->yres = mode->active_v;
  info->pixclock = (mode->pixel_freq) * 1000;
  info->vmode = mode->interlaced ? FB_VMODE_INTERLACED : FB_VMODE_NONINTERLACED;
  info->right_margin = mode->front_porch_h;
  info->hsync_len = mode->pulse_width_h;
  info->left_margin = mode->back_porch_h;
  info->lower_margin = mode->front_porch_v;
  info->vsync_len = mode->pulse_width_v;
  info->upper_margin = mode->back_porch_v;

  return true;
}

DisplayError HWHDMIInterface::Create(HWHDMIInterface **intf, HWInfoInterface *hw_info_intf,
                                     BufferSyncHandler *buffer_sync_handler) {
  DisplayError error = kErrorNone;
  HWHDMI *hw_fb_hdmi = NULL;

  hw_fb_hdmi = new HWHDMI(buffer_sync_handler, hw_info_intf);
  error = hw_fb_hdmi->Init();
  if (error != kErrorNone) {
    delete hw_fb_hdmi;
  } else {
    *intf = hw_fb_hdmi;
  }
  return error;
}

DisplayError HWHDMIInterface::Destroy(HWHDMIInterface *intf) {
  HWHDMI *hw_fb_hdmi = static_cast<HWHDMI *>(intf);
  hw_fb_hdmi->Deinit();
  delete hw_fb_hdmi;

  return kErrorNone;
}

HWHDMI::HWHDMI(BufferSyncHandler *buffer_sync_handler,  HWInfoInterface *hw_info_intf)
  : HWDevice(buffer_sync_handler), hw_scan_info_() {
  HWDevice::device_type_ = kDeviceHDMI;
  HWDevice::device_name_ = "HDMI Display Device";
  HWDevice::hw_info_intf_ = hw_info_intf;
}

DisplayError HWHDMI::Init() {
  DisplayError error = kErrorNone;

  error = HWDevice::Init();
  if (error != kErrorNone) {
    goto CleanupOnError;
  }

  // Mode look-up table for HDMI
  supported_video_modes_ = new msm_hdmi_mode_timing_info[HDMI_VFRMT_MAX];
  if (!supported_video_modes_) {
    error = kErrorMemory;
    goto CleanupOnError;
  }
  // Populate the mode table for supported modes
  MSM_HDMI_MODES_INIT_TIMINGS(supported_video_modes_);
  MSM_HDMI_MODES_SET_SUPP_TIMINGS(supported_video_modes_, MSM_HDMI_MODES_ALL);

  ReadScanInfo();
  return kErrorNone;

CleanupOnError:
  if (supported_video_modes_) {
    delete supported_video_modes_;
  }

  return error;
}

DisplayError HWHDMI::Deinit() {
  hdmi_mode_count_ = 0;
  if (supported_video_modes_) {
    delete supported_video_modes_;
  }

  return kErrorNone;
}

DisplayError HWHDMI::Open(HWEventHandler *eventhandler) {
  return HWDevice::Open(eventhandler);
}

DisplayError HWHDMI::Close() {
  return HWDevice::Close();
}

DisplayError HWHDMI::GetNumDisplayAttributes(uint32_t *count) {
  *count = GetHDMIModeCount();
  if (*count <= 0) {
    return kErrorHardware;
  }

  return kErrorNone;
}

int HWHDMI::GetHDMIModeCount() {
  ssize_t length = -1;
  char edid_str[256] = {'\0'};
  char edid_path[kMaxStringLength] = {'\0'};
  snprintf(edid_path, sizeof(edid_path), "%s%d/edid_modes", fb_path_, fb_node_index_);
  int edid_file = open_(edid_path, O_RDONLY);
  if (edid_file < 0) {
    DLOGE("EDID file open failed.");
    return -1;
  }

  length = pread_(edid_file, edid_str, sizeof(edid_str)-1, 0);
  if (length <= 0) {
    DLOGE("%s: edid_modes file empty");
    edid_str[0] = '\0';
  } else {
    DLOGI("EDID mode string: %s", edid_str);
    while (length > 1 && isspace(edid_str[length-1])) {
      --length;
    }
    edid_str[length] = '\0';
  }
  close_(edid_file);

  if (length > 0) {
    // Get EDID modes from the EDID string
    char *ptr = edid_str;
    const uint32_t edid_count_max = 128;
    char *tokens[edid_count_max] = { NULL };
    ParseLine(ptr, tokens, edid_count_max, &hdmi_mode_count_);
    for (uint32_t i = 0; i < hdmi_mode_count_; i++) {
      hdmi_modes_[i] = atoi(tokens[i]);
    }
  }
  return (hdmi_mode_count_ > 0) ? hdmi_mode_count_ : 0;
}

DisplayError HWHDMI::GetDisplayAttributes(HWDisplayAttributes *display_attributes,
                                                     uint32_t index) {
  DTRACE_SCOPED();

  // Variable screen info
  STRUCT_VAR(fb_var_screeninfo, var_screeninfo);

  // Get the resolution info from the look up table
  msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[0];
  for (int i = 0; i < HDMI_VFRMT_MAX; i++) {
    msm_hdmi_mode_timing_info *cur = &supported_video_modes_[i];
    if (cur->video_format == hdmi_modes_[index]) {
      timing_mode = cur;
      break;
    }
  }
  display_attributes->x_pixels = timing_mode->active_h;
  display_attributes->y_pixels = timing_mode->active_v;
  display_attributes->v_front_porch = timing_mode->front_porch_v;
  display_attributes->v_back_porch = timing_mode->back_porch_v;
  display_attributes->v_pulse_width = timing_mode->pulse_width_v;
  uint32_t h_blanking = timing_mode->front_porch_h + timing_mode->back_porch_h +
      timing_mode->pulse_width_h;
  display_attributes->h_total = timing_mode->active_h + h_blanking;
  display_attributes->x_dpi = 0;
  display_attributes->y_dpi = 0;
  display_attributes->fps = timing_mode->refresh_rate / 1000;
  display_attributes->vsync_period_ns = UINT32(1000000000L / display_attributes->fps);
  display_attributes->split_left = display_attributes->x_pixels;
  if (display_attributes->x_pixels > hw_resource_.max_mixer_width) {
    display_attributes->is_device_split = true;
    display_attributes->split_left = display_attributes->x_pixels / 2;
    display_attributes->h_total += h_blanking;
  }
  return kErrorNone;
}

DisplayError HWHDMI::SetDisplayAttributes(uint32_t index) {
  DTRACE_SCOPED();

  DisplayError error = kErrorNone;

  // Variable screen info
  STRUCT_VAR(fb_var_screeninfo, vscreeninfo);
  if (ioctl_(device_fd_, FBIOGET_VSCREENINFO, &vscreeninfo) < 0) {
    IOCTL_LOGE(FBIOGET_VSCREENINFO, device_type_);
    return kErrorHardware;
  }

  DLOGI("GetInfo<Mode=%d %dx%d (%d,%d,%d),(%d,%d,%d) %dMHz>", vscreeninfo.reserved[3],
        vscreeninfo.xres, vscreeninfo.yres, vscreeninfo.right_margin, vscreeninfo.hsync_len,
        vscreeninfo.left_margin, vscreeninfo.lower_margin, vscreeninfo.vsync_len,
        vscreeninfo.upper_margin, vscreeninfo.pixclock/1000000);

  msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[0];
  for (int i = 0; i < HDMI_VFRMT_MAX; i++) {
    msm_hdmi_mode_timing_info *cur = &supported_video_modes_[i];
    if (cur->video_format == hdmi_modes_[index]) {
      timing_mode = cur;
      break;
    }
  }

  if (MapHDMIDisplayTiming(timing_mode, &vscreeninfo) == false) {
    return kErrorParameters;
  }

  STRUCT_VAR(msmfb_metadata, metadata);
  metadata.op = metadata_op_vic;
  metadata.data.video_info_code = timing_mode->video_format;
  if (ioctl(device_fd_, MSMFB_METADATA_SET, &metadata) < 0) {
    IOCTL_LOGE(MSMFB_METADATA_SET, device_type_);
    return kErrorHardware;
  }

  DLOGI("SetInfo<Mode=%d %dx%d (%d,%d,%d),(%d,%d,%d) %dMHz>", vscreeninfo.reserved[3] & 0xFF00,
        vscreeninfo.xres, vscreeninfo.yres, vscreeninfo.right_margin, vscreeninfo.hsync_len,
        vscreeninfo.left_margin, vscreeninfo.lower_margin, vscreeninfo.vsync_len,
        vscreeninfo.upper_margin, vscreeninfo.pixclock/1000000);

  vscreeninfo.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_ALL | FB_ACTIVATE_FORCE;
  if (ioctl_(device_fd_, FBIOPUT_VSCREENINFO, &vscreeninfo) < 0) {
    IOCTL_LOGE(FBIOGET_VSCREENINFO, device_type_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWHDMI::GetConfigIndex(uint32_t mode, uint32_t *index) {
  // Check if the mode is valid and return corresponding index
  for (uint32_t i = 0; i < hdmi_mode_count_; i++) {
    if (hdmi_modes_[i] == mode) {
      *index = i;
      DLOGI("Index = %d for config = %d", *index, mode);
      return kErrorNone;
    }
  }

  DLOGE("Config = %d not supported", mode);
  return kErrorNotSupported;
}

DisplayError HWHDMI::PowerOn() {
  return HWDevice::PowerOn();
}

DisplayError HWHDMI::PowerOff() {
  return HWDevice::PowerOff();
}

DisplayError HWHDMI::Doze() {
  return HWDevice::Doze();
}

DisplayError HWHDMI::DozeSuspend() {
  return HWDevice::DozeSuspend();
}

DisplayError HWHDMI::Standby() {
  return HWDevice::Standby();
}

DisplayError HWHDMI::Validate(HWLayers *hw_layers) {
  HWDevice::ResetDisplayParams();
  return HWDevice::Validate(hw_layers);
}

DisplayError HWHDMI::Commit(HWLayers *hw_layers) {
  return HWDevice::Commit(hw_layers);
}

DisplayError HWHDMI::Flush() {
  return HWDevice::Flush();
}

DisplayError HWHDMI::GetHWPanelInfo(HWPanelInfo *panel_info) {
  return HWDevice::GetHWPanelInfo(panel_info);
}

DisplayError HWHDMI::GetHWScanInfo(HWScanInfo *scan_info) {
  if (!scan_info) {
    return kErrorParameters;
  }
  *scan_info = hw_scan_info_;
  return kErrorNone;
}

DisplayError HWHDMI::GetVideoFormat(uint32_t config_index, uint32_t *video_format) {
  *video_format = hdmi_modes_[config_index];

  return kErrorNone;
}

DisplayError HWHDMI::GetMaxCEAFormat(uint32_t *max_cea_format) {
  *max_cea_format = HDMI_VFRMT_END;

  return kErrorNone;
}

HWScanSupport HWHDMI::MapHWScanSupport(uint32_t value) {
  switch (value) {
  // TODO(user): Read the scan type from driver defined values instead of hardcoding
  case 0:
    return kScanNotSupported;
  case 1:
    return kScanAlwaysOverscanned;
  case 2:
    return kScanAlwaysUnderscanned;
  case 3:
    return kScanBoth;
  default:
    return kScanNotSupported;
    break;
  }
}

void HWHDMI::ReadScanInfo() {
  int scan_info_file = -1;
  ssize_t len = -1;
  char data[4096] = {'\0'};

  snprintf(data, sizeof(data), "%s%d/scan_info", fb_path_, fb_node_index_);
  scan_info_file = open_(data, O_RDONLY);
  if (scan_info_file < 0) {
    DLOGW("File '%s' not found.", data);
    return;
  }

  memset(&data[0], 0, sizeof(data));
  len = read(scan_info_file, data, sizeof(data) - 1);
  if (len <= 0) {
    close_(scan_info_file);
    DLOGW("File %s%d/scan_info is empty.", fb_path_, fb_node_index_);
    return;
  }
  data[len] = '\0';
  close_(scan_info_file);

  const uint32_t scan_info_max_count = 3;
  uint32_t scan_info_count = 0;
  char *tokens[scan_info_max_count] = { NULL };
  ParseLine(data, tokens, scan_info_max_count, &scan_info_count);
  if (scan_info_count != scan_info_max_count) {
    DLOGW("Failed to parse scan info string %s", data);
    return;
  }

  hw_scan_info_.pt_scan_support = MapHWScanSupport(atoi(tokens[0]));
  hw_scan_info_.it_scan_support = MapHWScanSupport(atoi(tokens[1]));
  hw_scan_info_.cea_scan_support = MapHWScanSupport(atoi(tokens[2]));
  DLOGI("PT %d IT %d CEA %d", hw_scan_info_.pt_scan_support, hw_scan_info_.it_scan_support,
        hw_scan_info_.cea_scan_support);
}

DisplayError HWHDMI::GetPPFeaturesVersion(PPFeatureVersion *vers) {
  return kErrorNotSupported;
}

DisplayError HWHDMI::SetPPFeatures(PPFeaturesConfig &feature_list) {
  return kErrorNotSupported;
}

}  // namespace sdm

