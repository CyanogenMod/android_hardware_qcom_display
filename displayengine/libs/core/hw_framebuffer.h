/*
* Copyright (c) 2014, The Linux Foundation. All rights reserved.
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

#ifndef __HW_FRAMEBUFFER_H__
#define __HW_FRAMEBUFFER_H__

#include <stdio.h>
#include <stdlib.h>
#include <linux/msm_mdp_ext.h>
#include <video/msm_hdmi_modes.h>

#include <poll.h>
#include <pthread.h>

#include "hw_interface.h"

namespace sde {

class HWFrameBuffer : public HWInterface {
 public:
  HWFrameBuffer();
  DisplayError Init();
  DisplayError Deinit();
  virtual DisplayError GetHWCapabilities(HWResourceInfo *hw_res_info);
  virtual DisplayError Open(HWDeviceType type, Handle *device, HWEventHandler *eventhandler);
  virtual DisplayError Close(Handle device);
  virtual DisplayError GetNumDisplayAttributes(Handle device, uint32_t *count);
  virtual DisplayError GetDisplayAttributes(Handle device, HWDisplayAttributes *display_attributes,
                                            uint32_t index);
  virtual DisplayError SetDisplayAttributes(Handle device, uint32_t index);
  virtual DisplayError PowerOn(Handle device);
  virtual DisplayError PowerOff(Handle device);
  virtual DisplayError Doze(Handle device);
  virtual DisplayError SetVSyncState(Handle device, bool enable);
  virtual DisplayError Standby(Handle device);
  virtual DisplayError Validate(Handle device, HWLayers *hw_layers);
  virtual DisplayError Commit(Handle device, HWLayers *hw_layers);

 private:
  struct HWContext {
    HWDeviceType type;
    int device_fd;
    mdp_layer_commit mdp_commit;
    mdp_input_layer mdp_layers[kMaxSDELayers * 2];   // split panel (left + right) for worst case

    HWContext() : type(kDeviceMax), device_fd(-1) {
      ResetMDPCommit();
    }

    void ResetMDPCommit() {
      memset(&mdp_commit, 0, sizeof(mdp_commit));
      memset(&mdp_layers, 0, sizeof(mdp_layers));
      mdp_commit.version = MDP_COMMIT_VERSION_1_0;
      mdp_commit.commit_v1.input_layers = mdp_layers;
    }
  };

  enum PanelType {
    kNoPanel,
    kCommandModePanel,
    kVideoModePanel,
    kDTvPanel,
    kWriteBackPanel,
    kLVDSPanel,
    kEDPPanel,
  };

  enum {
    kHWEventVSync,
    kHWEventBlank,
  };

  // Maps to the msm_fb_panel_info
  struct PanelInfo {
    PanelType type;        // Smart or Dumb
    bool partial_update;   // Partial update feature
    int left_align;        // ROI left alignment restriction
    int width_align;       // ROI width alignment restriction
    int top_align;;        // ROI top alignment restriction
    int height_align;      // ROI height alignment restriction
    int min_roi_width;     // Min width needed for ROI
    int min_roi_height;    // Min height needed for ROI
    bool needs_roi_merge;  // Merge ROI's of both the DSI's
    bool dynamic_fps;      // Panel Supports dynamic fps
    uint32_t min_fps;      // Min fps supported by panel
    uint32_t max_fps;      // Max fps supported by panel

    PanelInfo() : type(kNoPanel), partial_update(false), left_align(false), width_align(false),
      top_align(false), height_align(false), min_roi_width(0), min_roi_height(0),
      needs_roi_merge(false), dynamic_fps(false), min_fps(0), max_fps(0) { }
  };

  static const int kMaxStringLength = 1024;
  static const int kNumPhysicalDisplays = 2;
  static const int kNumDisplayEvents = 2;
  static const int kHWMdssVersion5 = 500;  // MDSS_V5

  inline DisplayError SetFormat(const LayerBufferFormat &source, uint32_t *target);
  inline void SetBlending(const LayerBlending &source, mdss_mdp_blend_op *target);
  inline void SetRect(const LayerRect &source, mdp_rect *target);

  // Event Thread to receive vsync/blank events
  static void* DisplayEventThread(void *context);
  void* DisplayEventThreadHandler();

  void HandleVSync(int display_id, char *data);
  void HandleBlank(int display_id, char *data);

  // Populates HW FrameBuffer Node Index
  void PopulateFBNodeIndex();
  // Populates the Panel Info based on node index
  void PopulatePanelInfo(int fb_index);
  // Populates HW Capabilities
  DisplayError PopulateHWCapabilities();
  int ParseLine(char *input, char *token[], const uint32_t max_token, uint32_t *count);

  // HDMI Related Functions
  bool EnableHotPlugDetection(int enable);
  int GetHDMIModeCount();
  bool MapHDMIDisplayTiming(const msm_hdmi_mode_timing_info *mode, fb_var_screeninfo *info);
  void ResetHDMIModes();

  // Pointers to system calls which are either mapped to actual system call or virtual driver.
  int (*ioctl_)(int, int, ...);
  int (*open_)(const char *, int, ...);
  int (*close_)(int);
  int (*poll_)(struct pollfd *, nfds_t, int);
  ssize_t (*pread_)(int, void *, size_t, off_t);
  FILE* (*fopen_)( const char *fname, const char *mode);
  int (*fclose_)(FILE* fileptr);
  ssize_t (*getline_)(char **lineptr, size_t *linelen, FILE *stream);
  ssize_t (*read_)(int fd, void *buf, size_t count);
  ssize_t (*write_)(int fd, const void *buf, size_t count);


  // Store the Device EventHandlers - used for callback
  HWEventHandler *event_handler_[kNumPhysicalDisplays];
  pollfd poll_fds_[kNumPhysicalDisplays][kNumDisplayEvents];
  pthread_t event_thread_;
  const char *event_thread_name_;
  bool fake_vsync_;
  bool exit_threads_;
  HWResourceInfo hw_resource_;
  int fb_node_index_[kDeviceMax];
  const char* fb_path_;
  PanelInfo pri_panel_info_;
  bool hotplug_enabled_;
  uint32_t hdmi_mode_count_;
  uint32_t hdmi_modes_[256];
  // Holds the hdmi timing information. Ex: resolution, fps etc.,
  msm_hdmi_mode_timing_info *supported_video_modes_;
};

}  // namespace sde

#endif  // __HW_FRAMEBUFFER_H__

