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

#include "strategy.h"

#define __CLASS__ "Strategy"

namespace sdm {

Strategy::Strategy(ExtensionInterface *extension_intf, DisplayType type,
                   const HWResourceInfo &hw_resource_info, const HWPanelInfo &hw_panel_info)
  : extension_intf_(extension_intf), strategy_intf_(NULL), partial_update_intf_(NULL),
    display_type_(type), hw_resource_info_(hw_resource_info), hw_panel_info_(hw_panel_info),
    hw_layers_info_(NULL), fb_layer_index_(0), extn_start_success_(false), tried_default_(false) {
}

DisplayError Strategy::Init() {
  DisplayError error = kErrorNone;

  if (extension_intf_) {
    error = extension_intf_->CreateStrategyExtn(display_type_, &strategy_intf_);
    if (error != kErrorNone) {
      DLOGE("Failed to create strategy");
      return error;
    }

    error = extension_intf_->CreatePartialUpdate(display_type_, hw_resource_info_,
                                                 hw_panel_info_, &partial_update_intf_);
    if (error != kErrorNone) {
      DLOGW("Partial Update creation failed, Continue without partial update.");
    }
  }

  return kErrorNone;
}

DisplayError Strategy::Deinit() {
  if (strategy_intf_) {
    if (partial_update_intf_) {
      extension_intf_->DestroyPartialUpdate(partial_update_intf_);
    }

    extension_intf_->DestroyStrategyExtn(strategy_intf_);
  }

  return kErrorNone;
}

DisplayError Strategy::Start(HWLayersInfo *hw_layers_info, uint32_t *max_attempts) {
  DisplayError error = kErrorNone;

  hw_layers_info_ = hw_layers_info;
  extn_start_success_ = false;
  tried_default_ = false;

  uint32_t i = 0;
  LayerStack *layer_stack = hw_layers_info_->stack;
  for (; i < layer_stack->layer_count; i++) {
    if (layer_stack->layers[i].composition == kCompositionGPUTarget) {
      fb_layer_index_ = i;
      break;
    }
  }

  if (i == layer_stack->layer_count) {
    return kErrorUndefined;
  }

  GenerateROI();

  if (strategy_intf_) {
    error = strategy_intf_->Start(hw_layers_info_, max_attempts);
    if (error == kErrorNone) {
      extn_start_success_ = true;
      return kErrorNone;
    }
  }

  *max_attempts = 1;

  return kErrorNone;
}

DisplayError Strategy::Stop() {
  if (extn_start_success_) {
    return strategy_intf_->Stop();
  }

  return kErrorNone;
}

DisplayError Strategy::GetNextStrategy(StrategyConstraints *constraints) {
  DisplayError error = kErrorNone;

  if (extn_start_success_) {
    error = strategy_intf_->GetNextStrategy(constraints);
    if (error == kErrorNone) {
      return kErrorNone;
    }
  }

  // Default composition is already tried.
  if (tried_default_) {
    return kErrorUndefined;
  }

  // Mark all application layers for GPU composition. Find GPU target buffer and store its index for
  // programming the hardware.
  LayerStack *layer_stack = hw_layers_info_->stack;
  uint32_t &hw_layer_count = hw_layers_info_->count;
  hw_layer_count = 0;

  for (uint32_t i = 0; i < layer_stack->layer_count; i++) {
    LayerComposition &composition = layer_stack->layers[i].composition;
    if (composition == kCompositionGPUTarget) {
      hw_layers_info_->index[hw_layer_count++] = i;
    } else if (composition != kCompositionBlitTarget) {
      composition = kCompositionGPU;
    }
  }

  tried_default_ = true;

  // There can be one and only one GPU target buffer.
  if (hw_layer_count != 1) {
    return kErrorParameters;
  }

  return kErrorNone;
}

void Strategy::GenerateROI() {
  bool split_display = false;

  if (partial_update_intf_ && partial_update_intf_->GenerateROI(hw_layers_info_) == kErrorNone) {
    return;
  }

  LayerStack *layer_stack = hw_layers_info_->stack;
  LayerRect &src_rect = layer_stack->layers[fb_layer_index_].src_rect;
  // TODO(user): read panels x_pixels and y_pixels instead of fb_x_res and fb_y_res
  float fb_x_res = src_rect.right - src_rect.left;
  float fb_y_res = src_rect.bottom - src_rect.top;

  if (!hw_resource_info_.is_src_split &&
     ((fb_x_res > hw_resource_info_.max_mixer_width) ||
     ((display_type_ == kPrimary) && hw_panel_info_.split_info.right_split))) {
    split_display = true;
  }

  if (split_display) {
    float left_split = FLOAT(hw_panel_info_.split_info.left_split);
    hw_layers_info_->left_partial_update = (LayerRect) {0.0f, 0.0f, left_split, fb_y_res};
    hw_layers_info_->right_partial_update = (LayerRect) {left_split, 0.0f, fb_x_res, fb_y_res};
  } else {
    hw_layers_info_->left_partial_update = (LayerRect) {0.0f, 0.0f, fb_x_res, fb_y_res};
    hw_layers_info_->right_partial_update = (LayerRect) {0.0f, 0.0f, 0.0f, 0.0f};
  }
}

}  // namespace sdm

