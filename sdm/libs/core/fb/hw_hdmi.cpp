/*
* Copyright (c) 2015 - 2016, The Linux Foundation. All rights reserved.
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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <utils/debug.h>
#include <utils/sys.h>
#include <vector>
#include <map>
#include <utility>

#include "hw_hdmi.h"

#define __CLASS__ "HWHDMI"

namespace sdm {

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

  info->grayscale = V4L2_PIX_FMT_RGB24;
  // If the mode supports YUV420 set grayscale to the FOURCC value for YUV420.
  if (IS_BIT_SET(mode->pixel_formats, 1)) {
    info->grayscale = V4L2_PIX_FMT_NV12;
  }

  if (!mode->active_low_h)
    info->sync |= FB_SYNC_HOR_HIGH_ACT;
  else
    info->sync &= ~FB_SYNC_HOR_HIGH_ACT;

  if (!mode->active_low_v)
    info->sync |= FB_SYNC_VERT_HIGH_ACT;
  else
    info->sync &= ~FB_SYNC_VERT_HIGH_ACT;

  return true;
}

DisplayError HWHDMI::Create(HWInterface **intf, HWInfoInterface *hw_info_intf,
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

DisplayError HWHDMI::Destroy(HWInterface *intf) {
  HWHDMI *hw_fb_hdmi = static_cast<HWHDMI *>(intf);
  hw_fb_hdmi->Deinit();
  delete hw_fb_hdmi;

  return kErrorNone;
}

HWHDMI::HWHDMI(BufferSyncHandler *buffer_sync_handler,  HWInfoInterface *hw_info_intf)
  : HWDevice(buffer_sync_handler), hw_scan_info_(), active_config_index_(0) {
  HWDevice::device_type_ = kDeviceHDMI;
  HWDevice::device_name_ = "HDMI Display Device";
  HWDevice::hw_info_intf_ = hw_info_intf;
}

DisplayError HWHDMI::Init() {
  DisplayError error = kErrorNone;

  SetSourceProductInformation("vendor_name", "ro.product.manufacturer");
  SetSourceProductInformation("product_description", "ro.product.name");

  error = HWDevice::Init();
  if (error != kErrorNone) {
    return error;
  }

  error = ReadEDIDInfo();
  if (error != kErrorNone) {
    Deinit();
    return error;
  }

  if (!IsResolutionFilePresent()) {
    Deinit();
    return kErrorHardware;
  }

  // Mode look-up table for HDMI
  supported_video_modes_ = new msm_hdmi_mode_timing_info[hdmi_mode_count_];
  if (!supported_video_modes_) {
    Deinit();
    return kErrorMemory;
  }

  error = ReadTimingInfo();
  if (error != kErrorNone) {
    Deinit();
    return error;
  }

  ReadScanInfo();

  s3d_mode_sdm_to_mdp_.insert(std::pair<HWS3DMode, msm_hdmi_s3d_mode>
                             (kS3DModeNone, HDMI_S3D_NONE));
  s3d_mode_sdm_to_mdp_.insert(std::pair<HWS3DMode, msm_hdmi_s3d_mode>
                             (kS3DModeLR, HDMI_S3D_SIDE_BY_SIDE));
  s3d_mode_sdm_to_mdp_.insert(std::pair<HWS3DMode, msm_hdmi_s3d_mode>
                             (kS3DModeRL, HDMI_S3D_SIDE_BY_SIDE));
  s3d_mode_sdm_to_mdp_.insert(std::pair<HWS3DMode, msm_hdmi_s3d_mode>
                             (kS3DModeTB, HDMI_S3D_TOP_AND_BOTTOM));
  s3d_mode_sdm_to_mdp_.insert(std::pair<HWS3DMode, msm_hdmi_s3d_mode>
                             (kS3DModeFP, HDMI_S3D_FRAME_PACKING));

  return error;
}

DisplayError HWHDMI::Deinit() {
  hdmi_mode_count_ = 0;
  if (supported_video_modes_) {
    delete[] supported_video_modes_;
  }

  return HWDevice::Deinit();
}

DisplayError HWHDMI::GetNumDisplayAttributes(uint32_t *count) {
  *count = hdmi_mode_count_;
  if (*count <= 0) {
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWHDMI::GetActiveConfig(uint32_t *active_config_index) {
  *active_config_index = active_config_index_;
  return kErrorNone;
}

DisplayError HWHDMI::ReadEDIDInfo() {
  ssize_t length = -1;
  char edid_str[kPageSize] = {'\0'};
  char edid_path[kMaxStringLength] = {'\0'};
  snprintf(edid_path, sizeof(edid_path), "%s%d/edid_modes", fb_path_, fb_node_index_);
  int edid_file = Sys::open_(edid_path, O_RDONLY);
  if (edid_file < 0) {
    DLOGE("EDID file open failed.");
    return kErrorHardware;
  }

  length = Sys::pread_(edid_file, edid_str, sizeof(edid_str)-1, 0);
  if (length <= 0) {
    DLOGE("%s: edid_modes file empty");
    return kErrorHardware;
  }
  Sys::close_(edid_file);

  DLOGI("EDID mode string: %s", edid_str);
  while (length > 1 && isspace(edid_str[length-1])) {
    --length;
  }
  edid_str[length] = '\0';

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

  return kErrorNone;
}

DisplayError HWHDMI::GetDisplayAttributes(uint32_t index,
                                          HWDisplayAttributes *display_attributes) {
  DTRACE_SCOPED();

  if (index > hdmi_mode_count_) {
    return kErrorNotSupported;
  }

  // Variable screen info
  STRUCT_VAR(fb_var_screeninfo, var_screeninfo);

  // Get the resolution info from the look up table
  msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[0];
  for (uint32_t i = 0; i < hdmi_mode_count_; i++) {
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

  GetDisplayS3DSupport(index, display_attributes);
  display_attributes->is_yuv = IS_BIT_SET(timing_mode->pixel_formats, 1);

  return kErrorNone;
}

DisplayError HWHDMI::SetDisplayAttributes(uint32_t index) {
  DTRACE_SCOPED();

  if (index > hdmi_mode_count_) {
    return kErrorNotSupported;
  }

  // Variable screen info
  STRUCT_VAR(fb_var_screeninfo, vscreeninfo);
  if (Sys::ioctl_(device_fd_, FBIOGET_VSCREENINFO, &vscreeninfo) < 0) {
    IOCTL_LOGE(FBIOGET_VSCREENINFO, device_type_);
    return kErrorHardware;
  }

  DLOGI("GetInfo<Mode=%d %dx%d (%d,%d,%d),(%d,%d,%d) %dMHz>", vscreeninfo.reserved[3],
        vscreeninfo.xres, vscreeninfo.yres, vscreeninfo.right_margin, vscreeninfo.hsync_len,
        vscreeninfo.left_margin, vscreeninfo.lower_margin, vscreeninfo.vsync_len,
        vscreeninfo.upper_margin, vscreeninfo.pixclock/1000000);

  msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[0];
  for (uint32_t i = 0; i < hdmi_mode_count_; i++) {
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
  if (Sys::ioctl_(device_fd_, MSMFB_METADATA_SET, &metadata) < 0) {
    IOCTL_LOGE(MSMFB_METADATA_SET, device_type_);
    return kErrorHardware;
  }

  DLOGI("SetInfo<Mode=%d %dx%d (%d,%d,%d),(%d,%d,%d) %dMHz>", vscreeninfo.reserved[3] & 0xFF00,
        vscreeninfo.xres, vscreeninfo.yres, vscreeninfo.right_margin, vscreeninfo.hsync_len,
        vscreeninfo.left_margin, vscreeninfo.lower_margin, vscreeninfo.vsync_len,
        vscreeninfo.upper_margin, vscreeninfo.pixclock/1000000);

  vscreeninfo.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_ALL | FB_ACTIVATE_FORCE;
  if (Sys::ioctl_(device_fd_, FBIOPUT_VSCREENINFO, &vscreeninfo) < 0) {
    IOCTL_LOGE(FBIOPUT_VSCREENINFO, device_type_);
    return kErrorHardware;
  }

  active_config_index_ = index;

  frame_rate_ = timing_mode->refresh_rate;

  // Get the supported s3d modes for current active config index
  HWDisplayAttributes attrib;
  GetDisplayS3DSupport(index, &attrib);
  supported_s3d_modes_.clear();
  supported_s3d_modes_.push_back(kS3DModeNone);
  for (uint32_t mode = kS3DModeNone + 1; mode < kS3DModeMax; mode ++) {
    if (IS_BIT_SET(attrib.s3d_config, (HWS3DMode)mode)) {
      supported_s3d_modes_.push_back((HWS3DMode)mode);
    }
  }

  SetS3DMode(kS3DModeNone);

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

DisplayError HWHDMI::Validate(HWLayers *hw_layers) {
  HWDevice::ResetDisplayParams();
  return HWDevice::Validate(hw_layers);
}

DisplayError HWHDMI::GetHWScanInfo(HWScanInfo *scan_info) {
  if (!scan_info) {
    return kErrorParameters;
  }
  *scan_info = hw_scan_info_;
  return kErrorNone;
}

DisplayError HWHDMI::GetVideoFormat(uint32_t config_index, uint32_t *video_format) {
  if (config_index > hdmi_mode_count_) {
    return kErrorNotSupported;
  }

  *video_format = hdmi_modes_[config_index];

  return kErrorNone;
}

DisplayError HWHDMI::GetMaxCEAFormat(uint32_t *max_cea_format) {
  *max_cea_format = HDMI_VFRMT_END;

  return kErrorNone;
}

DisplayError HWHDMI::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  DisplayError error = kErrorNone;
  int fd = -1;
  char data[kMaxStringLength] = {'\0'};

  snprintf(data, sizeof(data), "%s%d/hdcp2p2/min_level_change", fb_path_, fb_node_index_);

  fd = Sys::open_(data, O_WRONLY);
  if (fd < 0) {
    DLOGW("File '%s' could not be opened.", data);
    return kErrorHardware;
  }

  snprintf(data, sizeof(data), "%d", min_enc_level);

  ssize_t err = Sys::pwrite_(fd, data, strlen(data), 0);
  if (err <= 0) {
    DLOGE("Write failed, Error = %s", strerror(errno));
    error = kErrorHardware;
  }

  Sys::close_(fd);

  return error;
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
  char data[kPageSize] = {'\0'};

  snprintf(data, sizeof(data), "%s%d/scan_info", fb_path_, fb_node_index_);
  scan_info_file = Sys::open_(data, O_RDONLY);
  if (scan_info_file < 0) {
    DLOGW("File '%s' not found.", data);
    return;
  }

  memset(&data[0], 0, sizeof(data));
  len = Sys::pread_(scan_info_file, data, sizeof(data) - 1, 0);
  if (len <= 0) {
    Sys::close_(scan_info_file);
    DLOGW("File %s%d/scan_info is empty.", fb_path_, fb_node_index_);
    return;
  }
  data[len] = '\0';
  Sys::close_(scan_info_file);

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

int HWHDMI::OpenResolutionFile(int file_mode) {
  char file_path[kMaxStringLength];
  memset(file_path, 0, sizeof(file_path));
  snprintf(file_path , sizeof(file_path), "%s%d/res_info", fb_path_, fb_node_index_);

  int fd = Sys::open_(file_path, file_mode);

  if (fd < 0) {
    DLOGE("file '%s' not found : ret = %d err str: %s", file_path, fd, strerror(errno));
  }

  return fd;
}

// Method to request HDMI driver to write a new page of timing info into res_info node
void HWHDMI::RequestNewPage(uint32_t page_number) {
  char page_string[kPageSize];
  int fd = OpenResolutionFile(O_WRONLY);
  if (fd < 0) {
    return;
  }

  snprintf(page_string, sizeof(page_string), "%d", page_number);

  DLOGI_IF(kTagDriverConfig, "page=%s", page_string);

  ssize_t err = Sys::pwrite_(fd, page_string, sizeof(page_string), 0);
  if (err <= 0) {
    DLOGE("Write to res_info failed (%s)", strerror(errno));
  }

  Sys::close_(fd);
}

// Reads the contents of res_info node into a buffer if the file is not empty
bool HWHDMI::ReadResolutionFile(char *config_buffer) {
  ssize_t bytes_read = 0;
  int fd = OpenResolutionFile(O_RDONLY);
  if (fd >= 0) {
    bytes_read = Sys::pread_(fd, config_buffer, kPageSize, 0);
    Sys::close_(fd);
  }

  DLOGI_IF(kTagDriverConfig, "bytes_read = %d", bytes_read);

  return (bytes_read > 0);
}

// Populates the internal timing info structure with the timing info obtained
// from the HDMI driver
DisplayError HWHDMI::ReadTimingInfo() {
  uint32_t config_index = 0;
  uint32_t page_number = MSM_HDMI_INIT_RES_PAGE;
  uint32_t size = sizeof(msm_hdmi_mode_timing_info);

  while (true) {
    char config_buffer[kPageSize] = {0};
    msm_hdmi_mode_timing_info *info = reinterpret_cast<msm_hdmi_mode_timing_info *>(config_buffer);
    RequestNewPage(page_number);

    if (!ReadResolutionFile(config_buffer)) {
      break;
    }

    while (info->video_format && size < kPageSize && config_index < hdmi_mode_count_) {
      supported_video_modes_[config_index] = *info;
      size += sizeof(msm_hdmi_mode_timing_info);

      DLOGI_IF(kTagDriverConfig, "Config=%d Mode %d: (%dx%d) @ %d, pixel formats %d",
               config_index,
               supported_video_modes_[config_index].video_format,
               supported_video_modes_[config_index].active_h,
               supported_video_modes_[config_index].active_v,
               supported_video_modes_[config_index].refresh_rate,
               supported_video_modes_[config_index].pixel_formats);

      info++;
      config_index++;
    }

    size = sizeof(msm_hdmi_mode_timing_info);
    // Request HDMI driver to populate res_info with more
    // timing information
    page_number++;
  }

  if (page_number == MSM_HDMI_INIT_RES_PAGE || config_index == 0) {
    DLOGE("No timing information found.");
    return kErrorHardware;
  }

  return kErrorNone;
}

bool HWHDMI::IsResolutionFilePresent() {
  bool is_file_present = false;
  int fd = OpenResolutionFile(O_RDONLY);
  if (fd >= 0) {
    is_file_present = true;
    Sys::close_(fd);
  }

  return is_file_present;
}

void HWHDMI::SetSourceProductInformation(const char *node, const char *name) {
  char property_value[kMaxStringLength];
  char sys_fs_path[kMaxStringLength];
  int hdmi_node_index = GetFBNodeIndex(kDeviceHDMI);
  if (hdmi_node_index < 0) {
    return;
  }

  ssize_t length = 0;
  bool prop_read_success = Debug::GetProperty(name, property_value);
  if (!prop_read_success) {
    return;
  }

  snprintf(sys_fs_path , sizeof(sys_fs_path), "%s%d/%s", fb_path_, hdmi_node_index, node);
  length = HWDevice::SysFsWrite(sys_fs_path, property_value, strlen(property_value));
  if (length <= 0) {
    DLOGW("Failed to write %s = %s", node, property_value);
  }
}

DisplayError HWHDMI::GetDisplayS3DSupport(uint32_t index,
                                          HWDisplayAttributes *attrib) {
  ssize_t length = -1;
  char edid_s3d_str[kPageSize] = {'\0'};
  char edid_s3d_path[kMaxStringLength] = {'\0'};
  snprintf(edid_s3d_path, sizeof(edid_s3d_path), "%s%d/edid_3d_modes", fb_path_, fb_node_index_);

  if (index > hdmi_mode_count_) {
    return kErrorNotSupported;
  }

  SET_BIT(attrib->s3d_config, kS3DModeNone);

  // Three level inception!
  // The string looks like 16=SSH,4=FP:TAB:SSH,5=FP:SSH,32=FP:TAB:SSH
  char *saveptr_l1, *saveptr_l2, *saveptr_l3;
  char *l1, *l2, *l3;

  int edid_s3d_node = Sys::open_(edid_s3d_path, O_RDONLY);
  if (edid_s3d_node < 0) {
    DLOGW("%s could not be opened : %s", edid_s3d_path, strerror(errno));
    return kErrorNotSupported;
  }

  length = Sys::pread_(edid_s3d_node, edid_s3d_str, sizeof(edid_s3d_str)-1, 0);
  if (length <= 0) {
    Sys::close_(edid_s3d_node);
    return kErrorNotSupported;
  }

  l1 = strtok_r(edid_s3d_str, ",", &saveptr_l1);
  while (l1 != NULL) {
    l2 = strtok_r(l1, "=", &saveptr_l2);
    if (l2 != NULL) {
      if (hdmi_modes_[index] == (uint32_t)atoi(l2)) {
          l3 = strtok_r(saveptr_l2, ":", &saveptr_l3);
          while (l3 != NULL) {
            if (strncmp("SSH", l3, strlen("SSH")) == 0) {
              SET_BIT(attrib->s3d_config, kS3DModeLR);
              SET_BIT(attrib->s3d_config, kS3DModeRL);
            } else if (strncmp("TAB", l3, strlen("TAB")) == 0) {
              SET_BIT(attrib->s3d_config, kS3DModeTB);
            } else if (strncmp("FP", l3, strlen("FP")) == 0) {
              SET_BIT(attrib->s3d_config, kS3DModeFP);
            }
            l3 = strtok_r(NULL, ":", &saveptr_l3);
          }
      }
    }
    l1 = strtok_r(NULL, ",", &saveptr_l1);
  }

  Sys::close_(edid_s3d_node);
  return kErrorNone;
}

bool HWHDMI::IsSupportedS3DMode(HWS3DMode s3d_mode) {
  for (uint32_t i = 0; i < supported_s3d_modes_.size(); i++) {
    if (supported_s3d_modes_[i] == s3d_mode) {
      return true;
    }
  }
  return false;
}

DisplayError HWHDMI::SetS3DMode(HWS3DMode s3d_mode) {
  if (!IsSupportedS3DMode(s3d_mode)) {
    DLOGW("S3D mode is not supported s3d_mode = %d", s3d_mode);
    return kErrorNotSupported;
  }

  std::map<HWS3DMode, msm_hdmi_s3d_mode>::iterator it = s3d_mode_sdm_to_mdp_.find(s3d_mode);
  if (it == s3d_mode_sdm_to_mdp_.end()) {
    return kErrorNotSupported;
  }
  msm_hdmi_s3d_mode s3d_mdp_mode = it->second;

  if (active_mdp_s3d_mode_ == s3d_mdp_mode) {
    // HDMI_S3D_SIDE_BY_SIDE is an mdp mapping for kS3DModeLR and kS3DModeRL s3d modes. So no need
    // to update the s3d_mode node. hw_panel_info needs to be updated to differentiate these two s3d
    // modes in strategy
    hw_panel_info_.s3d_mode = s3d_mode;
    return kErrorNone;
  }

  ssize_t length = -1;
  char s3d_mode_path[kMaxStringLength] = {'\0'};
  char s3d_mode_string[kMaxStringLength] = {'\0'};
  snprintf(s3d_mode_path, sizeof(s3d_mode_path), "%s%d/s3d_mode", fb_path_, fb_node_index_);

  int s3d_mode_node = Sys::open_(s3d_mode_path, O_RDWR);
  if (s3d_mode_node < 0) {
    DLOGW("%s could not be opened : %s", s3d_mode_path, strerror(errno));
    return kErrorNotSupported;
  }

  snprintf(s3d_mode_string, sizeof(s3d_mode_string), "%d", s3d_mdp_mode);
  length = Sys::pwrite_(s3d_mode_node, s3d_mode_string, sizeof(s3d_mode_string), 0);
  if (length <= 0) {
    DLOGW("Failed to write into s3d node: %s", strerror(errno));
    Sys::close_(s3d_mode_node);
    return kErrorNotSupported;
  }

  active_mdp_s3d_mode_ = s3d_mdp_mode;
  hw_panel_info_.s3d_mode = s3d_mode;
  Sys::close_(s3d_mode_node);

  DLOGI_IF(kTagDriverConfig, "s3d mode %d", hw_panel_info_.s3d_mode);
  return kErrorNone;
}

DisplayError HWHDMI::GetDynamicFrameRateMode(uint32_t refresh_rate, uint32_t *mode,
                                             DynamicFPSData *data, uint32_t *config_index) {
  msm_hdmi_mode_timing_info *cur = NULL;
  msm_hdmi_mode_timing_info *dst = NULL;
  uint32_t i = 0;
  int pre_refresh_rate_diff = 0;
  bool pre_unstd_mode = false;

  for (i = 0; i < hdmi_mode_count_; i++) {
    msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[i];
    if (timing_mode->video_format == hdmi_modes_[active_config_index_]) {
      cur = timing_mode;
      break;
    }
  }

  if (cur == NULL) {
    DLOGE("can't find timing info for active config index(%d)", active_config_index_);
    return kErrorUndefined;
  }

  if (cur->refresh_rate != frame_rate_) {
    pre_unstd_mode = true;
  }

  if (i >= hdmi_mode_count_) {
    return kErrorNotSupported;
  }

  dst = cur;
  pre_refresh_rate_diff = static_cast<int>(dst->refresh_rate) - static_cast<int>(refresh_rate);

  for (i = 0; i < hdmi_mode_count_; i++) {
    msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[i];
    if (cur->active_h == timing_mode->active_h &&
       cur->active_v == timing_mode->active_v) {
      int cur_refresh_rate_diff = static_cast<int>(timing_mode->refresh_rate) -
                                  static_cast<int>(refresh_rate);
      if (abs(pre_refresh_rate_diff) > abs(cur_refresh_rate_diff)) {
        pre_refresh_rate_diff = cur_refresh_rate_diff;
        dst = timing_mode;
      }
    }
  }

  if (abs(pre_refresh_rate_diff) > kThresholdRefreshRate) {
    return kErrorNotSupported;
  }

  GetConfigIndex(dst->video_format, config_index);

  // When there is a change in pixel format set the mode using FBIOPUT_VSCREENINFO info ioctl.
  if (cur->pixel_formats != dst->pixel_formats) {
    *mode = kModeSuspendResume;
    return kErrorNone;
  }

  data->hor_front_porch = dst->front_porch_h;
  data->hor_back_porch = dst->back_porch_h;
  data->hor_pulse_width = dst->pulse_width_h;
  data->clk_rate_hz = dst->pixel_freq;
  data->fps = refresh_rate;

  if (dst->front_porch_h != cur->front_porch_h) {
    *mode = kModeHFP;
  }

  if (dst->refresh_rate != refresh_rate || dst->pixel_freq != cur->pixel_freq) {
    if (*mode == kModeHFP) {
      if (dst->refresh_rate != refresh_rate) {
        *mode = kModeHFPCalcClock;
      } else {
        *mode = kModeClockHFP;
      }
    } else {
        *mode = kModeClock;
    }
  }

  if (pre_unstd_mode && (*mode == kModeHFP)) {
    *mode = kModeClockHFP;
  }

  return kErrorNone;
}

DisplayError HWHDMI::SetRefreshRate(uint32_t refresh_rate) {
  char mode_path[kMaxStringLength] = {0};
  char node_path[kMaxStringLength] = {0};
  uint32_t mode = kModeClock;
  uint32_t config_index = 0;
  DynamicFPSData data;
  DisplayError error = kErrorNone;

  if (refresh_rate == frame_rate_) {
    return error;
  }

  error = GetDynamicFrameRateMode(refresh_rate, &mode, &data, &config_index);
  if (error != kErrorNone) {
    return error;
  }

  if (mode == kModeSuspendResume) {
    SetDisplayAttributes(config_index);
    return kErrorNone;
  }

  snprintf(mode_path, sizeof(mode_path), "%s%d/msm_fb_dfps_mode", fb_path_, fb_node_index_);
  snprintf(node_path, sizeof(node_path), "%s%d/dynamic_fps", fb_path_, fb_node_index_);

  int fd_mode = Sys::open_(mode_path, O_WRONLY);
  if (fd_mode < 0) {
    DLOGE("Failed to open %s with error %s", mode_path, strerror(errno));
    return kErrorFileDescriptor;
  }

  char dfps_mode[kMaxStringLength];
  snprintf(dfps_mode, sizeof(dfps_mode), "%d", mode);
  DLOGI_IF(kTagDriverConfig, "Setting dfps_mode  = %d", mode);
  ssize_t len = Sys::pwrite_(fd_mode, dfps_mode, strlen(dfps_mode), 0);
  if (len < 0) {
    DLOGE("Failed to enable dfps mode %d with error %s", mode, strerror(errno));
    Sys::close_(fd_mode);
    return kErrorUndefined;
  }
  Sys::close_(fd_mode);

  int fd_node = Sys::open_(node_path, O_WRONLY);
  if (fd_node < 0) {
    DLOGE("Failed to open %s with error %s", node_path, strerror(errno));
    return kErrorFileDescriptor;
  }

  char refresh_rate_string[kMaxStringLength];
  if (mode == kModeHFP || mode == kModeClock) {
    snprintf(refresh_rate_string, sizeof(refresh_rate_string), "%d", refresh_rate);
    DLOGI_IF(kTagDriverConfig, "Setting refresh rate = %d", refresh_rate);
  } else {
    snprintf(refresh_rate_string, sizeof(refresh_rate_string), "%d %d %d %d %d",
             data.hor_front_porch, data.hor_back_porch, data.hor_pulse_width,
             data.clk_rate_hz, data.fps);
  }
  len = Sys::pwrite_(fd_node, refresh_rate_string, strlen(refresh_rate_string), 0);
  if (len < 0) {
    DLOGE("Failed to write %d with error %s", refresh_rate, strerror(errno));
    Sys::close_(fd_node);
    return kErrorUndefined;
  }
  Sys::close_(fd_node);

  error = ReadTimingInfo();
  if (error != kErrorNone) {
    return error;
  }

  frame_rate_ = refresh_rate;
  active_config_index_ = config_index;

  DLOGI_IF(kTagDriverConfig, "config_index(%d) Mode(%d) frame_rate(%d)",
           config_index,
           mode,
           frame_rate_);

  return kErrorNone;
}

}  // namespace sdm

