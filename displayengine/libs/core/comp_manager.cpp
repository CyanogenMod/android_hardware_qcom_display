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

#include <dlfcn.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <core/buffer_allocator.h>

#include "comp_manager.h"
#include "strategy_default.h"

#define __CLASS__ "CompManager"

namespace sde {

CompManager::CompManager()
  : strategy_lib_(NULL), create_strategy_intf_(NULL), destroy_strategy_intf_(NULL),
    registered_displays_(0), configured_displays_(0), safe_mode_(false) {
}

DisplayError CompManager::Init(const HWResourceInfo &hw_res_info, BufferAllocator *buffer_allocator,
                               BufferSyncHandler *buffer_sync_handler) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  error = res_mgr_.Init(hw_res_info, buffer_allocator, buffer_sync_handler);
  if (error != kErrorNone) {
    return error;
  }

  // Try to load strategy library & get handle to its interface.
  // Default to GPU only composition on failure.
  strategy_lib_ = ::dlopen(STRATEGY_LIBRARY_NAME, RTLD_NOW);
  if (strategy_lib_) {
    void **create_sym = reinterpret_cast<void **>(&create_strategy_intf_);
    void **destroy_sym = reinterpret_cast<void **>(&destroy_strategy_intf_);

    *create_sym = ::dlsym(strategy_lib_, CREATE_STRATEGY_INTERFACE_NAME);
    *destroy_sym = ::dlsym(strategy_lib_, DESTROY_STRATEGY_INTERFACE_NAME);

    if (!create_strategy_intf_) {
      DLOGE("Unable to find symbol for %s", CREATE_STRATEGY_INTERFACE_NAME);
      error = kErrorUndefined;
    }

    if (!destroy_strategy_intf_) {
      DLOGE("Unable to find symbol for %s", DESTROY_STRATEGY_INTERFACE_NAME);
      error = kErrorUndefined;
    }

    if (error != kErrorNone) {
      ::dlclose(strategy_lib_);
      res_mgr_.Deinit();
    }
  } else {
    DLOGW("Unable to load = %s, using GPU only (default) composition", STRATEGY_LIBRARY_NAME);
    create_strategy_intf_ = StrategyDefault::CreateStrategyInterface;
    destroy_strategy_intf_ = StrategyDefault::DestroyStrategyInterface;
  }

  return error;
}

DisplayError CompManager::Deinit() {
  SCOPE_LOCK(locker_);

  if (strategy_lib_) {
    ::dlclose(strategy_lib_);
  }

  res_mgr_.Deinit();

  return kErrorNone;
}

DisplayError CompManager::RegisterDisplay(DisplayType type, const HWDisplayAttributes &attributes,
                                          Handle *display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  DisplayCompositionContext *display_comp_ctx = new DisplayCompositionContext();
  if (!display_comp_ctx) {
    return kErrorMemory;
  }

  if (create_strategy_intf_(STRATEGY_VERSION_TAG, type,
                            &display_comp_ctx->strategy_intf) != kErrorNone) {
    DLOGW("Unable to create strategy interface");
    delete display_comp_ctx;
    display_comp_ctx = NULL;
    return kErrorUndefined;
  }

  error = res_mgr_.RegisterDisplay(type, attributes, &display_comp_ctx->display_resource_ctx);
  if (error != kErrorNone) {
    destroy_strategy_intf_(display_comp_ctx->strategy_intf);
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

  res_mgr_.UnregisterDisplay(display_comp_ctx->display_resource_ctx);
  destroy_strategy_intf_(display_comp_ctx->strategy_intf);

  CLEAR_BIT(registered_displays_, display_comp_ctx->display_type);
  CLEAR_BIT(configured_displays_, display_comp_ctx->display_type);

  DLOGV_IF(kTagCompManager, "registered display bit mask 0x%x, configured display bit mask 0x%x, " \
           "display type %d", registered_displays_, configured_displays_,
           display_comp_ctx->display_type);

  if (display_comp_ctx) {
    delete display_comp_ctx;
    display_comp_ctx = NULL;
  }

  return kErrorNone;
}

void CompManager::PrepareStrategyConstraints(Handle comp_handle, HWLayers *hw_layers) {
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(comp_handle);
  StrategyConstraints *constraints = &display_comp_ctx->constraints;

  constraints->safe_mode = safe_mode_;

  // Limit 2 layer SDE Comp on HDMI
  if (display_comp_ctx->display_type == kHDMI) {
    constraints->max_layers = 2;
  }

  // If a strategy fails after successfully allocating resources, then set safe mode
  if (display_comp_ctx->remaining_strategies != display_comp_ctx->max_strategies) {
    constraints->safe_mode = true;
  }

  if (display_comp_ctx->idle_fallback) {
    constraints->safe_mode = true;
  }
}

void CompManager::PrePrepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  display_comp_ctx->strategy_intf->Start(&hw_layers->info,
                                         &display_comp_ctx->max_strategies);
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
  res_mgr_.Start(display_resource_ctx);

  bool exit = false;
  uint32_t &count = display_comp_ctx->remaining_strategies;
  for (; !exit && count > 0; count--) {
    error = display_comp_ctx->strategy_intf->GetNextStrategy(&display_comp_ctx->constraints);
    if (error != kErrorNone) {
      // Composition strategies exhausted. Resource Manager could not allocate resources even for
      // GPU composition. This will never happen.
      exit = true;
    }

    if (!exit) {
      error = res_mgr_.Acquire(display_resource_ctx, hw_layers);
      // Exit if successfully allocated resource, else try next strategy.
      exit = (error == kErrorNone);
    }
  }

  if (error != kErrorNone) {
    DLOGE("Composition strategies exhausted for display = %d", display_comp_ctx->display_type);
  }

  res_mgr_.Stop(display_resource_ctx);

  return error;
}

DisplayError CompManager::PostPrepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  Handle &display_resource_ctx = display_comp_ctx->display_resource_ctx;

  DisplayError error = kErrorNone;
  error = res_mgr_.PostPrepare(display_resource_ctx, hw_layers);
  if (error != kErrorNone) {
    return error;
  }

  display_comp_ctx->strategy_intf->Stop();

  return kErrorNone;
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

  error = res_mgr_.PostCommit(display_comp_ctx->display_resource_ctx, hw_layers);
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

  res_mgr_.Purge(display_comp_ctx->display_resource_ctx);
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

void CompManager::AppendDump(char *buffer, uint32_t length) {
  SCOPE_LOCK(locker_);
}

}  // namespace sde

