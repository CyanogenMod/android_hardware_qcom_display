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

#ifndef __COMP_MANAGER_H__
#define __COMP_MANAGER_H__

#include <core/device_interface.h>

#include "hw_interface.h"
#include "strategy_default.h"
#include "res_manager.h"
#include "dump_impl.h"

namespace sde {

class CompManager : public DumpImpl {
 public:
  CompManager();
  DisplayError Init(const HWResourceInfo &hw_res_info_);
  DisplayError Deinit();
  DisplayError RegisterDevice(DeviceType type, const HWDeviceAttributes &attributes,
                              Handle *device);
  DisplayError UnregisterDevice(Handle device);
  DisplayError Prepare(Handle device, HWLayers *hw_layers);
  void PostPrepare(Handle device, HWLayers *hw_layers);
  void PostCommit(Handle device, HWLayers *hw_layers);
  void Purge(Handle device);

  // DumpImpl method
  virtual void AppendDump(char *buffer, uint32_t length);

 private:
  void PrepareStrategyConstraints(Handle device, HWLayers *hw_layers);
  struct CompManagerDevice {
    StrategyConstraints constraints;
    Handle res_mgr_device;
    DeviceType device_type;
  };

  Locker locker_;
  void *strategy_lib_;
  StrategyInterface *strategy_intf_;
  StrategyDefault strategy_default_;
  ResManager res_mgr_;
  uint64_t registered_displays_;        // Stores the bit mask of registered displays
  uint64_t configured_displays_;        // Stores the bit mask of sucessfully configured displays
  bool safe_mode_;                      // Flag to notify all displays to be in resource crunch
                                        // mode, where strategy manager chooses the best strategy
                                        // that uses optimal number of pipes for each display
};

}  // namespace sde

#endif  // __COMP_MANAGER_H__

