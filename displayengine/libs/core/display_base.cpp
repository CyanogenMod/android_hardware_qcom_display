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

#include "display_base.h"
#include "offline_ctrl.h"

#define __CLASS__ "DisplayBase"

namespace sde {

// TODO(user): Have a single structure handle carries all the interface pointers and variables.
DisplayBase::DisplayBase(DisplayType display_type, DisplayEventHandler *event_handler,
                         HWDeviceType hw_device_type, BufferSyncHandler *buffer_sync_handler,
                         CompManager *comp_manager, OfflineCtrl *offline_ctrl)
  : display_type_(display_type), event_handler_(event_handler), hw_device_type_(hw_device_type),
    buffer_sync_handler_(buffer_sync_handler), comp_manager_(comp_manager),
    offline_ctrl_(offline_ctrl), state_(kStateOff), hw_device_(0), display_comp_ctx_(0),
    display_attributes_(NULL), num_modes_(0), active_mode_index_(0), pending_commit_(false),
    vsync_enable_(false) {
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

  error = offline_ctrl_->RegisterDisplay(display_type_, &display_offline_ctx_);
  if (error != kErrorNone) {
    goto CleanupOnError;
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
  offline_ctrl_->UnregisterDisplay(display_offline_ctx_);

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

    comp_manager_->PrePrepare(display_comp_ctx_, &hw_layers_);
    while (true) {
      error = comp_manager_->Prepare(display_comp_ctx_, &hw_layers_);
      if (error != kErrorNone) {
        break;
      }

      error = offline_ctrl_->Prepare(display_offline_ctx_, &hw_layers_);
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

  if (state_ == kStateOn) {
    if (!pending_commit_) {
      DLOGE("Commit: Corresponding Prepare() is not called for display = %d", display_type_);
      return kErrorUndefined;
    }

    error = offline_ctrl_->Commit(display_offline_ctx_, &hw_layers_);
    if (error == kErrorNone) {
      error = hw_intf_->Commit(&hw_layers_);
      if (error == kErrorNone) {
        error = comp_manager_->PostCommit(display_comp_ctx_, &hw_layers_);
        if (error != kErrorNone) {
          DLOGE("Composition manager PostCommit failed");
        }
      } else {
        DLOGE("Unexpected error. Commit failed on driver.");
      }
    }
  } else {
    return kErrorNotSupported;
  }

  pending_commit_ = false;

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

DisplayError DisplayBase::SetActiveConfig(DisplayConfigVariableInfo *variable_info) {
  DisplayError error = kErrorNone;

  if (!variable_info) {
    return kErrorParameters;
  }

  HWDisplayAttributes display_attributes = display_attributes_[active_mode_index_];

  display_attributes.x_pixels = variable_info->x_pixels;
  display_attributes.y_pixels = variable_info->y_pixels;
  display_attributes.fps = variable_info->fps;

  // if display is already connected, unregister display from composition manager and register
  // the display with new configuration.
  if (display_comp_ctx_) {
    comp_manager_->UnregisterDisplay(display_comp_ctx_);
  }

  error = comp_manager_->RegisterDisplay(display_type_, display_attributes, hw_panel_info_,
                                         &display_comp_ctx_);
  if (error != kErrorNone) {
    return error;
  }

  display_attributes_[active_mode_index_] = display_attributes;

  return kErrorNone;
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
  }

  return error;
}

DisplayError DisplayBase::SetDisplayMode(uint32_t mode) {
  return kErrorNotSupported;
}

void DisplayBase::AppendDump(char *buffer, uint32_t length) {
  DumpImpl::AppendString(buffer, length, "\n-----------------------");
  DumpImpl::AppendString(buffer, length, "\ndevice type: %u", display_type_);
  DumpImpl::AppendString(buffer, length, "\nstate: %u, vsync on: %u", state_, INT(vsync_enable_));
  DumpImpl::AppendString(buffer, length, "\nnum configs: %u, active config index: %u",
                         num_modes_, active_mode_index_);

  DisplayConfigVariableInfo &info = display_attributes_[active_mode_index_];
  DumpImpl::AppendString(buffer, length, "\nres:%ux%u, dpi:%.2fx%.2f, fps:%.2f, vsync period: %u",
                         info.x_pixels, info.y_pixels, info.x_dpi, info.y_dpi, info.fps,
                         info.vsync_period_ns);

  uint32_t num_layers = 0;
  uint32_t num_hw_layers = 0;
  if (hw_layers_.info.stack) {
    num_layers = hw_layers_.info.stack->layer_count;
    num_hw_layers = hw_layers_.info.count;
  }

  DumpImpl::AppendString(buffer, length, "\n\nnum actual layers: %u, num sde layers: %u",
                         num_layers, num_hw_layers);

  for (uint32_t i = 0; i < num_hw_layers; i++) {
    Layer &layer = hw_layers_.info.stack->layers[hw_layers_.info.index[i]];
    LayerBuffer *input_buffer = layer.input_buffer;
    HWLayerConfig &layer_config = hw_layers_.config[i];
    HWPipeInfo &left_pipe = hw_layers_.config[i].left_pipe;
    HWPipeInfo &right_pipe = hw_layers_.config[i].right_pipe;
    HWRotateInfo &left_rotate = hw_layers_.config[i].rotates[0];
    HWRotateInfo &right_rotate = hw_layers_.config[i].rotates[1];

    DumpImpl::AppendString(buffer, length, "\n\nsde idx: %u, actual idx: %u", i,
                           hw_layers_.info.index[i]);
    DumpImpl::AppendString(buffer, length, "\nw: %u, h: %u, fmt: %u",
                           input_buffer->width, input_buffer->height, input_buffer->format);
    AppendRect(buffer, length, "\nsrc_rect:", &layer.src_rect);
    AppendRect(buffer, length, "\ndst_rect:", &layer.dst_rect);

    if (left_rotate.valid) {
      DumpImpl::AppendString(buffer, length, "\n\tleft rotate =>");
      DumpImpl::AppendString(buffer, length, "\n\t  pipe id: 0x%x", left_rotate.pipe_id);
      AppendRect(buffer, length, "\n\t  src_roi:", &left_rotate.src_roi);
      AppendRect(buffer, length, "\n\t  dst_roi:", &left_rotate.dst_roi);
    }

    if (right_rotate.valid) {
      DumpImpl::AppendString(buffer, length, "\n\tright rotate =>");
      DumpImpl::AppendString(buffer, length, "\n\t  pipe id: 0x%x", right_rotate.pipe_id);
      AppendRect(buffer, length, "\n\t  src_roi:", &right_rotate.src_roi);
      AppendRect(buffer, length, "\n\t  dst_roi:", &right_rotate.dst_roi);
    }

    if (left_pipe.valid) {
      DumpImpl::AppendString(buffer, length, "\n\tleft pipe =>");
      DumpImpl::AppendString(buffer, length, "\n\t  pipe id: 0x%x", left_pipe.pipe_id);
      AppendRect(buffer, length, "\n\t  src_roi:", &left_pipe.src_roi);
      AppendRect(buffer, length, "\n\t  dst_roi:", &left_pipe.dst_roi);
    }

    if (right_pipe.valid) {
      DumpImpl::AppendString(buffer, length, "\n\tright pipe =>");
      DumpImpl::AppendString(buffer, length, "\n\t  pipe id: 0x%x", right_pipe.pipe_id);
      AppendRect(buffer, length, "\n\t  src_roi:", &right_pipe.src_roi);
      AppendRect(buffer, length, "\n\t  dst_roi:", &right_pipe.dst_roi);
    }
  }
}

void DisplayBase::AppendRect(char *buffer, uint32_t length, const char *rect_name,
                             LayerRect *rect) {
  DumpImpl::AppendString(buffer, length, "%s %.1f, %.1f, %.1f, %.1f",
                         rect_name, rect->left, rect->top, rect->right, rect->bottom);
}

int DisplayBase::GetBestConfig() {
  return (num_modes_ == 1) ? 0 : -1;
}

}  // namespace sde

