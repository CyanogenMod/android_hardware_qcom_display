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

#include "strategy.h"

#define __CLASS__ "Strategy"

namespace sdm {

Strategy::Strategy(ExtensionInterface *extension_intf, DisplayType type,
                   const HWResourceInfo &hw_resource_info, const HWPanelInfo &hw_panel_info,
                   const HWMixerAttributes &mixer_attributes,
                   const HWDisplayAttributes &display_attributes,
                   const DisplayConfigVariableInfo &fb_config)
  : extension_intf_(extension_intf), display_type_(type), hw_resource_info_(hw_resource_info),
    hw_panel_info_(hw_panel_info), mixer_attributes_(mixer_attributes),
    display_attributes_(display_attributes), fb_config_(fb_config) {
}

DisplayError Strategy::Init() {
  DisplayError error = kErrorNone;

  if (extension_intf_) {
    error = extension_intf_->CreateStrategyExtn(display_type_, hw_panel_info_.mode,
                                                hw_panel_info_.s3d_mode,
#ifndef SDM_LEGACY
                                                mixer_attributes_, fb_config_,
#endif
                                                &strategy_intf_);
    if (error != kErrorNone) {
      DLOGE("Failed to create strategy");
      return error;
    }

    error = extension_intf_->CreatePartialUpdate(display_type_, hw_resource_info_, hw_panel_info_,
#ifndef SDM_LEGACY
                                                 mixer_attributes_, display_attributes_,
#endif
                                                 &partial_update_intf_);
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

DisplayError Strategy::Start(HWLayersInfo *hw_layers_info, uint32_t *max_attempts,
                             bool partial_update_enable) {
  DisplayError error = kErrorNone;

  hw_layers_info_ = hw_layers_info;
  extn_start_success_ = false;
  tried_default_ = false;
  uint32_t i = 0;
  LayerStack *layer_stack = hw_layers_info_->stack;
  uint32_t layer_count = UINT32(layer_stack->layers.size());
  for (; i < layer_count; i++) {
    if (layer_stack->layers.at(i)->composition == kCompositionGPUTarget) {
      fb_layer_index_ = i;
      break;
    }
  }

  if (i == layer_count) {
    return kErrorUndefined;
  }

  if (partial_update_intf_) {
    partial_update_intf_->ControlPartialUpdate(partial_update_enable);
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
  if (strategy_intf_) {
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

  for (uint32_t i = 0; i < layer_stack->layers.size(); i++) {
    Layer *layer = layer_stack->layers.at(i);
    LayerComposition &composition = layer->composition;
    if (composition == kCompositionGPUTarget) {
      hw_layers_info_->updated_src_rect[hw_layer_count] = layer->src_rect;
      hw_layers_info_->updated_dst_rect[hw_layer_count] = layer->dst_rect;
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

  float layer_mixer_width = mixer_attributes_.width;
  float layer_mixer_height = mixer_attributes_.height;

  if (!hw_resource_info_.is_src_split &&
     ((layer_mixer_width > hw_resource_info_.max_mixer_width) ||
     ((hw_panel_info_.is_primary_panel) && hw_panel_info_.split_info.right_split))) {
    split_display = true;
  }

  if (split_display) {
    float left_split = FLOAT(mixer_attributes_.split_left);
    hw_layers_info_->left_partial_update = (LayerRect) {0.0f, 0.0f, left_split, layer_mixer_height};
    hw_layers_info_->right_partial_update = (LayerRect) {left_split, 0.0f, layer_mixer_width,
                                            layer_mixer_height};
  } else {
    hw_layers_info_->left_partial_update = (LayerRect) {0.0f, 0.0f, layer_mixer_width,
                                           layer_mixer_height};
    hw_layers_info_->right_partial_update = (LayerRect) {0.0f, 0.0f, 0.0f, 0.0f};
  }
}

DisplayError Strategy::Reconfigure(const HWPanelInfo &hw_panel_info,
                         const HWDisplayAttributes &display_attributes,
                         const HWMixerAttributes &mixer_attributes,
                         const DisplayConfigVariableInfo &fb_config) {
  hw_panel_info_ = hw_panel_info;
  display_attributes_ = display_attributes;
  mixer_attributes_ = mixer_attributes;

  if (!extension_intf_) {
    return kErrorNone;
  }

  // TODO(user): PU Intf will not be created for video mode panels, hence re-evaluate if
  // reconfigure is needed.
  if (partial_update_intf_) {
    extension_intf_->DestroyPartialUpdate(partial_update_intf_);
    partial_update_intf_ = NULL;
  }

  extension_intf_->CreatePartialUpdate(display_type_, hw_resource_info_, hw_panel_info_,
#ifndef SDM_LEGACY
                                       mixer_attributes_, display_attributes_,
#endif
                                       &partial_update_intf_);

  return strategy_intf_->Reconfigure(hw_panel_info_.mode, hw_panel_info_.s3d_mode, mixer_attributes,
                                     fb_config);
}

}  // namespace sdm
