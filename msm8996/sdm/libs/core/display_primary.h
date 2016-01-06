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

#ifndef __DISPLAY_PRIMARY_H__
#define __DISPLAY_PRIMARY_H__

#include "display_base.h"
#include "dump_impl.h"

namespace sdm {

class HWPrimaryInterface;

class DisplayPrimary : public DisplayBase, DumpImpl, HWEventHandler {
 public:
  DisplayPrimary(DisplayEventHandler *event_handler, HWInfoInterface *hw_info_intf,
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
  virtual void AppendDump(char *buffer, uint32_t length);
  virtual DisplayError SetCursorPosition(int x, int y);
  virtual DisplayError GetPanelBrightness(int *level);

  // Implement the HWEventHandlers
  virtual DisplayError VSync(int64_t timestamp);
  virtual DisplayError Blank(bool blank);
  virtual void IdleTimeout();
  virtual void ThermalEvent(int64_t thermal_level);

 private:
  Locker locker_;
  uint32_t idle_timeout_ms_ = 0;
};

}  // namespace sdm

#endif  // __DISPLAY_PRIMARY_H__

