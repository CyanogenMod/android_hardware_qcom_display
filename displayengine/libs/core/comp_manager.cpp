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

// SDE_LOG_TAG definition must precede debug.h include.
#define SDE_LOG_TAG kTagCore
#define SDE_MODULE_NAME "CompManager"
#include <utils/debug.h>

#include <dlfcn.h>
#include <utils/constants.h>

#include "comp_manager.h"

namespace sde {

CompManager::CompManager() : strategy_lib_(NULL), strategy_intf_(NULL), registered_displays_(0),
                             configured_displays_(0), safe_mode_(false) {
}

DisplayError CompManager::Init(const HWResourceInfo &hw_res_info) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  error = res_mgr_.Init(hw_res_info);
  if (error != kErrorNone) {
    return error;
  }

  // Try to load strategy library & get handle to its interface.
  // Default to GPU only composition on failure.
  strategy_lib_ = ::dlopen(STRATEGY_LIBRARY_NAME, RTLD_NOW);
  if (!strategy_lib_) {
    DLOGW("Unable to load = %s", STRATEGY_LIBRARY_NAME);
  } else {
    CreateStrategyInterface create_strategy_intf = NULL;
    void **sym = reinterpret_cast<void **>(&create_strategy_intf);
    *sym = ::dlsym(strategy_lib_, CREATE_STRATEGY_INTERFACE_NAME);
    if (!create_strategy_intf) {
      DLOGW("Unable to find symbol for %s", CREATE_STRATEGY_INTERFACE_NAME);
    } else if (create_strategy_intf(STRATEGY_VERSION_TAG, &strategy_intf_) != kErrorNone) {
      DLOGW("Unable to create strategy interface");
    }
  }

  if (!strategy_intf_) {
    DLOGI("Using GPU only composition");
    if (strategy_lib_) {
      ::dlclose(strategy_lib_);
      strategy_lib_ = NULL;
    }
    strategy_intf_ = &strategy_default_;
  }

  return kErrorNone;
}

DisplayError CompManager::Deinit() {
  SCOPE_LOCK(locker_);

  if (strategy_lib_) {
    DestroyStrategyInterface destroy_strategy_intf = NULL;
    void **sym = reinterpret_cast<void **>(&destroy_strategy_intf);
    *sym = ::dlsym(strategy_lib_, DESTROY_STRATEGY_INTERFACE_NAME);
    if (!destroy_strategy_intf) {
      DLOGW("Unable to find symbol for %s", DESTROY_STRATEGY_INTERFACE_NAME);
    } else if (destroy_strategy_intf(strategy_intf_) != kErrorNone) {
      DLOGW("Unable to destroy strategy interface");
    }
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

  error = res_mgr_.RegisterDisplay(type, attributes, &display_comp_ctx->display_resource_ctx);
  if (error != kErrorNone) {
    delete display_comp_ctx;
    return error;
  }
  SET_BIT(registered_displays_, type);
  display_comp_ctx->display_type = type;
  *display_ctx = display_comp_ctx;
  // New display device has been added, so move the composition mode to safe mode until unless
  // resources for the added display is configured properly.
  safe_mode_ = true;

  return kErrorNone;
}

DisplayError CompManager::UnregisterDisplay(Handle comp_handle) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(comp_handle);

  res_mgr_.UnregisterDisplay(display_comp_ctx->display_resource_ctx);
  CLEAR_BIT(registered_displays_, display_comp_ctx->display_type);
  CLEAR_BIT(configured_displays_, display_comp_ctx->display_type);
  delete display_comp_ctx;

  return kErrorNone;
}

void CompManager::PrepareStrategyConstraints(Handle comp_handle, HWLayers *hw_layers) {
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(comp_handle);
  StrategyConstraints *constraints = &display_comp_ctx->constraints;

  constraints->safe_mode = safe_mode_;
  // If validation for the best available composition strategy with driver has failed, just
  // fallback to safe mode composition e.g. GPU or video only.
  if (UNLIKELY(hw_layers->info.flags)) {
    constraints->safe_mode = true;
    return;
  }
}

DisplayError CompManager::Prepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  Handle &display_resource_ctx = display_comp_ctx->display_resource_ctx;

  DisplayError error = kErrorNone;

  PrepareStrategyConstraints(display_ctx, hw_layers);

  // Select a composition strategy, and try to allocate resources for it.
  res_mgr_.Start(display_resource_ctx);
  while (true) {
    error = strategy_intf_->GetNextStrategy(&display_comp_ctx->constraints, &hw_layers->info);
    if (UNLIKELY(error != kErrorNone)) {
      // Composition strategies exhausted. Resource Manager could not allocate resources even for
      // GPU composition. This will never happen.
      DLOGE("Unexpected failure. Composition strategies exhausted.");
      return error;
    }

    error = res_mgr_.Acquire(display_resource_ctx, hw_layers);
    if (error != kErrorNone) {
      // Not enough resources, try next strategy.
      continue;
    } else {
      // Successfully selected and configured a composition strategy.
      break;
    }
  }
  res_mgr_.Stop(display_resource_ctx);

  return kErrorNone;
}

void CompManager::PostPrepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);
}

void CompManager::PostCommit(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  SET_BIT(configured_displays_, display_comp_ctx->display_type);
  if (configured_displays_ == registered_displays_) {
      safe_mode_ = false;
  }

  res_mgr_.PostCommit(display_comp_ctx->display_resource_ctx, hw_layers);
}

void CompManager::Purge(Handle display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  res_mgr_.Purge(display_comp_ctx->display_resource_ctx);
}

void CompManager::AppendDump(char *buffer, uint32_t length) {
  SCOPE_LOCK(locker_);
}

}  // namespace sde

