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

#ifndef __DISPLAY_BASE_H__
#define __DISPLAY_BASE_H__

#include <core/display_interface.h>
#include <private/strategy_interface.h>
#include <utils/locker.h>

#include "hw_interface.h"
#include "comp_manager.h"
#include "buffer_manager.h"


namespace sde {

class OfflineCtrl;

class DisplayBase : public DisplayInterface, HWEventHandler, DumpImpl {
 public:
  DisplayBase(DisplayType display_type, DisplayEventHandler *event_handler,
              HWDeviceType hw_device_type, HWInterface *hw_intf, CompManager *comp_manager,
              OfflineCtrl *offline_ctrl);
  virtual ~DisplayBase() { }
  virtual DisplayError Init();
  virtual DisplayError Deinit();
  virtual DisplayError Prepare(LayerStack *layer_stack);
  virtual DisplayError Commit(LayerStack *layer_stack);
  virtual DisplayError Flush();
  virtual DisplayError GetDisplayState(DisplayState *state);
  virtual DisplayError GetNumVariableInfoConfigs(uint32_t *count);
  virtual DisplayError GetConfig(DisplayConfigFixedInfo *fixed_info);
  virtual DisplayError GetConfig(uint32_t index, DisplayConfigVariableInfo *variable_info);
  virtual DisplayError GetActiveConfig(uint32_t *index);
  virtual DisplayError GetVSyncState(bool *enabled);
  virtual DisplayError SetDisplayState(DisplayState state);
  virtual DisplayError SetActiveConfig(DisplayConfigVariableInfo *variable_info);
  virtual DisplayError SetActiveConfig(uint32_t index);
  virtual DisplayError SetVSyncState(bool enable);
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);

  // Implement the HWEventHandlers
  virtual DisplayError VSync(int64_t timestamp);
  virtual DisplayError Blank(bool blank);
  virtual void IdleTimeout();

  // DumpImpl method
  virtual void AppendDump(char *buffer, uint32_t length);
  void AppendRect(char *buffer, uint32_t length, const char *rect_name, LayerRect *rect);

 protected:
  virtual int GetBestConfig();

  Locker locker_;
  DisplayType display_type_;
  DisplayEventHandler *event_handler_;
  HWDeviceType hw_device_type_;
  HWInterface *hw_intf_;
  CompManager *comp_manager_;
  OfflineCtrl *offline_ctrl_;
  DisplayState state_;
  Handle hw_device_;
  Handle display_comp_ctx_;
  Handle display_offline_ctx_;
  HWDisplayAttributes *display_attributes_;
  uint32_t num_modes_;
  uint32_t active_mode_index_;
  HWLayers hw_layers_;
  bool pending_commit_;
  bool vsync_enable_;
};

}  // namespace sde

#endif  // __DISPLAY_BASE_H__

