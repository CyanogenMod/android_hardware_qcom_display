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
#include <private/rotator_interface.h>
#include <private/color_interface.h>
#include <utils/locker.h>

#include "hw_interface.h"
#include "comp_manager.h"
#include "color_manager.h"

namespace sdm {

class RotatorCtrl;
class HWInfoInterface;

class DisplayBase : public DisplayInterface {
 public:
  DisplayBase(DisplayType display_type, DisplayEventHandler *event_handler,
              HWDeviceType hw_device_type, BufferSyncHandler *buffer_sync_handler,
              CompManager *comp_manager, RotatorInterface *rotator_intf,
              HWInfoInterface *hw_info_intf);
  virtual ~DisplayBase() { }
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
  virtual DisplayError SetActiveConfig(uint32_t index);
  virtual DisplayError SetMaxMixerStages(uint32_t max_mixer_stages);
  virtual DisplayError ControlPartialUpdate(bool enable, uint32_t *pending);
  virtual DisplayError SetDisplayMode(uint32_t mode);
  virtual DisplayError IsScalingValid(const LayerRect &crop, const LayerRect &dst, bool rotate90);
  virtual bool IsUnderscanSupported();
  virtual DisplayError SetPanelBrightness(int level);
  virtual DisplayError OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);
  virtual DisplayError ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload,
                                            PPDisplayAPIPayload *out_payload,
                                            PPPendingParams *pending_action);
  virtual DisplayError ApplyDefaultDisplayMode(void);
  virtual DisplayError SetCursorPosition(int x, int y);
  virtual DisplayError GetRefreshRateRange(uint32_t *min_refresh_rate, uint32_t *max_refresh_rate);
  virtual DisplayError GetPanelBrightness(int *level);

 protected:
  // DumpImpl method
  void AppendDump(char *buffer, uint32_t length);

  bool IsRotationRequired(HWLayers *hw_layers);
  const char *GetName(const LayerComposition &composition);
  const char *GetName(const LayerBufferFormat &format);
  DisplayError ValidateGPUTarget(LayerStack *layer_stack);

  DisplayType display_type_;
  DisplayEventHandler *event_handler_ = NULL;
  HWDeviceType hw_device_type_;
  HWInterface *hw_intf_ = NULL;
  HWPanelInfo hw_panel_info_;
  BufferSyncHandler *buffer_sync_handler_ = NULL;
  CompManager *comp_manager_ = NULL;
  RotatorInterface *rotator_intf_ = NULL;
  DisplayState state_ = kStateOff;
  bool active_ = false;
  Handle hw_device_ = 0;
  Handle display_comp_ctx_ = 0;
  Handle display_rotator_ctx_ = 0;
  HWLayers hw_layers_;
  bool pending_commit_ = false;
  bool vsync_enable_ = false;
  bool underscan_supported_ = false;
  uint32_t max_mixer_stages_ = 0;
  HWInfoInterface *hw_info_intf_ = NULL;
  ColorManagerProxy *color_mgr_ = NULL;  // each display object owns its ColorManagerProxy
  bool partial_update_control_ = true;

 private:
  // Unused
  virtual DisplayError GetConfig(DisplayConfigFixedInfo *variable_info) {
    return kErrorNone;
  }
};

}  // namespace sdm

#endif  // __DISPLAY_BASE_H__
