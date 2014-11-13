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

#ifndef __DEVICE_BASE_H__
#define __DEVICE_BASE_H__

#include <core/device_interface.h>
#include <private/strategy_interface.h>
#include <utils/locker.h>

#include "hw_interface.h"
#include "comp_manager.h"

namespace sde {

class DeviceBase : public DeviceInterface, HWEventHandler, DumpImpl {
 public:
  DeviceBase(DeviceType device_type, DeviceEventHandler *event_handler,
             HWBlockType hw_block_type, HWInterface *hw_intf, CompManager *comp_manager);
  virtual ~DeviceBase() { }
  virtual DisplayError Init();
  virtual DisplayError Deinit();
  virtual DisplayError Prepare(LayerStack *layer_stack);
  virtual DisplayError Commit(LayerStack *layer_stack);
  virtual DisplayError GetDeviceState(DeviceState *state);
  virtual DisplayError GetNumVariableInfoConfigs(uint32_t *count);
  virtual DisplayError GetConfig(DeviceConfigFixedInfo *fixed_info);
  virtual DisplayError GetConfig(DeviceConfigVariableInfo *variable_info, uint32_t mode);
  virtual DisplayError GetVSyncState(bool *enabled);
  virtual DisplayError SetDeviceState(DeviceState state);
  virtual DisplayError SetConfig(uint32_t mode);
  virtual DisplayError SetVSyncState(bool enable);

  // Implement the HWEventHandlers
  virtual DisplayError VSync(int64_t timestamp);
  virtual DisplayError Blank(bool blank);

  // DumpImpl method
  virtual void AppendDump(char *buffer, uint32_t length);
  void AppendRect(char *buffer, uint32_t length, const char *rect_name, LayerRect *rect);

 protected:
  Locker locker_;
  DeviceType device_type_;
  DeviceEventHandler *event_handler_;
  HWBlockType hw_block_type_;
  HWInterface *hw_intf_;
  CompManager *comp_manager_;
  DeviceState state_;
  Handle hw_device_;
  Handle comp_mgr_device_;
  HWDeviceAttributes *device_attributes_;
  uint32_t num_modes_;
  uint32_t active_mode_index_;
  HWLayers hw_layers_;
  bool pending_commit_;
  bool vsync_enable_;
};

}  // namespace sde

#endif  // __DEVICE_BASE_H__

