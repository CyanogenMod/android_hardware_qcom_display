/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

#ifndef __HW_HDMI_H__
#define __HW_HDMI_H__

#include <video/msm_hdmi_modes.h>
#include <map>
#include <vector>

#include "hw_device.h"

namespace sdm {

class HWHDMI : public HWDevice {
 public:
  static DisplayError Create(HWInterface **intf, HWInfoInterface *hw_info_intf,
                             BufferSyncHandler *buffer_sync_handler);
  static DisplayError Destroy(HWInterface *intf);

 protected:
  HWHDMI(BufferSyncHandler *buffer_sync_handler, HWInfoInterface *hw_info_intf);
  virtual DisplayError Init(HWEventHandler *eventhandler);
  virtual DisplayError Deinit();
  virtual DisplayError GetNumDisplayAttributes(uint32_t *count);
  // Requirement to call this only after the first config has been explicitly set by client
  virtual DisplayError GetActiveConfig(uint32_t *active_config);
  virtual DisplayError GetDisplayAttributes(uint32_t index,
                                            HWDisplayAttributes *display_attributes);
  virtual DisplayError GetHWScanInfo(HWScanInfo *scan_info);
  virtual DisplayError GetVideoFormat(uint32_t config_index, uint32_t *video_format);
  virtual DisplayError GetMaxCEAFormat(uint32_t *max_cea_format);
  virtual DisplayError OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);
  virtual DisplayError SetDisplayAttributes(uint32_t index);
  virtual DisplayError GetConfigIndex(uint32_t mode, uint32_t *index);
  virtual DisplayError Validate(HWLayers *hw_layers);
  virtual DisplayError SetS3DMode(HWS3DMode s3d_mode);

 private:
  DisplayError ReadEDIDInfo();
  void ReadScanInfo();
  HWScanSupport MapHWScanSupport(uint32_t value);
  int OpenResolutionFile(int file_mode);
  void RequestNewPage(uint32_t page_number);
  DisplayError ReadTimingInfo();
  bool ReadResolutionFile(char *config_buffer);
  bool IsResolutionFilePresent();
  void SetSourceProductInformation(const char *node, const char *name);
  DisplayError GetDisplayS3DSupport(uint32_t num_modes,
                                    HWDisplayAttributes *attrib);
  bool IsSupportedS3DMode(HWS3DMode s3d_mode);

  uint32_t hdmi_mode_count_;
  uint32_t hdmi_modes_[256];
  // Holds the hdmi timing information. Ex: resolution, fps etc.,
  msm_hdmi_mode_timing_info *supported_video_modes_;
  HWScanInfo hw_scan_info_;
  uint32_t active_config_index_;
  std::map<HWS3DMode, msm_hdmi_s3d_mode> s3d_mode_sdm_to_mdp_;
  std::vector<HWS3DMode> supported_s3d_modes_;
  int active_mdp_s3d_mode_ = HDMI_S3D_NONE;
};

}  // namespace sdm

#endif  // __HW_HDMI_H__

