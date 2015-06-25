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

#include <utils/constants.h>
#include <utils/debug.h>
#include <core/buffer_allocator.h>

#include "comp_manager.h"
#include "strategy.h"

#define __CLASS__ "CompManager"

namespace sdm {

CompManager::CompManager()
  : resource_intf_(NULL), registered_displays_(0), configured_displays_(0), safe_mode_(false),
    extension_intf_(NULL) {
}

DisplayError CompManager::Init(const HWResourceInfo &hw_res_info,
                               ExtensionInterface *extension_intf) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  if (extension_intf) {
    error = extension_intf->CreateResourceExtn(hw_res_info, &resource_intf_);
  } else {
    resource_intf_ = &resource_default_;
    error = resource_default_.Init(hw_res_info);
  }

  if (error != kErrorNone) {
    return error;
  }

  hw_res_info_ = hw_res_info;
  extension_intf_ = extension_intf;

  return error;
}

DisplayError CompManager::Deinit() {
  SCOPE_LOCK(locker_);

  if (extension_intf_) {
    extension_intf_->DestroyResourceExtn(resource_intf_);
  } else {
    resource_default_.Deinit();
  }

  return kErrorNone;
}

DisplayError CompManager::RegisterDisplay(DisplayType type, const HWDisplayAttributes &attributes,
                                          const HWPanelInfo &hw_panel_info, Handle *display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  DisplayCompositionContext *display_comp_ctx = new DisplayCompositionContext();
  if (!display_comp_ctx) {
    return kErrorMemory;
  }

  Strategy *&strategy = display_comp_ctx->strategy;
  strategy = new Strategy(extension_intf_, type, hw_res_info_, hw_panel_info);
  if (!strategy) {
    DLOGE("Unable to create strategy");
    delete display_comp_ctx;
    return kErrorMemory;
  }

  error = strategy->Init();
  if (error != kErrorNone) {
    delete strategy;
    delete display_comp_ctx;
    return error;
  }

  error = resource_intf_->RegisterDisplay(type, attributes, hw_panel_info,
                                          &display_comp_ctx->display_resource_ctx);
  if (error != kErrorNone) {
    strategy->Deinit();
    delete strategy;
    delete display_comp_ctx;
    display_comp_ctx = NULL;
    return error;
  }

  SET_BIT(registered_displays_, type);
  display_comp_ctx->display_type = type;
  *display_ctx = display_comp_ctx;
  // New non-primary display device has been added, so move the composition mode to safe mode until
  // resources for the added display is configured properly.
  if (type != kPrimary) {
    safe_mode_ = true;
  }

  DLOGV_IF(kTagCompManager, "registered display bit mask 0x%x, configured display bit mask 0x%x, " \
           "display type %d", registered_displays_, configured_displays_,
           display_comp_ctx->display_type);

  return kErrorNone;
}

DisplayError CompManager::UnregisterDisplay(Handle comp_handle) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(comp_handle);

  if (!display_comp_ctx) {
    return kErrorParameters;
  }

  resource_intf_->UnregisterDisplay(display_comp_ctx->display_resource_ctx);

  Strategy *&strategy = display_comp_ctx->strategy;
  strategy->Deinit();
  delete strategy;

  CLEAR_BIT(registered_displays_, display_comp_ctx->display_type);
  CLEAR_BIT(configured_displays_, display_comp_ctx->display_type);

  DLOGV_IF(kTagCompManager, "registered display bit mask 0x%x, configured display bit mask 0x%x, " \
           "display type %d", registered_displays_, configured_displays_,
           display_comp_ctx->display_type);

  delete display_comp_ctx;
  display_comp_ctx = NULL;
  return kErrorNone;
}

void CompManager::ReconfigureDisplay(Handle comp_handle, const HWDisplayAttributes &attributes,
                                     const HWPanelInfo &hw_panel_info) {
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(comp_handle);

  resource_intf_->ReconfigureDisplay(display_comp_ctx->display_resource_ctx, attributes,
                                     hw_panel_info);

  // TODO(user): Need to reconfigure strategy with updated panel info
}

void CompManager::PrepareStrategyConstraints(Handle comp_handle, HWLayers *hw_layers) {
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(comp_handle);
  StrategyConstraints *constraints = &display_comp_ctx->constraints;

  constraints->safe_mode = safe_mode_;

  // Limit 2 layer SDE Comp on HDMI/Virtual
  if (display_comp_ctx->display_type != kPrimary) {
    constraints->max_layers = 2;
  }

  // If a strategy fails after successfully allocating resources, then set safe mode
  if (display_comp_ctx->remaining_strategies != display_comp_ctx->max_strategies) {
    constraints->safe_mode = true;
  }

  if (display_comp_ctx->idle_fallback || display_comp_ctx->fallback_) {
    constraints->safe_mode = true;
  }
}

void CompManager::PrePrepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  display_comp_ctx->strategy->Start(&hw_layers->info, &display_comp_ctx->max_strategies);
  display_comp_ctx->remaining_strategies = display_comp_ctx->max_strategies;

  // Avoid idle fallback, if there is only one app layer.
  // TODO(user): App layer count will change for hybrid composition
  uint32_t app_layer_count = hw_layers->info.stack->layer_count - 1;
  if (!display_comp_ctx->idle_fallback && app_layer_count > 1) {
    display_comp_ctx->handle_idle_timeout = true;
  }
}

DisplayError CompManager::Prepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  Handle &display_resource_ctx = display_comp_ctx->display_resource_ctx;

  DisplayError error = kErrorUndefined;

  PrepareStrategyConstraints(display_ctx, hw_layers);

  // Select a composition strategy, and try to allocate resources for it.
  resource_intf_->Start(display_resource_ctx);

  bool exit = false;
  uint32_t &count = display_comp_ctx->remaining_strategies;
  for (; !exit && count > 0; count--) {
    error = display_comp_ctx->strategy->GetNextStrategy(&display_comp_ctx->constraints);
    if (error != kErrorNone) {
      // Composition strategies exhausted. Resource Manager could not allocate resources even for
      // GPU composition. This will never happen.
      exit = true;
    }

    if (!exit) {
      error = resource_intf_->Acquire(display_resource_ctx, hw_layers);
      // Exit if successfully allocated resource, else try next strategy.
      exit = (error == kErrorNone);
    }
  }

  if (error != kErrorNone) {
    DLOGE("Composition strategies exhausted for display = %d", display_comp_ctx->display_type);
  }

  resource_intf_->Stop(display_resource_ctx);

  return error;
}

DisplayError CompManager::PostPrepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  Handle &display_resource_ctx = display_comp_ctx->display_resource_ctx;

  DisplayError error = kErrorNone;
  error = resource_intf_->PostPrepare(display_resource_ctx, hw_layers);
  if (error != kErrorNone) {
    return error;
  }

  display_comp_ctx->strategy->Stop();

  return kErrorNone;
}

DisplayError CompManager::ReConfigure(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  Handle &display_resource_ctx = display_comp_ctx->display_resource_ctx;

  DisplayError error = kErrorUndefined;
  resource_intf_->Start(display_resource_ctx);
  error = resource_intf_->Acquire(display_resource_ctx, hw_layers);

  if (error != kErrorNone) {
    DLOGE("Reconfigure failed for display = %d", display_comp_ctx->display_type);
  }

  resource_intf_->Stop(display_resource_ctx);
  if (error != kErrorNone) {
      error = resource_intf_->PostPrepare(display_resource_ctx, hw_layers);
  }

  return error;
}

DisplayError CompManager::PostCommit(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  SET_BIT(configured_displays_, display_comp_ctx->display_type);
  if (configured_displays_ == registered_displays_) {
      safe_mode_ = false;
  }

  error = resource_intf_->PostCommit(display_comp_ctx->display_resource_ctx, hw_layers);
  if (error != kErrorNone) {
    return error;
  }

  display_comp_ctx->idle_fallback = false;

  DLOGV_IF(kTagCompManager, "registered display bit mask 0x%x, configured display bit mask 0x%x, " \
           "display type %d", registered_displays_, configured_displays_,
           display_comp_ctx->display_type);

  return kErrorNone;
}

void CompManager::Purge(Handle display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  resource_intf_->Purge(display_comp_ctx->display_resource_ctx);
}

bool CompManager::ProcessIdleTimeout(Handle display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  if (!display_comp_ctx) {
    return false;
  }

  // 1. handle_idle_timeout flag is set to true on start of every draw call, if the current
  //    composition is not due to idle fallback.
  // 2. idle_fallback flag will be set only if handle_idle_timeout flag is true and there is no
  //    update to the screen for specified amount of time.
  // 3. handle_idle_timeout flag helps us handle the very first idle timeout event and
  //    ignore the next idle timeout event on consecutive two idle timeout events.
  if (display_comp_ctx->handle_idle_timeout) {
    display_comp_ctx->idle_fallback = true;
    display_comp_ctx->handle_idle_timeout = false;

    return true;
  }

  return false;
}

void CompManager::ProcessThermalEvent(Handle display_ctx, int64_t thermal_level) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
          reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  if (thermal_level >= kMaxThermalLevel) {
    display_comp_ctx->fallback_ = true;
  } else {
    display_comp_ctx->fallback_ = false;
  }
}

DisplayError CompManager::SetMaxMixerStages(Handle display_ctx, uint32_t max_mixer_stages) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  if (display_comp_ctx) {
    error = resource_intf_->SetMaxMixerStages(display_comp_ctx->display_resource_ctx,
                                              max_mixer_stages);
  }

  return error;
}

void CompManager::AppendDump(char *buffer, uint32_t length) {
  SCOPE_LOCK(locker_);
}

DisplayError CompManager::ValidateScaling(const LayerRect &crop, const LayerRect &dst,
                                          bool rotate90) {
  return resource_intf_->ValidateScaling(crop, dst, rotate90);
}

}  // namespace sdm

