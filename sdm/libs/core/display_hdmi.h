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

#ifndef __DISPLAY_HDMI_H__
#define __DISPLAY_HDMI_H__

#include <map>

#include "display_base.h"
#include "dump_impl.h"

namespace sdm {

class HWHDMIInterface;

class DisplayHDMI : public DisplayBase, DumpImpl {
 public:
  DisplayHDMI(DisplayEventHandler *event_handler, HWInfoInterface *hw_info_intf,
              BufferSyncHandler *buffer_sync_handler, CompManager *comp_manager,
              RotatorInterface *rotator_intf);
  virtual DisplayError Init();
  virtual DisplayError Deinit();
  virtual DisplayError Prepare(LayerStack *layer_stack);
  virtual DisplayError Commit(LayerStack *layer_stack);
  virtual DisplayError Flush();
  virtual DisplayError GetDisplayState(DisplayState *state);
  virtual DisplayError GetNumVariableInfoConfigs(uint32_t *count);
  virtual DisplayError GetConfig(uint32_t index, DisplayConfigVariableInfo *variable_info);
  virtual DisplayError GetActiveConfig(uint32_t *index);
  virtual DisplayError GetVSyncState(bool *enabled);
  virtual DisplayError SetDisplayState(DisplayState state);
  virtual DisplayError SetActiveConfig(DisplayConfigVariableInfo *variable_info);
  virtual DisplayError SetActiveConfig(uint32_t index);
  virtual DisplayError SetVSyncState(bool enable);
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);
  virtual DisplayError SetMaxMixerStages(uint32_t max_mixer_stages);
  virtual DisplayError SetDisplayMode(uint32_t mode);
  virtual DisplayError IsScalingValid(const LayerRect &crop, const LayerRect &dst, bool rotate90);
  virtual DisplayError GetRefreshRateRange(uint32_t *min_refresh_rate, uint32_t *max_refresh_rate);
  virtual DisplayError SetRefreshRate(uint32_t refresh_rate);
  virtual bool IsUnderscanSupported();
  virtual DisplayError SetPanelBrightness(int level);
  virtual DisplayError OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);
  virtual void AppendDump(char *buffer, uint32_t length);
  virtual DisplayError SetCursorPosition(int x, int y);

 private:
  virtual uint32_t GetBestConfig(HWS3DMode s3d_mode);
  virtual void GetScanSupport();
  virtual void SetS3DMode(LayerStack *layer_stack);

  Locker locker_;
  HWScanSupport scan_support_;
  std::map<LayerBufferS3DFormat, HWS3DMode> s3d_format_to_mode_;
};

}  // namespace sdm

#endif  // __DISPLAY_HDMI_H__

