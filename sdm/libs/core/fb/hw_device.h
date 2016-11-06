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

#ifndef __HW_DEVICE_H__
#define __HW_DEVICE_H__

#include <errno.h>
#include <linux/msm_mdp_ext.h>
#include <linux/mdss_rotator.h>
#include <pthread.h>

#include "hw_interface.h"

#define IOCTL_LOGE(ioctl, type) DLOGE("ioctl %s, device = %d errno = %d, desc = %s", #ioctl, \
                                      type, errno, strerror(errno))

namespace sdm {
class HWInfoInterface;

class HWDevice : public HWInterface {
 protected:
  explicit HWDevice(BufferSyncHandler *buffer_sync_handler);
  virtual ~HWDevice() {}

  // From HWInterface
  virtual DisplayError GetActiveConfig(uint32_t *active_config);
  virtual DisplayError GetNumDisplayAttributes(uint32_t *count);
  virtual DisplayError GetDisplayAttributes(uint32_t index,
                                            HWDisplayAttributes *display_attributes);
  virtual DisplayError GetHWPanelInfo(HWPanelInfo *panel_info);
  virtual DisplayError SetDisplayAttributes(uint32_t index);
  virtual DisplayError GetConfigIndex(uint32_t mode, uint32_t *index);
  virtual DisplayError PowerOn();
  virtual DisplayError PowerOff();
  virtual DisplayError Doze();
  virtual DisplayError DozeSuspend();
  virtual DisplayError Standby();
  virtual DisplayError Validate(HWLayers *hw_layers);
  virtual DisplayError Commit(HWLayers *hw_layers);
  virtual DisplayError Flush();
  virtual DisplayError GetPPFeaturesVersion(PPFeatureVersion *vers);
  virtual DisplayError SetPPFeatures(PPFeaturesConfig *feature_list);
  virtual DisplayError SetVSyncState(bool enable);
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);
  virtual DisplayError SetDisplayMode(const HWDisplayMode hw_display_mode);
  virtual DisplayError SetRefreshRate(uint32_t refresh_rate);
  virtual DisplayError SetPanelBrightness(int level);
  virtual DisplayError GetHWScanInfo(HWScanInfo *scan_info);
  virtual DisplayError GetVideoFormat(uint32_t config_index, uint32_t *video_format);
  virtual DisplayError GetMaxCEAFormat(uint32_t *max_cea_format);
  virtual DisplayError SetCursorPosition(HWLayers *hw_layers, int x, int y);
  virtual DisplayError OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);
  virtual DisplayError GetPanelBrightness(int *level);
  virtual DisplayError SetAutoRefresh(bool enable) { return kErrorNone; }
  virtual DisplayError SetS3DMode(HWS3DMode s3d_mode);

  // For HWDevice derivatives
  virtual DisplayError Init();
  virtual DisplayError Deinit();

  enum {
    kHWEventVSync,
    kHWEventBlank,
  };

  static const int kMaxStringLength = 1024;
  static const int kNumPhysicalDisplays = 2;

  void DumpLayerCommit(const mdp_layer_commit &layer_commit);
  DisplayError SetFormat(const LayerBufferFormat &source, uint32_t *target);
  DisplayError SetStride(HWDeviceType device_type, LayerBufferFormat format,
                         uint32_t width, uint32_t *target);
  void SetBlending(const LayerBlending &source, mdss_mdp_blend_op *target);
  void SetRect(const LayerRect &source, mdp_rect *target);
  void SetMDPFlags(const Layer &layer, const bool &is_rotator_used,
                   bool is_cursor_pipe_used, uint32_t *mdp_flags);
  // Retrieves HW FrameBuffer Node Index
  int GetFBNodeIndex(HWDeviceType device_type);
  // Populates HWPanelInfo based on node index
  void PopulateHWPanelInfo();
  void GetHWPanelInfoByNode(int device_node, HWPanelInfo *panel_info);
  void GetHWPanelNameByNode(int device_node, HWPanelInfo *panel_info);
  void GetHWDisplayPortAndMode(int device_node, HWDisplayPort *port, HWDisplayMode *mode);
  void GetSplitInfo(int device_node, HWPanelInfo *panel_info);
  void GetHWPanelMaxBrightnessFromNode(HWPanelInfo *panel_info);
  int ParseLine(char *input, char *tokens[], const uint32_t max_token, uint32_t *count);
  int ParseLine(char *input, const char *delim, char *tokens[],
                const uint32_t max_token, uint32_t *count);
  mdp_scale_data* GetScaleDataRef(uint32_t index) { return &scale_data_[index]; }
  void SetHWScaleData(const ScaleData &scale, uint32_t index);
  void ResetDisplayParams();
  void SetCSC(LayerCSC source, mdp_color_space *color_space);
  void SetIGC(const Layer &layer, uint32_t index);
  void SetFRC(HWLayers *hw_layers);

  bool EnableHotPlugDetection(int enable);
  ssize_t SysFsWrite(const char* file_node, const char* value, ssize_t length);

  HWResourceInfo hw_resource_;
  HWPanelInfo hw_panel_info_;
  HWInfoInterface *hw_info_intf_;
  int fb_node_index_;
  const char *fb_path_;
  bool hotplug_enabled_;
  BufferSyncHandler *buffer_sync_handler_;
  int device_fd_;
  HWDeviceType device_type_;
  mdp_layer_commit mdp_disp_commit_;
  mdp_input_layer mdp_in_layers_[kMaxSDELayers * 2];   // split panel (left + right)
  mdp_scale_data scale_data_[kMaxSDELayers * 2];
  mdp_overlay_pp_params pp_params_[kMaxSDELayers * 2];
  mdp_igc_lut_data_v1_7 igc_lut_data_[kMaxSDELayers * 2];
  mdp_output_layer mdp_out_layer_;
  const char *device_name_;
  bool synchronous_commit_;
  mdp_frc_info mdp_frc_info_;
};

}  // namespace sdm

#endif  // __HW_DEVICE_H__

