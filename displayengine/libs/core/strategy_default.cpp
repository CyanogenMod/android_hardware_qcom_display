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

#include <utils/constants.h>
#include <utils/debug.h>

#include "strategy_default.h"

#define __CLASS__ "StrategyDefault"

namespace sde {

StrategyDefault::StrategyDefault(DisplayType type, const HWResourceInfo &hw_resource_info,
                                 const HWPanelInfo &hw_panel_info) : type_(type),
                                 hw_resource_info_(hw_resource_info),
                                 hw_panel_info_(hw_panel_info), hw_layers_info_(NULL) {
}

DisplayError StrategyDefault::CreateStrategyInterface(uint16_t version, DisplayType type,
                                                      const HWResourceInfo &hw_resource_info,
                                                      const HWPanelInfo &hw_panel_info,
                                                      StrategyInterface **interface) {
  StrategyDefault *strategy_default  = new StrategyDefault(type, hw_resource_info, hw_panel_info);

  if (!strategy_default) {
    return kErrorMemory;
  }

  *interface = strategy_default;

  return kErrorNone;
}

DisplayError StrategyDefault::DestroyStrategyInterface(StrategyInterface *interface) {
  StrategyDefault *strategy_default = static_cast<StrategyDefault *>(interface);

  if (!strategy_default) {
    return kErrorParameters;
  }

  delete strategy_default;

  return kErrorNone;
}

bool StrategyDefault::IsDisplaySplit(uint32_t fb_x_res) {
  if (fb_x_res > hw_resource_info_.max_mixer_width) {
    return true;
  }

  if ((type_ == kPrimary) && hw_panel_info_.split_info.right_split) {
    return true;
  }

  return false;
}

DisplayError StrategyDefault::Start(HWLayersInfo *hw_layers_info, uint32_t *max_attempts) {
  if (!hw_layers_info) {
    return kErrorParameters;
  }

  hw_layers_info_ = hw_layers_info;
  *max_attempts = 1;

  const LayerStack *layer_stack = hw_layers_info_->stack;
  for (uint32_t i = 0; i < layer_stack->layer_count; i++) {
    LayerComposition &composition = layer_stack->layers[i].composition;
    if (composition == kCompositionGPUTarget) {
      fb_layer_index_ = i;
      break;
    }
  }

  const LayerRect &src_rect = hw_layers_info_->stack->layers[fb_layer_index_].src_rect;
  // TODO(user): read panels x_pixels and y_pixels instead of fb_x_res and fb_y_res
  const float fb_x_res = src_rect.right - src_rect.left;
  const float fb_y_res = src_rect.bottom - src_rect.top;

  if (IsDisplaySplit(INT(fb_x_res))) {
    float left_split = FLOAT(hw_panel_info_.split_info.left_split);
    hw_layers_info->left_partial_update = (LayerRect) {0.0, 0.0, left_split, fb_y_res};
    hw_layers_info->right_partial_update = (LayerRect) {left_split, 0.0, fb_x_res, fb_y_res};
  } else {
    hw_layers_info->left_partial_update = (LayerRect) {0.0, 0.0, fb_x_res, fb_y_res};
    hw_layers_info->right_partial_update = (LayerRect) {0.0, 0.0, 0.0, 0.0};
  }

  return kErrorNone;
}

DisplayError StrategyDefault::Stop() {
  return kErrorNone;
}

DisplayError StrategyDefault::GetNextStrategy(StrategyConstraints *constraints) {
  // Mark all layers for GPU composition. Find GPU target buffer and store its index for programming
  // the hardware.
  LayerStack *layer_stack = hw_layers_info_->stack;
  uint32_t &hw_layer_count = hw_layers_info_->count;

  hw_layer_count = 0;
  for (uint32_t i = 0; i < layer_stack->layer_count; i++) {
    LayerComposition &composition = layer_stack->layers[i].composition;
    if (composition != kCompositionGPUTarget) {
      composition = kCompositionGPU;
    } else {
      hw_layers_info_->index[hw_layer_count++] = i;
    }
  }

  // There can be one and only one GPU target buffer.
  if (hw_layer_count != 1) {
    return kErrorParameters;
  }

  return kErrorNone;
}

}  // namespace sde

