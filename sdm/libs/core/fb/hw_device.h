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

#ifndef __HW_DEVICE_H__
#define __HW_DEVICE_H__

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/msm_mdp_ext.h>
#include <linux/mdss_rotator.h>
#include <poll.h>
#include <pthread.h>

#include "hw_interface.h"
#include "hw_info_interface.h"

#define IOCTL_LOGE(ioctl, type) DLOGE("ioctl %s, device = %d errno = %d, desc = %s", #ioctl, \
                                      type, errno, strerror(errno))

namespace sdm {

class HWDevice {
 protected:
  explicit HWDevice(BufferSyncHandler *buffer_sync_handler);
  DisplayError Init();
  DisplayError Open(HWEventHandler *eventhandler);
  DisplayError Close();
  DisplayError GetNumDisplayAttributes(uint32_t *count);
  DisplayError GetDisplayAttributes(HWDisplayAttributes *display_attributes,
                                    uint32_t index);
  DisplayError GetHWPanelInfo(HWPanelInfo *panel_info);
  DisplayError SetDisplayAttributes(uint32_t index);
  DisplayError GetConfigIndex(uint32_t mode, uint32_t *index);
  DisplayError PowerOn();
  DisplayError PowerOff();
  DisplayError Doze();
  DisplayError DozeSuspend();
  DisplayError Standby();
  DisplayError Validate(HWLayers *hw_layers);
  DisplayError Commit(HWLayers *hw_layers);
  DisplayError Flush();

  enum {
    kHWEventVSync,
    kHWEventBlank,
  };

  static const int kMaxStringLength = 1024;
  static const int kNumPhysicalDisplays = 2;
  static const int kNumDisplayEvents = 4;

  void DumpLayerCommit(const mdp_layer_commit &layer_commit);
  DisplayError SetFormat(const LayerBufferFormat &source, uint32_t *target);
  DisplayError SetStride(HWDeviceType device_type, LayerBufferFormat format,
                         uint32_t width, uint32_t *target);
  void SetBlending(const LayerBlending &source, mdss_mdp_blend_op *target);
  void SetRect(const LayerRect &source, mdp_rect *target);
  void SetMDPFlags(const Layer &layer, const bool &is_rotator_used, uint32_t *mdp_flags);
  void SyncMerge(const int &fd1, const int &fd2, int *target);

  // Retrieves HW FrameBuffer Node Index
  int GetFBNodeIndex(HWDeviceType device_type);
  // Populates HWPanelInfo based on node index
  void PopulateHWPanelInfo();
  void GetHWPanelInfoByNode(int device_node, HWPanelInfo *panel_info);
  HWDisplayPort GetHWDisplayPort(int device_node);
  HWDisplayMode GetHWDisplayMode(int device_node);
  void GetSplitInfo(int device_node, HWPanelInfo *panel_info);
  int ParseLine(char *input, char *tokens[], const uint32_t max_token, uint32_t *count);
  mdp_scale_data* GetScaleDataRef(uint32_t index) { return &scale_data_[index]; }
  void SetHWScaleData(const ScaleData &scale, uint32_t index);
  void ResetDisplayParams();
  void SetColorSpace(LayerColorSpace source, mdp_color_space *color_space);

  bool EnableHotPlugDetection(int enable);

  // Pointers to system calls which are either mapped to actual system call or virtual driver.
  int (*ioctl_)(int, int, ...);
  int (*open_)(const char *, int, ...);
  int (*close_)(int);
  int (*poll_)(struct pollfd *, nfds_t, int);
  ssize_t (*pread_)(int, void *, size_t, off_t);
  ssize_t (*pwrite_)(int, const void *, size_t, off_t);
  FILE* (*fopen_)( const char *fname, const char *mode);
  int (*fclose_)(FILE* fileptr);
  ssize_t (*getline_)(char **lineptr, size_t *linelen, FILE *stream);

  // Store the Device EventHandler - used for callback
  HWEventHandler *event_handler_;
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
  mdp_output_layer mdp_out_layer_;
  const char *device_name_;
  bool synchronous_commit_;
};

}  // namespace sdm

#endif  // __HW_DEVICE_H__

