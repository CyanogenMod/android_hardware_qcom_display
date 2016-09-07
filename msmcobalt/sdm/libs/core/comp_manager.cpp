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

#include <utils/constants.h>
#include <utils/debug.h>
#include <core/buffer_allocator.h>

#include "comp_manager.h"
#include "strategy.h"

#define __CLASS__ "CompManager"

namespace sdm {

DisplayError CompManager::Init(const HWResourceInfo &hw_res_info,
                               ExtensionInterface *extension_intf,
                               BufferSyncHandler *buffer_sync_handler) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  if (extension_intf) {
    error = extension_intf->CreateResourceExtn(hw_res_info, &resource_intf_, buffer_sync_handler);
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

DisplayError CompManager::RegisterDisplay(DisplayType type,
                                          const HWDisplayAttributes &display_attributes,
                                          const HWPanelInfo &hw_panel_info,
                                          const HWMixerAttributes &mixer_attributes,
                                          const DisplayConfigVariableInfo &fb_config,
                                          Handle *display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  DisplayCompositionContext *display_comp_ctx = new DisplayCompositionContext();
  if (!display_comp_ctx) {
    return kErrorMemory;
  }

  Strategy *&strategy = display_comp_ctx->strategy;
  strategy = new Strategy(extension_intf_, type, hw_res_info_, hw_panel_info, mixer_attributes,
                          display_attributes, fb_config);
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

  error = resource_intf_->RegisterDisplay(type, display_attributes, hw_panel_info, mixer_attributes,
                                          &display_comp_ctx->display_resource_ctx);
  if (error != kErrorNone) {
    strategy->Deinit();
    delete strategy;
    delete display_comp_ctx;
    display_comp_ctx = NULL;
    return error;
  }

  registered_displays_[type] = 1;
  display_comp_ctx->is_primary_panel = hw_panel_info.is_primary_panel;
  display_comp_ctx->display_type = type;
  *display_ctx = display_comp_ctx;
  // New non-primary display device has been added, so move the composition mode to safe mode until
  // resources for the added display is configured properly.
  if (!display_comp_ctx->is_primary_panel) {
    safe_mode_ = true;
  }

  DLOGV_IF(kTagCompManager, "registered display bit mask 0x%x, configured display bit mask 0x%x, " \
           "display type %d", registered_displays_.to_ulong(), configured_displays_.to_ulong(),
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

  registered_displays_[display_comp_ctx->display_type] = 0;
  configured_displays_[display_comp_ctx->display_type] = 0;

  if (display_comp_ctx->display_type == kHDMI) {
    max_layers_ = kMaxSDELayers;
  }

  DLOGV_IF(kTagCompManager, "registered display bit mask 0x%x, configured display bit mask 0x%x, " \
           "display type %d", registered_displays_.to_ulong(), configured_displays_.to_ulong(),
           display_comp_ctx->display_type);

  delete display_comp_ctx;
  display_comp_ctx = NULL;
  return kErrorNone;
}

DisplayError CompManager::ReconfigureDisplay(Handle comp_handle,
                                             const HWDisplayAttributes &display_attributes,
                                             const HWPanelInfo &hw_panel_info,
                                             const HWMixerAttributes &mixer_attributes,
                                             const DisplayConfigVariableInfo &fb_config) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(comp_handle);

  error = resource_intf_->ReconfigureDisplay(display_comp_ctx->display_resource_ctx,
                                             display_attributes, hw_panel_info, mixer_attributes);
  if (error != kErrorNone) {
    return error;
  }

  if (display_comp_ctx->strategy) {
    error = display_comp_ctx->strategy->Reconfigure(hw_panel_info, display_attributes,
                                                    mixer_attributes, fb_config);
    if (error != kErrorNone) {
      DLOGE("Unable to Reconfigure strategy.");
      display_comp_ctx->strategy->Deinit();
      delete display_comp_ctx->strategy;
      display_comp_ctx->strategy = NULL;
      return error;
    }
  }

  // For HDMI S3D mode, set max_layers_ to 0 so that primary display would fall back
  // to GPU composition to release pipes for HDMI.
  if (display_comp_ctx->display_type == kHDMI) {
    if (hw_panel_info.s3d_mode != kS3DModeNone) {
      max_layers_ = 0;
    } else {
      max_layers_ = kMaxSDELayers;
    }
  }

  return error;
}

void CompManager::PrepareStrategyConstraints(Handle comp_handle, HWLayers *hw_layers) {
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(comp_handle);
  StrategyConstraints *constraints = &display_comp_ctx->constraints;

  constraints->safe_mode = safe_mode_;
  constraints->use_cursor = false;
  constraints->max_layers = max_layers_;

  // Limit 2 layer SDE Comp if its not a Primary Display
  if (!display_comp_ctx->is_primary_panel) {
    constraints->max_layers = 2;
  }

  // If a strategy fails after successfully allocating resources, then set safe mode
  if (display_comp_ctx->remaining_strategies != display_comp_ctx->max_strategies) {
    constraints->safe_mode = true;
  }

  // Avoid idle fallback, if there is only one app layer.
  // TODO(user): App layer count will change for hybrid composition
  uint32_t app_layer_count = UINT32(hw_layers->info.stack->layers.size()) - 1;
  if ((app_layer_count > 1 && display_comp_ctx->idle_fallback) || display_comp_ctx->fallback_) {
    // Handle the idle timeout by falling back
    constraints->safe_mode = true;
  }

  if (SupportLayerAsCursor(comp_handle, hw_layers)) {
    constraints->use_cursor = true;
  }
}

void CompManager::PrePrepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  display_comp_ctx->strategy->Start(&hw_layers->info, &display_comp_ctx->max_strategies,
                                    display_comp_ctx->partial_update_enable);
  display_comp_ctx->remaining_strategies = display_comp_ctx->max_strategies;
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
  configured_displays_[display_comp_ctx->display_type] = 1;
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

void CompManager::ProcessIdleTimeout(Handle display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  if (!display_comp_ctx) {
    return;
  }

  display_comp_ctx->idle_fallback = true;
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

void CompManager::ControlPartialUpdate(Handle display_ctx, bool enable) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  display_comp_ctx->partial_update_enable = enable;
}

void CompManager::AppendDump(char *buffer, uint32_t length) {
  SCOPE_LOCK(locker_);
}

DisplayError CompManager::ValidateScaling(const LayerRect &crop, const LayerRect &dst,
                                          bool rotate90) {
  return resource_intf_->ValidateScaling(crop, dst, rotate90, Debug::IsUbwcTiledFrameBuffer(),
                                         true /* use_rotator_downscale */);
}

DisplayError CompManager::ValidateCursorPosition(Handle display_ctx, HWLayers *hw_layers,
                                                 int x, int y) {
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  Handle &display_resource_ctx = display_comp_ctx->display_resource_ctx;

  return resource_intf_->ValidateCursorPosition(display_resource_ctx, hw_layers, x, y);
}

bool CompManager::SupportLayerAsCursor(Handle comp_handle, HWLayers *hw_layers) {
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(comp_handle);
  Handle &display_resource_ctx = display_comp_ctx->display_resource_ctx;
  LayerStack *layer_stack = hw_layers->info.stack;
  bool supported = false;
  int32_t gpu_index = -1;

  if (!layer_stack->flags.cursor_present) {
    return supported;
  }

  for (int32_t i = INT32(layer_stack->layers.size() - 1); i >= 0; i--) {
    Layer *layer = layer_stack->layers.at(UINT32(i));
    if (layer->composition == kCompositionGPUTarget) {
      gpu_index = i;
      break;
    }
  }
  if (gpu_index <= 0) {
    return supported;
  }
  Layer *cursor_layer = layer_stack->layers.at(UINT32(gpu_index) - 1);
  if (cursor_layer->flags.cursor && resource_intf_->ValidateCursorConfig(display_resource_ctx,
                                    cursor_layer, true) == kErrorNone) {
    supported = true;
  }

  return supported;
}

DisplayError CompManager::SetMaxBandwidthMode(HWBwModes mode) {
  if ((hw_res_info_.has_dyn_bw_support == false) || (mode >= kBwModeMax)) {
    return kErrorNotSupported;
  }

  return resource_intf_->SetMaxBandwidthMode(mode);
}

bool CompManager::CanSetIdleTimeout(Handle display_ctx) {
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  if (!display_comp_ctx) {
    return false;
  }

  if (!display_comp_ctx->idle_fallback) {
    return true;
  }

  return false;
}

DisplayError CompManager::GetScaleLutConfig(HWScaleLutInfo *lut_info) {
  return resource_intf_->GetScaleLutConfig(lut_info);
}

DisplayError CompManager::SetDetailEnhancerData(Handle display_ctx,
                                                const DisplayDetailEnhancerData &de_data) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  return resource_intf_->SetDetailEnhancerData(display_comp_ctx->display_resource_ctx, de_data);
}

}  // namespace sdm

