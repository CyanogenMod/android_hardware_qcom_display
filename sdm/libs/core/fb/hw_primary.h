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

#ifndef __HW_PRIMARY_H__
#define __HW_PRIMARY_H__

#include <sys/poll.h>
#include <vector>
#include <string>

#include "hw_device.h"

namespace sdm {
#define MAX_SYSFS_COMMAND_LENGTH 12
struct DisplayConfigVariableInfo;

class HWPrimary : public HWDevice {
 public:
  static DisplayError Create(HWInterface **intf, HWInfoInterface *hw_info_intf,
                             BufferSyncHandler *buffer_sync_handler, HWEventHandler *eventhandler);
  static DisplayError Destroy(HWInterface *intf);

 protected:
  HWPrimary(BufferSyncHandler *buffer_sync_handler, HWInfoInterface *hw_info_intf);
  virtual DisplayError Init(HWEventHandler *eventhandler);
  virtual DisplayError Deinit();
  virtual DisplayError GetNumDisplayAttributes(uint32_t *count);
  virtual DisplayError GetActiveConfig(uint32_t *active_config);
  virtual DisplayError GetDisplayAttributes(uint32_t index,
                                            HWDisplayAttributes *display_attributes);
  virtual DisplayError SetDisplayAttributes(uint32_t index);
  virtual DisplayError GetConfigIndex(uint32_t mode, uint32_t *index);
  virtual DisplayError PowerOff();
  virtual DisplayError Doze();
  virtual DisplayError DozeSuspend();
  virtual DisplayError Validate(HWLayers *hw_layers);
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);
  virtual DisplayError SetVSyncState(bool enable);
  virtual DisplayError SetDisplayMode(const HWDisplayMode hw_display_mode);
  virtual DisplayError SetRefreshRate(uint32_t refresh_rate);
  virtual DisplayError SetPanelBrightness(int level);
  virtual DisplayError GetPPFeaturesVersion(PPFeatureVersion *vers);
  virtual DisplayError SetPPFeatures(PPFeaturesConfig *feature_list);
  virtual DisplayError GetPanelBrightness(int *level);
  virtual DisplayError SetAutoRefresh(bool enable);

 private:
  // Panel modes for the MSMFB_LPM_ENABLE ioctl
  enum {
    kModeLPMVideo,
    kModeLPMCommand,
  };

  // Event Thread to receive vsync/blank events
  static void* DisplayEventThread(void *context);
  void* DisplayEventThreadHandler();

  void HandleVSync(char *data);
  void HandleBlank(char *data);
  void HandleIdleTimeout(char *data);
  void HandleThermal(char *data);
  DisplayError PopulateDisplayAttributes();
  void InitializeConfigs();
  bool IsResolutionSwitchEnabled() { return !display_configs_.empty(); }
  bool GetCurrentModeFromSysfs(size_t *curr_x_pixels, size_t *curr_y_pixels);

  pollfd poll_fds_[kNumDisplayEvents];
  pthread_t event_thread_;
  const char *event_thread_name_ = "SDM_EventThread";
  bool fake_vsync_ = false;
  bool exit_threads_ = false;
  HWDisplayAttributes display_attributes_;
  std::vector<DisplayConfigVariableInfo> display_configs_;
  std::vector<std::string> display_config_strings_;
  uint32_t active_config_index_ = 0;
  const char *kBrightnessNode = "/sys/class/leds/lcd-backlight/brightness";
  const char *kAutoRefreshNode = "/sys/devices/virtual/graphics/fb0/msm_cmd_autorefresh_en";
  bool auto_refresh_ = false;
};

}  // namespace sdm

#endif  // __HW_PRIMARY_H__

