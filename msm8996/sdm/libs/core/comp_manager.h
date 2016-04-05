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

#ifndef __COMP_MANAGER_H__
#define __COMP_MANAGER_H__

#include <core/display_interface.h>
#include <private/extension_interface.h>
#include <utils/locker.h>

#include "strategy.h"
#include "resource_default.h"
#include "hw_interface.h"
#include "dump_impl.h"

namespace sdm {

class CompManager : public DumpImpl {
 public:
  DisplayError Init(const HWResourceInfo &hw_res_info_, ExtensionInterface *extension_intf,
                  BufferSyncHandler *buffer_sync_handler);
  DisplayError Deinit();
  DisplayError RegisterDisplay(DisplayType type, const HWDisplayAttributes &attributes,
                               const HWPanelInfo &hw_panel_info, Handle *res_mgr_hnd);
  DisplayError UnregisterDisplay(Handle res_mgr_hnd);
  DisplayError ReconfigureDisplay(Handle display_ctx, const HWDisplayAttributes &attributes,
                                  const HWPanelInfo &hw_panel_info);
  void PrePrepare(Handle display_ctx, HWLayers *hw_layers);
  DisplayError Prepare(Handle display_ctx, HWLayers *hw_layers);
  DisplayError PostPrepare(Handle display_ctx, HWLayers *hw_layers);
  DisplayError ReConfigure(Handle display_ctx, HWLayers *hw_layers);
  DisplayError PostCommit(Handle display_ctx, HWLayers *hw_layers);
  void Purge(Handle display_ctx);
  void ProcessIdleTimeout(Handle display_ctx);
  void ProcessThermalEvent(Handle display_ctx, int64_t thermal_level);
  DisplayError SetMaxMixerStages(Handle display_ctx, uint32_t max_mixer_stages);
  void ControlPartialUpdate(Handle display_ctx, bool enable);
  DisplayError ValidateScaling(const LayerRect &crop, const LayerRect &dst, bool rotate90);
  DisplayError ValidateCursorPosition(Handle display_ctx, HWLayers *hw_layers, int x, int y);
  bool SupportLayerAsCursor(Handle display_ctx, HWLayers *hw_layers);
  bool CanSetIdleTimeout(Handle display_ctx);
  DisplayError SetMaxBandwidthMode(HWBwModes mode);

  // DumpImpl method
  virtual void AppendDump(char *buffer, uint32_t length);

 private:
  static const int kMaxThermalLevel = 3;

  void PrepareStrategyConstraints(Handle display_ctx, HWLayers *hw_layers);

  struct DisplayCompositionContext {
    Strategy *strategy = NULL;
    StrategyConstraints constraints;
    Handle display_resource_ctx = NULL;
    DisplayType display_type = kPrimary;
    uint32_t max_strategies = 0;
    uint32_t remaining_strategies = 0;
    bool idle_fallback = false;
    bool fallback_ = false;
    uint32_t partial_update_enable = true;
  };

  Locker locker_;
  ResourceInterface *resource_intf_ = NULL;
  ResourceDefault resource_default_;
  uint32_t registered_displays_ = 0;    // Stores the bit mask of registered displays
  uint32_t configured_displays_ = 0;    // Stores the bit mask of sucessfully configured displays
  bool safe_mode_ = false;              // Flag to notify all displays to be in resource crunch
                                        // mode, where strategy manager chooses the best strategy
                                        // that uses optimal number of pipes for each display
  HWResourceInfo hw_res_info_;
  ExtensionInterface *extension_intf_ = NULL;
  uint32_t max_layers_ = kMaxSDELayers;
};

}  // namespace sdm

#endif  // __COMP_MANAGER_H__

