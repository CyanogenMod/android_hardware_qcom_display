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

#include "hw_device.h"
#include "hw_primary_interface.h"

namespace sdm {

class HWPrimary : public HWDevice, public HWPrimaryInterface {
 public:
  HWPrimary(BufferSyncHandler *buffer_sync_handler, HWInfoInterface *hw_info_intf);
  virtual DisplayError Init();
  virtual DisplayError Deinit();
  virtual DisplayError Open(HWEventHandler *eventhandler);
  virtual DisplayError Close();
  virtual DisplayError GetNumDisplayAttributes(uint32_t *count);
  virtual DisplayError GetDisplayAttributes(HWDisplayAttributes *display_attributes,
                                            uint32_t index);
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
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);
  virtual DisplayError SetVSyncState(bool enable);
  virtual DisplayError SetDisplayMode(const HWDisplayMode hw_display_mode);
  virtual DisplayError SetRefreshRate(uint32_t refresh_rate);
  virtual DisplayError GetPPFeaturesVersion(PPFeatureVersion *vers);
  virtual DisplayError SetPPFeatures(PPFeaturesConfig &feature_list);

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

  pollfd poll_fds_[kNumDisplayEvents];
  pthread_t event_thread_;
  const char *event_thread_name_;
  bool fake_vsync_;
  bool exit_threads_;
  HWDisplayAttributes display_attributes_;
  bool config_changed_;
};

}  // namespace sdm

#endif  // __HW_PRIMARY_H__

