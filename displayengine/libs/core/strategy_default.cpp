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
#define SDE_MODULE_NAME "StrategyDefault"
#include <utils/debug.h>

#include <utils/constants.h>

#include "strategy_default.h"

namespace sde {

DisplayError StrategyDefault::GetNextStrategy(StrategyConstraints *constraints,
                                              HWLayersInfo *hw_layers_info) {
  // Mark all layers for GPU composition. Find GPU target buffer and store its index for programming
  // the hardware.
  LayerStack *layer_stack = hw_layers_info->stack;
  uint32_t &hw_layer_count = hw_layers_info->count;

  hw_layer_count = 0;
  for (uint32_t i = 0; i < layer_stack->layer_count; i++) {
    LayerComposition &composition = layer_stack->layers[i].composition;
    if (composition != kCompositionGPUTarget) {
      composition = kCompositionGPU;
    } else {
      hw_layers_info->index[hw_layer_count++] = i;
    }
  }

  // There can be one and only one GPU target buffer.
  if (hw_layer_count != 1) {
    return kErrorParameters;
  }

  hw_layers_info->flags |= kFlagGPU;

  return kErrorNone;
}

}  // namespace sde

