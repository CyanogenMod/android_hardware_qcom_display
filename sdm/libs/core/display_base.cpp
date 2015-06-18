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

#include <stdio.h>
#include <utils/constants.h>
#include <utils/debug.h>

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
    rotator_intf_(rotator_intf), state_(kStateOff), hw_device_(0), display_comp_ctx_(0),
    display_attributes_(NULL), num_modes_(0), active_mode_index_(0), pending_commit_(false),
    vsync_enable_(false), underscan_supported_(false), max_mixer_stages_(0),
    hw_info_intf_(hw_info_intf), color_mgr_(NULL) {
}

DisplayError DisplayBase::Init() {
  DisplayError error = kErrorNone;
  hw_panel_info_ = HWPanelInfo();
  hw_intf_->GetHWPanelInfo(&hw_panel_info_);

  error = hw_intf_->GetNumDisplayAttributes(&num_modes_);
  if (error != kErrorNone) {
    goto CleanupOnError;
  }

  display_attributes_ = new HWDisplayAttributes[num_modes_];
  if (!display_attributes_) {
    error = kErrorMemory;
    goto CleanupOnError;
  }

  for (uint32_t i = 0; i < num_modes_; i++) {
    error = hw_intf_->GetDisplayAttributes(&display_attributes_[i], i);
    if (error != kErrorNone) {
      goto CleanupOnError;
    }
  }

  active_mode_index_ = GetBestConfig();

  error = hw_intf_->SetDisplayAttributes(active_mode_index_);
  if (error != kErrorNone) {
    goto CleanupOnError;
  }

  error = comp_manager_->RegisterDisplay(display_type_, display_attributes_[active_mode_index_],
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
    int max_mixer_stages = hw_resource_info.num_blending_stages;
    int property_value = Debug::GetMaxPipesPerMixer(display_type_);
    if (property_value >= 0) {
      max_mixer_stages = MIN(UINT32(property_value), hw_resource_info.num_blending_stages);
    }
    DisplayBase::SetMaxMixerStages(max_mixer_stages);
  }

  color_mgr_ = ColorManagerProxy::CreateColorManagerProxy(display_type_, hw_intf_,
                               display_attributes_[active_mode_index_], hw_panel_info_);
  if (!color_mgr_) {
    DLOGW("Unable to create ColorManagerProxy for display = %d", display_type_);
  }

  return kErrorNone;

CleanupOnError:
  if (display_comp_ctx_) {
    comp_manager_->UnregisterDisplay(display_comp_ctx_);
  }

  if (display_attributes_) {
    delete[] display_attributes_;
    display_attributes_ = NULL;
  }

  hw_intf_->Close();

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

  if (display_attributes_) {
    delete[] display_attributes_;
    display_attributes_ = NULL;
  }

  hw_intf_->Close();

  return kErrorNone;
}

DisplayError DisplayBase::Prepare(LayerStack *layer_stack) {
  DisplayError error = kErrorNone;

  if (!layer_stack) {
    return kErrorParameters;
  }

  pending_commit_ = false;

  if (state_ == kStateOn) {
    // Clean hw layers for reuse.
    hw_layers_.info = HWLayersInfo();
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
          error = rotator_intf_->Purge(display_rotator_ctx_, &hw_layers_);
        }
      }

      if (error == kErrorNone) {
        error = hw_intf_->Validate(&hw_layers_);
        if (error == kErrorNone) {
          // Strategy is successful now, wait for Commit().
          pending_commit_ = true;
          break;
        }
      }
    }
    comp_manager_->PostPrepare(display_comp_ctx_, &hw_layers_);
  } else {
    return kErrorNotSupported;
  }

  return error;
}

DisplayError DisplayBase::Commit(LayerStack *layer_stack) {
  DisplayError error = kErrorNone;

  if (!layer_stack) {
    return kErrorParameters;
  }

  if (state_ != kStateOn) {
    return kErrorNotSupported;
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
  if (error != kErrorNone) { // won't affect this execution path.
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

  if (state_ != kStateOn) {
    return kErrorNone;
  }

  hw_layers_.info.count = 0;
  error = hw_intf_->Flush();
  if (error == kErrorNone) {
    comp_manager_->Purge(display_comp_ctx_);
    pending_commit_ = false;
  } else {
    DLOGV("Failed to flush device.");
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
  if (!count) {
    return kErrorParameters;
  }

  *count = num_modes_;

  return kErrorNone;
}

DisplayError DisplayBase::GetConfig(DisplayConfigFixedInfo *fixed_info) {
  if (!fixed_info) {
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError DisplayBase::GetConfig(uint32_t index, DisplayConfigVariableInfo *variable_info) {
  if (!variable_info || index >= num_modes_) {
    return kErrorParameters;
  }

  *variable_info = display_attributes_[index];

  return kErrorNone;
}

DisplayError DisplayBase::GetActiveConfig(uint32_t *index) {
  if (!index) {
    return kErrorParameters;
  }

  *index = active_mode_index_;

  return kErrorNone;
}

DisplayError DisplayBase::GetVSyncState(bool *enabled) {
  if (!enabled) {
    return kErrorParameters;
  }

  return kErrorNone;
}

bool DisplayBase::IsUnderscanSupported() {
  return underscan_supported_;
}

DisplayError DisplayBase::SetDisplayState(DisplayState state) {
  DisplayError error = kErrorNone;

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
      comp_manager_->Purge(display_comp_ctx_);

      error = hw_intf_->PowerOff();
    }
    break;

  case kStateOn:
    error = hw_intf_->PowerOn();
    break;

  case kStateDoze:
    error = hw_intf_->Doze();
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
    state_ = state;
  }

  return error;
}

DisplayError DisplayBase::SetActiveConfig(uint32_t index) {
  DisplayError error = kErrorNone;

  if (index >= num_modes_) {
    return kErrorParameters;
  }

  error = hw_intf_->SetDisplayAttributes(index);
  if (error != kErrorNone) {
    return error;
  }

  active_mode_index_ = index;

  if (display_comp_ctx_) {
    comp_manager_->UnregisterDisplay(display_comp_ctx_);
  }

  error = comp_manager_->RegisterDisplay(display_type_, display_attributes_[index], hw_panel_info_,
                                         &display_comp_ctx_);

  return error;
}

DisplayError DisplayBase::SetMaxMixerStages(uint32_t max_mixer_stages) {
  DisplayError error = kErrorNone;

  if (comp_manager_) {
    error = comp_manager_->SetMaxMixerStages(display_comp_ctx_, max_mixer_stages);

    if (error == kErrorNone) {
      max_mixer_stages_ = max_mixer_stages;
    }
  }

  return error;
}

DisplayError DisplayBase::SetDisplayMode(uint32_t mode) {
  return kErrorNotSupported;
}

DisplayError DisplayBase::IsScalingValid(const LayerRect &crop, const LayerRect &dst,
                                         bool rotate90) {
  return comp_manager_->ValidateScaling(crop, dst, rotate90);
}

void DisplayBase::AppendDump(char *buffer, uint32_t length) {
  DumpImpl::AppendString(buffer, length, "\n-----------------------");
  DumpImpl::AppendString(buffer, length, "\ndevice type: %u", display_type_);
  DumpImpl::AppendString(buffer, length, "\nstate: %u, vsync on: %u, max. mixer stages: %u",
                         state_, INT(vsync_enable_), max_mixer_stages_);
  DumpImpl::AppendString(buffer, length, "\nnum configs: %u, active config index: %u",
                         num_modes_, active_mode_index_);

  DisplayConfigVariableInfo &info = display_attributes_[active_mode_index_];
  DumpImpl::AppendString(buffer, length, "\nres:%u x %u, dpi:%.2f x %.2f, fps:%.2f,"
                         "vsync period: %u", info.x_pixels, info.y_pixels, info.x_dpi,
                         info.y_dpi, info.fps, info.vsync_period_ns);

  DumpImpl::AppendString(buffer, length, "\n");

  uint32_t num_layers = 0;
  uint32_t num_hw_layers = 0;
  if (hw_layers_.info.stack) {
    num_layers = hw_layers_.info.stack->layer_count;
    num_hw_layers = hw_layers_.info.count;
  }

  if (num_hw_layers == 0) {
    DumpImpl::AppendString(buffer, length, "\nNo hardware layers programmed");
    return;
  }

  HWLayersInfo &layer_info = hw_layers_.info;
  LayerRect &l_roi = layer_info.left_partial_update;
  LayerRect &r_roi = layer_info.right_partial_update;
  DumpImpl::AppendString(buffer, length, "\nROI(L T R B) : LEFT(%d %d %d %d), RIGHT(%d %d %d %d)",
                         INT(l_roi.left), INT(l_roi.top), INT(l_roi.right), INT(l_roi.bottom),
                         INT(r_roi.left), INT(r_roi.top), INT(r_roi.right), INT(r_roi.bottom));

  const char *header  = "\n| Idx |  Comp Type  |  Split | WB |  Pipe |    W x H    |       Format       |  Src Rect (L T R B) |  Dst Rect (L T R B) |  Z |    Flags   | Deci(HxV) |";  //NOLINT
  const char *newline = "\n|-----|-------------|--------|----|-------|-------------|--------------------|---------------------|---------------------|----|------------|-----------|";  //NOLINT
  const char *format  = "\n| %3s | %11s "     "| %6s " "| %2s | 0x%03x | %4d x %4d | %18s "            "| %4d %4d %4d %4d "  "| %4d %4d %4d %4d "  "| %2s | %10s "   "| %9s |";  //NOLINT

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
    const char *rotate_split[2] = { "Rot-L", "Rot-R" };
    const char *comp_split[2] = { "Comp-L", "Comp-R" };

    snprintf(idx, sizeof(idx), "%d", layer_index);

    for (uint32_t count = 0; count < hw_rotator_session.hw_block_count; count++) {
      char writeback_id[8];
      HWRotateInfo &rotate = hw_rotator_session.hw_rotate_info[count];
      LayerRect &src_roi = rotate.src_roi;
      LayerRect &dst_roi = rotate.dst_roi;

      snprintf(writeback_id, sizeof(writeback_id), "%d", rotate.writeback_id);

      DumpImpl::AppendString(buffer, length, format, idx, comp_type, rotate_split[count],
                             writeback_id, rotate.pipe_id, input_buffer->width,
                             input_buffer->height, buffer_format, INT(src_roi.left),
                             INT(src_roi.top), INT(src_roi.right), INT(src_roi.bottom),
                             INT(dst_roi.left), INT(dst_roi.top), INT(dst_roi.right),
                             INT(dst_roi.bottom), "-", "-    ", "-    ");

      // print the below only once per layer block, fill with spaces for rest.
      idx[0] = 0;
      comp_type = "";
    }

    if (hw_rotator_session.hw_block_count > 0) {
      input_buffer = &hw_rotator_session.output_buffer;
      buffer_format = GetName(input_buffer->format);
    }

    for (uint32_t count = 0; count < 2; count++) {
      char decimation[16];
      char flags[16];
      char z_order[8];
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

      DumpImpl::AppendString(buffer, length, format, idx, comp_type, comp_split[count],
                             "-", pipe.pipe_id, input_buffer->width, input_buffer->height,
                             buffer_format, INT(src_roi.left), INT(src_roi.top),
                             INT(src_roi.right), INT(src_roi.bottom), INT(dst_roi.left),
                             INT(dst_roi.top), INT(dst_roi.right), INT(dst_roi.bottom),
                             z_order, flags, decimation);

      // print the below only once per layer block, fill with spaces for rest.
      idx[0] = 0;
      comp_type = "";
    }

    DumpImpl::AppendString(buffer, length, newline);
  }
}

int DisplayBase::GetBestConfig() {
  return (num_modes_ == 1) ? 0 : -1;
}

bool DisplayBase::IsRotationRequired(HWLayers *hw_layers) {
  HWLayersInfo &layer_info = hw_layers->info;

  for (uint32_t i = 0; i < layer_info.count; i++) {
    Layer& layer = layer_info.stack->layers[layer_info.index[i]];
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
  case kFormatRGBA8888Ubwc:             return "RGBA_8888_UBWC";
  case kFormatRGBX8888Ubwc:             return "RGBX_8888_UBWC";
  case kFormatRGB565Ubwc:               return "RGB_565_UBWC";
  case kFormatYCbCr420Planar:           return "Y_CB_CR_420";
  case kFormatYCrCb420Planar:           return "Y_CR_CB_420";
  case kFormatYCbCr420SemiPlanar:       return "Y_CBCR_420";
  case kFormatYCrCb420SemiPlanar:       return "Y_CRCB_420";
  case kFormatYCbCr420SemiPlanarVenus:  return "Y_CBCR_420_VENUS";
  case kFormatYCbCr422H1V2SemiPlanar:   return "Y_CBCR_422_H1V2";
  case kFormatYCrCb422H1V2SemiPlanar:   return "Y_CRCB_422_H1V2";
  case kFormatYCbCr422H2V1SemiPlanar:   return "Y_CBCR_422_H2V1";
  case kFormatYCrCb422H2V1SemiPlanar:   return "Y_CRCB_422_H2V2";
  case kFormatYCbCr420SPVenusUbwc:      return "Y_CBCR_420_VENUS_UBWC";
  case kFormatYCbCr422H2V1Packed:       return "YCBYCR_422_H2V1";
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

}  // namespace sdm
