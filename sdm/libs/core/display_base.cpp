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

#include <stdio.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/rect.h>

#include "display_base.h"
#include "hw_info_interface.h"

#define __CLASS__ "DisplayBase"

namespace sdm {

// TODO(user): Have a single structure handle carries all the interface pointers and variables.
DisplayBase::DisplayBase(DisplayType display_type, DisplayEventHandler *event_handler,
                         HWDeviceType hw_device_type, BufferSyncHandler *buffer_sync_handler,
                         CompManager *comp_manager, RotatorInterface *rotator_intf,
                         HWInfoInterface *hw_info_intf)
  : display_type_(display_type), event_handler_(event_handler), hw_device_type_(hw_device_type),
    buffer_sync_handler_(buffer_sync_handler), comp_manager_(comp_manager),
    rotator_intf_(rotator_intf), hw_info_intf_(hw_info_intf) {
}

DisplayError DisplayBase::Init() {
  DisplayError error = kErrorNone;
  hw_panel_info_ = HWPanelInfo();
  hw_intf_->GetHWPanelInfo(&hw_panel_info_);

  HWDisplayAttributes display_attrib;
  uint32_t active_index = 0;
  hw_intf_->GetActiveConfig(&active_index);
  hw_intf_->GetDisplayAttributes(active_index, &display_attrib);

  error = comp_manager_->RegisterDisplay(display_type_, display_attrib,
                                         hw_panel_info_, &display_comp_ctx_);
  if (error != kErrorNone) {
    goto CleanupOnError;
  }

  if (rotator_intf_) {
    error = rotator_intf_->RegisterDisplay(display_type_, &display_rotator_ctx_);
    if (error != kErrorNone) {
      goto CleanupOnError;
    }
  }

  if (hw_info_intf_) {
    HWResourceInfo hw_resource_info = HWResourceInfo();
    hw_info_intf_->GetHWResourceInfo(&hw_resource_info);
    auto max_mixer_stages = hw_resource_info.num_blending_stages;
    int property_value = Debug::GetMaxPipesPerMixer(display_type_);
    if (property_value >= 0) {
      max_mixer_stages = MIN(UINT32(property_value), hw_resource_info.num_blending_stages);
    }
    DisplayBase::SetMaxMixerStages(max_mixer_stages);
  }

  color_mgr_ = ColorManagerProxy::CreateColorManagerProxy(display_type_, hw_intf_,
                               display_attrib, hw_panel_info_);
  if (!color_mgr_) {
    DLOGW("Unable to create ColorManagerProxy for display = %d", display_type_);
  }

  return kErrorNone;

CleanupOnError:
  if (display_comp_ctx_) {
    comp_manager_->UnregisterDisplay(display_comp_ctx_);
  }

  return error;
}

DisplayError DisplayBase::Deinit() {
  if (rotator_intf_) {
    rotator_intf_->UnregisterDisplay(display_rotator_ctx_);
  }

  if (color_mgr_) {
    delete color_mgr_;
    color_mgr_ = NULL;
  }

  comp_manager_->UnregisterDisplay(display_comp_ctx_);

  return kErrorNone;
}

DisplayError DisplayBase::ValidateGPUTarget(LayerStack *layer_stack) {
  uint32_t i = 0;
  Layer *layers = layer_stack->layers;

  // TODO(user): Remove this check once we have query display attributes on virtual display
  if (display_type_ == kVirtual) {
    return kErrorNone;
  }

  while (i < layer_stack->layer_count && (layers[i].composition != kCompositionGPUTarget)) {
    i++;
  }

  if (i >= layer_stack->layer_count) {
    DLOGE("Either layer count is zero or GPU target layer is not present");
    return kErrorParameters;
  }

  uint32_t gpu_target_index = i;

  // Check GPU target layer
  Layer &gpu_target_layer = layer_stack->layers[gpu_target_index];

  if (!IsValid(gpu_target_layer.src_rect)) {
    DLOGE("Invalid src rect for GPU target layer");
    return kErrorParameters;
  }

  if (!IsValid(gpu_target_layer.dst_rect)) {
    DLOGE("Invalid dst rect for GPU target layer");
    return kErrorParameters;
  }

  auto gpu_target_layer_dst_xpixels = gpu_target_layer.dst_rect.right;
  auto gpu_target_layer_dst_ypixels = gpu_target_layer.dst_rect.bottom;

  HWDisplayAttributes display_attrib;
  uint32_t active_index = 0;
  hw_intf_->GetActiveConfig(&active_index);
  hw_intf_->GetDisplayAttributes(active_index, &display_attrib);

  if (gpu_target_layer_dst_xpixels > display_attrib.x_pixels ||
    gpu_target_layer_dst_ypixels > display_attrib.y_pixels) {
    DLOGE("GPU target layer dst rect is not with in limits");
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError DisplayBase::Prepare(LayerStack *layer_stack) {
  DisplayError error = kErrorNone;
  bool disable_partial_update = false;
  uint32_t pending = 0;

  if (!layer_stack) {
    return kErrorParameters;
  }

  pending_commit_ = false;

  error = ValidateGPUTarget(layer_stack);
  if (error != kErrorNone) {
    return error;
  }

  if (!active_) {
    return kErrorPermission;
  }

  // Request to disable partial update only if it is currently enabled.
  if (color_mgr_ && partial_update_control_) {
    disable_partial_update = color_mgr_->NeedsPartialUpdateDisable();
    if (disable_partial_update) {
      ControlPartialUpdate(false, &pending);
    }
  }

  // Clean hw layers for reuse.
  hw_layers_ = HWLayers();
  hw_layers_.info.stack = layer_stack;
  hw_layers_.output_compression = 1.0f;

  comp_manager_->PrePrepare(display_comp_ctx_, &hw_layers_);
  while (true) {
    error = comp_manager_->Prepare(display_comp_ctx_, &hw_layers_);
    if (error != kErrorNone) {
      break;
    }

    if (IsRotationRequired(&hw_layers_)) {
      if (!rotator_intf_) {
        continue;
      }
      error = rotator_intf_->Prepare(display_rotator_ctx_, &hw_layers_);
    } else {
      // Release all the previous rotator sessions.
      if (rotator_intf_) {
        error = rotator_intf_->Purge(display_rotator_ctx_);
      }
    }

    if (error == kErrorNone) {
      error = hw_intf_->Validate(&hw_layers_);
      if (error == kErrorNone) {
        // Strategy is successful now, wait for Commit().
        pending_commit_ = true;
        break;
      }
      if (error == kErrorShutDown) {
        comp_manager_->PostPrepare(display_comp_ctx_, &hw_layers_);
        return error;
      }
    }
  }

  comp_manager_->PostPrepare(display_comp_ctx_, &hw_layers_);
  if (disable_partial_update) {
    ControlPartialUpdate(true, &pending);
  }

  return error;
}

DisplayError DisplayBase::Commit(LayerStack *layer_stack) {
  DisplayError error = kErrorNone;

  if (!layer_stack) {
    return kErrorParameters;
  }

  if (!active_) {
    return kErrorPermission;
  }

  if (!pending_commit_) {
    DLOGE("Commit: Corresponding Prepare() is not called for display = %d", display_type_);
    return kErrorUndefined;
  }

  pending_commit_ = false;

  // Layer stack attributes has changed, need to Reconfigure, currently in use for Hybrid Comp
  if (layer_stack->flags.attributes_changed) {
    error = comp_manager_->ReConfigure(display_comp_ctx_, &hw_layers_);
    if (error != kErrorNone) {
      return error;
    }

    error = hw_intf_->Validate(&hw_layers_);
    if (error != kErrorNone) {
        return error;
    }
  }

  if (rotator_intf_ && IsRotationRequired(&hw_layers_)) {
    error = rotator_intf_->Commit(display_rotator_ctx_, &hw_layers_);
    if (error != kErrorNone) {
      return error;
    }
  }

  // check if feature list cache is dirty and pending.
  // If dirty, need program to hardware blocks.
  if (color_mgr_)
    error = color_mgr_->Commit();
  if (error != kErrorNone) {  // won't affect this execution path.
    DLOGW("ColorManager::Commit(...) isn't working");
  }

  error = hw_intf_->Commit(&hw_layers_);
  if (error != kErrorNone) {
    return error;
  }

  if (rotator_intf_ && IsRotationRequired(&hw_layers_)) {
    error = rotator_intf_->PostCommit(display_rotator_ctx_, &hw_layers_);
    if (error != kErrorNone) {
      return error;
    }
  }

  error = comp_manager_->PostCommit(display_comp_ctx_, &hw_layers_);
  if (error != kErrorNone) {
    return error;
  }

  return kErrorNone;
}

DisplayError DisplayBase::Flush() {
  DisplayError error = kErrorNone;

  if (!active_) {
    return kErrorPermission;
  }

  hw_layers_.info.count = 0;
  error = hw_intf_->Flush();
  if (error == kErrorNone) {
    // Release all the rotator sessions.
    if (rotator_intf_) {
      error = rotator_intf_->Purge(display_rotator_ctx_);
      if (error != kErrorNone) {
        DLOGE("Rotator purge failed for display %d", display_type_);
        return error;
      }
    }

    comp_manager_->Purge(display_comp_ctx_);

    pending_commit_ = false;
  } else {
    DLOGW("Unable to flush display = %d", display_type_);
  }

  return error;
}

DisplayError DisplayBase::GetDisplayState(DisplayState *state) {
  if (!state) {
    return kErrorParameters;
  }

  *state = state_;
  return kErrorNone;
}

DisplayError DisplayBase::GetNumVariableInfoConfigs(uint32_t *count) {
  return hw_intf_->GetNumDisplayAttributes(count);
}

DisplayError DisplayBase::GetConfig(uint32_t index, DisplayConfigVariableInfo *variable_info) {
  HWDisplayAttributes attrib;
  if (hw_intf_->GetDisplayAttributes(index, &attrib) == kErrorNone) {
    *variable_info = attrib;
    return kErrorNone;
  }

  return kErrorNotSupported;
}

DisplayError DisplayBase::GetActiveConfig(uint32_t *index) {
  return hw_intf_->GetActiveConfig(index);
}

DisplayError DisplayBase::GetVSyncState(bool *enabled) {
  if (!enabled) {
    return kErrorParameters;
  }

  *enabled = vsync_enable_;

  return kErrorNone;
}

bool DisplayBase::IsUnderscanSupported() {
  return underscan_supported_;
}

DisplayError DisplayBase::SetDisplayState(DisplayState state) {
  DisplayError error = kErrorNone;
  bool active = false;

  DLOGI("Set state = %d, display %d", state, display_type_);

  if (state == state_) {
    DLOGI("Same state transition is requested.");
    return kErrorNone;
  }

  switch (state) {
  case kStateOff:
    hw_layers_.info.count = 0;
    error = hw_intf_->Flush();
    if (error == kErrorNone) {
      // Release all the rotator sessions.
      if (rotator_intf_) {
        error = rotator_intf_->Purge(display_rotator_ctx_);
        if (error != kErrorNone) {
          DLOGE("Rotator purge failed for display %d", display_type_);
          return error;
        }
      }

      comp_manager_->Purge(display_comp_ctx_);

      error = hw_intf_->PowerOff();
    }
    break;

  case kStateOn:
    error = hw_intf_->PowerOn();
    active = true;
    break;

  case kStateDoze:
    error = hw_intf_->Doze();
    active = true;
    break;

  case kStateDozeSuspend:
    error = hw_intf_->DozeSuspend();
    break;

  case kStateStandby:
    error = hw_intf_->Standby();
    break;

  default:
    DLOGE("Spurious state = %d transition requested.", state);
    break;
  }

  if (error == kErrorNone) {
    active_ = active;
    state_ = state;
  }

  return error;
}

DisplayError DisplayBase::SetActiveConfig(uint32_t index) {
  DisplayError error = kErrorNone;
  uint32_t active_index = 0;

  hw_intf_->GetActiveConfig(&active_index);

  if (active_index == index) {
    return kErrorNone;
  }

  error = hw_intf_->SetDisplayAttributes(index);
  if (error != kErrorNone) {
    return error;
  }

  HWDisplayAttributes attrib;
  error = hw_intf_->GetDisplayAttributes(index, &attrib);
  if (error != kErrorNone) {
    return error;
  }

  if (display_comp_ctx_) {
    comp_manager_->UnregisterDisplay(display_comp_ctx_);
  }

  error = comp_manager_->RegisterDisplay(display_type_, attrib, hw_panel_info_,
                                         &display_comp_ctx_);

  return error;
}

DisplayError DisplayBase::SetMaxMixerStages(uint32_t max_mixer_stages) {
  DisplayError error = kErrorNone;

  error = comp_manager_->SetMaxMixerStages(display_comp_ctx_, max_mixer_stages);

  if (error == kErrorNone) {
    max_mixer_stages_ = max_mixer_stages;
  }

  return error;
}

DisplayError DisplayBase::ControlPartialUpdate(bool enable, uint32_t *pending) {
  if (!pending) {
    return kErrorParameters;
  }

  if (!hw_panel_info_.partial_update) {
    // Nothing to be done.
    DLOGI("partial update is not applicable for display=%d", display_type_);
    return kErrorNotSupported;
  }

  *pending = 0;
  if (enable == partial_update_control_) {
    DLOGI("Same state transition is requested.");
    return kErrorNone;
  }

  partial_update_control_ = enable;
  comp_manager_->ControlPartialUpdate(display_comp_ctx_, enable);

  if (!enable) {
    // If the request is to turn off feature, new draw call is required to have
    // the new setting into effect.
    *pending = 1;
  }

  return kErrorNone;
}

DisplayError DisplayBase::SetDisplayMode(uint32_t mode) {
  return kErrorNotSupported;
}

DisplayError DisplayBase::IsScalingValid(const LayerRect &crop, const LayerRect &dst,
                                         bool rotate90) {
  return comp_manager_->ValidateScaling(crop, dst, rotate90);
}

DisplayError DisplayBase::SetPanelBrightness(int level) {
  return kErrorNotSupported;
}

DisplayError DisplayBase::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  return kErrorNotSupported;
}

void DisplayBase::AppendDump(char *buffer, uint32_t length) {
  HWDisplayAttributes attrib;
  uint32_t active_index = 0;
  uint32_t num_modes = 0;
  hw_intf_->GetNumDisplayAttributes(&num_modes);
  hw_intf_->GetActiveConfig(&active_index);
  hw_intf_->GetDisplayAttributes(active_index, &attrib);

  DumpImpl::AppendString(buffer, length, "\n-----------------------");
  DumpImpl::AppendString(buffer, length, "\ndevice type: %u", display_type_);
  DumpImpl::AppendString(buffer, length, "\nstate: %u, vsync on: %u, max. mixer stages: %u",
                         state_, INT(vsync_enable_), max_mixer_stages_);
  DumpImpl::AppendString(buffer, length, "\nnum configs: %u, active config index: %u",
                         num_modes, active_index);

  DisplayConfigVariableInfo &info = attrib;

  uint32_t num_hw_layers = 0;
  if (hw_layers_.info.stack) {
    num_hw_layers = hw_layers_.info.count;
  }

  if (num_hw_layers == 0) {
    DumpImpl::AppendString(buffer, length, "\nNo hardware layers programmed");
    return;
  }

  LayerBuffer *out_buffer = hw_layers_.info.stack->output_buffer;
  if (out_buffer) {
    DumpImpl::AppendString(buffer, length, "\nres:%u x %u format: %s", out_buffer->width,
                           out_buffer->height, GetName(out_buffer->format));
  } else {
    DumpImpl::AppendString(buffer, length, "\nres:%u x %u, dpi:%.2f x %.2f, fps:%u,"
                           "vsync period: %u", info.x_pixels, info.y_pixels, info.x_dpi,
                           info.y_dpi, info.fps, info.vsync_period_ns);
  }

  DumpImpl::AppendString(buffer, length, "\n");

  HWLayersInfo &layer_info = hw_layers_.info;
  LayerRect &l_roi = layer_info.left_partial_update;
  LayerRect &r_roi = layer_info.right_partial_update;
  DumpImpl::AppendString(buffer, length, "\nROI(L T R B) : LEFT(%d %d %d %d)", INT(l_roi.left),
                         INT(l_roi.top), INT(l_roi.right), INT(l_roi.bottom));

  if (IsValid(r_roi)) {
    DumpImpl::AppendString(buffer, length, ", RIGHT(%d %d %d %d)", INT(r_roi.left),
                           INT(r_roi.top), INT(r_roi.right), INT(r_roi.bottom));
  }

  const char *header  = "\n| Idx |  Comp Type  |  Split | WB |  Pipe |    W x H    |          Format          |  Src Rect (L T R B) |  Dst Rect (L T R B) |  Z |    Flags   | Deci(HxV) | CS |";  //NOLINT
  const char *newline = "\n|-----|-------------|--------|----|-------|-------------|--------------------------|---------------------|---------------------|----|------------|-----------|----|";  //NOLINT
  const char *format  = "\n| %3s | %11s "     "| %6s " "| %2s | 0x%03x | %4d x %4d | %24s "                  "| %4d %4d %4d %4d "  "| %4d %4d %4d %4d "  "| %2s | %10s "   "| %9s | %2s |";  //NOLINT

  DumpImpl::AppendString(buffer, length, "\n");
  DumpImpl::AppendString(buffer, length, newline);
  DumpImpl::AppendString(buffer, length, header);
  DumpImpl::AppendString(buffer, length, newline);

  for (uint32_t i = 0; i < num_hw_layers; i++) {
    uint32_t layer_index = hw_layers_.info.index[i];
    Layer &layer = hw_layers_.info.stack->layers[layer_index];
    LayerBuffer *input_buffer = layer.input_buffer;
    HWLayerConfig &layer_config = hw_layers_.config[i];
    HWRotatorSession &hw_rotator_session = layer_config.hw_rotator_session;

    char idx[8] = { 0 };
    const char *comp_type = GetName(layer.composition);
    const char *buffer_format = GetName(input_buffer->format);
    const char *rotate_split[2] = { "Rot-1", "Rot-2" };
    const char *comp_split[2] = { "Comp-1", "Comp-2" };

    snprintf(idx, sizeof(idx), "%d", layer_index);

    for (uint32_t count = 0; count < hw_rotator_session.hw_block_count; count++) {
      char writeback_id[8] = { 0 };
      HWRotateInfo &rotate = hw_rotator_session.hw_rotate_info[count];
      LayerRect &src_roi = rotate.src_roi;
      LayerRect &dst_roi = rotate.dst_roi;

      snprintf(writeback_id, sizeof(writeback_id), "%d", rotate.writeback_id);

      DumpImpl::AppendString(buffer, length, format, idx, comp_type, rotate_split[count],
                             writeback_id, rotate.pipe_id, input_buffer->width,
                             input_buffer->height, buffer_format, INT(src_roi.left),
                             INT(src_roi.top), INT(src_roi.right), INT(src_roi.bottom),
                             INT(dst_roi.left), INT(dst_roi.top), INT(dst_roi.right),
                             INT(dst_roi.bottom), "-", "-    ", "-    ", "-");

      // print the below only once per layer block, fill with spaces for rest.
      idx[0] = 0;
      comp_type = "";
    }

    if (hw_rotator_session.hw_block_count > 0) {
      input_buffer = &hw_rotator_session.output_buffer;
      buffer_format = GetName(input_buffer->format);
    }

    for (uint32_t count = 0; count < 2; count++) {
      char decimation[16] = { 0 };
      char flags[16] = { 0 };
      char z_order[8] = { 0 };
      char csc[8] = { 0 };

      HWPipeInfo &pipe = (count == 0) ? layer_config.left_pipe : layer_config.right_pipe;

      if (!pipe.valid) {
        continue;
      }

      LayerRect &src_roi = pipe.src_roi;
      LayerRect &dst_roi = pipe.dst_roi;

      snprintf(z_order, sizeof(z_order), "%d", pipe.z_order);
      snprintf(flags, sizeof(flags), "0x%08x", layer.flags.flags);
      snprintf(decimation, sizeof(decimation), "%3d x %3d", pipe.horizontal_decimation,
               pipe.vertical_decimation);
      snprintf(csc, sizeof(csc), "%d", layer.csc);

      DumpImpl::AppendString(buffer, length, format, idx, comp_type, comp_split[count],
                             "-", pipe.pipe_id, input_buffer->width, input_buffer->height,
                             buffer_format, INT(src_roi.left), INT(src_roi.top),
                             INT(src_roi.right), INT(src_roi.bottom), INT(dst_roi.left),
                             INT(dst_roi.top), INT(dst_roi.right), INT(dst_roi.bottom),
                             z_order, flags, decimation, csc);

      // print the below only once per layer block, fill with spaces for rest.
      idx[0] = 0;
      comp_type = "";
    }

    DumpImpl::AppendString(buffer, length, newline);
  }
}

bool DisplayBase::IsRotationRequired(HWLayers *hw_layers) {
  HWLayersInfo &layer_info = hw_layers->info;

  for (uint32_t i = 0; i < layer_info.count; i++) {
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;

    if (hw_rotator_session->hw_block_count) {
      return true;
    }
  }

  return false;
}

const char * DisplayBase::GetName(const LayerComposition &composition) {
  switch (composition) {
  case kCompositionGPU:         return "GPU";
  case kCompositionSDE:         return "SDE";
  case kCompositionHWCursor:    return "CURSOR";
  case kCompositionHybrid:      return "HYBRID";
  case kCompositionBlit:        return "BLIT";
  case kCompositionGPUTarget:   return "GPU_TARGET";
  case kCompositionBlitTarget:  return "BLIT_TARGET";
  default:                      return "UNKNOWN";
  }
}

const char * DisplayBase::GetName(const LayerBufferFormat &format) {
  switch (format) {
  case kFormatARGB8888:                 return "ARGB_8888";
  case kFormatRGBA8888:                 return "RGBA_8888";
  case kFormatBGRA8888:                 return "BGRA_8888";
  case kFormatXRGB8888:                 return "XRGB_8888";
  case kFormatRGBX8888:                 return "RGBX_8888";
  case kFormatBGRX8888:                 return "BGRX_8888";
  case kFormatRGBA5551:                 return "RGBA_5551";
  case kFormatRGBA4444:                 return "RGBA_4444";
  case kFormatRGB888:                   return "RGB_888";
  case kFormatBGR888:                   return "BGR_888";
  case kFormatRGB565:                   return "RGB_565";
  case kFormatBGR565:                   return "BGR_565";
  case kFormatRGBA8888Ubwc:             return "RGBA_8888_UBWC";
  case kFormatRGBX8888Ubwc:             return "RGBX_8888_UBWC";
  case kFormatBGR565Ubwc:               return "BGR_565_UBWC";
  case kFormatYCbCr420Planar:           return "Y_CB_CR_420";
  case kFormatYCrCb420Planar:           return "Y_CR_CB_420";
  case kFormatYCrCb420PlanarStride16:   return "Y_CR_CB_420_STRIDE16";
  case kFormatYCbCr420SemiPlanar:       return "Y_CBCR_420";
  case kFormatYCrCb420SemiPlanar:       return "Y_CRCB_420";
  case kFormatYCbCr420SemiPlanarVenus:  return "Y_CBCR_420_VENUS";
  case kFormatYCrCb420SemiPlanarVenus:  return "Y_CRCB_420_VENUS";
  case kFormatYCbCr422H1V2SemiPlanar:   return "Y_CBCR_422_H1V2";
  case kFormatYCrCb422H1V2SemiPlanar:   return "Y_CRCB_422_H1V2";
  case kFormatYCbCr422H2V1SemiPlanar:   return "Y_CBCR_422_H2V1";
  case kFormatYCrCb422H2V1SemiPlanar:   return "Y_CRCB_422_H2V2";
  case kFormatYCbCr420SPVenusUbwc:      return "Y_CBCR_420_VENUS_UBWC";
  case kFormatYCbCr422H2V1Packed:       return "YCBYCR_422_H2V1";
  case kFormatRGBA1010102:              return "RGBA_1010102";
  case kFormatARGB2101010:              return "ARGB_2101010";
  case kFormatRGBX1010102:              return "RGBX_1010102";
  case kFormatXRGB2101010:              return "XRGB_2101010";
  case kFormatBGRA1010102:              return "BGRA_1010102";
  case kFormatABGR2101010:              return "ABGR_2101010";
  case kFormatBGRX1010102:              return "BGRX_1010102";
  case kFormatXBGR2101010:              return "XBGR_2101010";
  case kFormatRGBA1010102Ubwc:          return "RGBA_1010102_UBWC";
  case kFormatRGBX1010102Ubwc:          return "RGBX_1010102_UBWC";
  case kFormatYCbCr420P010:             return "Y_CBCR_420_P010";
  case kFormatYCbCr420TP10Ubwc:         return "Y_CBCR_420_TP10_UBWC";
  default:                              return "UNKNOWN";
  }
}

DisplayError DisplayBase::ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload,
                                               PPDisplayAPIPayload *out_payload,
                                               PPPendingParams *pending_action) {
  if (color_mgr_)
    return color_mgr_->ColorSVCRequestRoute(in_payload, out_payload, pending_action);
  else
    return kErrorParameters;
}

DisplayError DisplayBase::ApplyDefaultDisplayMode() {
  if (color_mgr_)
    return color_mgr_->ApplyDefaultDisplayMode();
  else
    return kErrorParameters;
}

DisplayError DisplayBase::SetCursorPosition(int x, int y) {
  if (state_ != kStateOn) {
    return kErrorNotSupported;
  }

  DisplayError error = comp_manager_->ValidateCursorPosition(display_comp_ctx_, &hw_layers_, x, y);
  if (error == kErrorNone) {
    return hw_intf_->SetCursorPosition(&hw_layers_, x, y);
  }

  return kErrorNone;
}

DisplayError DisplayBase::GetRefreshRateRange(uint32_t *min_refresh_rate,
                                              uint32_t *max_refresh_rate) {
  // The min and max refresh rates will be same when the HWPanelInfo does not contain valid rates.
  // Usually for secondary displays, command mode panels
  HWDisplayAttributes display_attributes;
  uint32_t active_index = 0;
  hw_intf_->GetActiveConfig(&active_index);
  DisplayError error = hw_intf_->GetDisplayAttributes(active_index, &display_attributes);
  if (error) {
    return error;
  }

  *min_refresh_rate = display_attributes.fps;
  *max_refresh_rate = display_attributes.fps;

  return error;
}

DisplayError DisplayBase::GetPanelBrightness(int *level) {
  return kErrorNotSupported;
}

}  // namespace sdm
