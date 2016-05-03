/*
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <math.h>
#include <errno.h>
#include <gralloc_priv.h>
#include <gr.h>
#include <utils/constants.h>
#include <utils/formats.h>
#include <utils/rect.h>
#include <utils/debug.h>
#include <sync/sync.h>
#include <cutils/properties.h>
#include <map>
#include <string>
#include <utility>

#include "hwc_display.h"
#include "hwc_debugger.h"
#include "blit_engine_c2d.h"

#ifdef QTI_BSP
#include <hardware/display_defs.h>
#endif

#define __CLASS__ "HWCDisplay"

namespace sdm {

static void ApplyDeInterlaceAdjustment(Layer *layer) {
  // De-interlacing adjustment
  if (layer->input_buffer->flags.interlace) {
    float height = (layer->src_rect.bottom - layer->src_rect.top) / 2.0f;
    layer->src_rect.top = ROUND_UP_ALIGN_DOWN(layer->src_rect.top / 2.0f, 2);
    layer->src_rect.bottom = layer->src_rect.top + floorf(height);
  }
}

HWCDisplay::HWCDisplay(CoreInterface *core_intf, HWCCallbacks *callbacks, DisplayType type,
                       hwc2_display_t id, bool needs_blit, qService::QService *qservice,
                       DisplayClass display_class)
    : core_intf_(core_intf),
      callbacks_(callbacks),
      type_(type),
      id_(id),
      needs_blit_(needs_blit),
      qservice_(qservice),
      display_class_(display_class) {
}

int HWCDisplay::Init() {
  DisplayError error = core_intf_->CreateDisplay(type_, this, &display_intf_);
  if (error != kErrorNone) {
    DLOGE("Display create failed. Error = %d display_type %d event_handler %p disp_intf %p", error,
          type_, this, &display_intf_);
    return -EINVAL;
  }

  int property_swap_interval = 1;
  HWCDebugHandler::Get()->GetProperty("debug.egl.swapinterval", &property_swap_interval);
  if (property_swap_interval == 0) {
    swap_interval_zero_ = true;
  }

  framebuffer_config_ = new DisplayConfigVariableInfo();
  if (!framebuffer_config_) {
    DLOGV("Failed to allocate memory for custom framebuffer config.");
    core_intf_->DestroyDisplay(display_intf_);
    return -EINVAL;
  }

  client_target_ = new HWCLayer(id_);
  int blit_enabled = 0;
  HWCDebugHandler::Get()->GetProperty("persist.hwc.blit.comp", &blit_enabled);
  if (needs_blit_ && blit_enabled) {
    blit_engine_ = new BlitEngineC2d();
    if (!blit_engine_) {
      DLOGI("Create Blit Engine C2D failed");
    } else {
      if (blit_engine_->Init() < 0) {
        DLOGI("Blit Engine Init failed, Blit Composition will not be used!!");
        delete blit_engine_;
        blit_engine_ = NULL;
      }
    }
  }

  display_intf_->GetRefreshRateRange(&min_refresh_rate_, &max_refresh_rate_);
  current_refresh_rate_ = max_refresh_rate_;
  DLOGI("Display created with id: %d", id_);
  return 0;
}

int HWCDisplay::Deinit() {
  DisplayError error = core_intf_->DestroyDisplay(display_intf_);
  if (error != kErrorNone) {
    DLOGE("Display destroy failed. Error = %d", error);
    return -EINVAL;
  }

  delete framebuffer_config_;
  delete client_target_;

  if (blit_engine_) {
    blit_engine_->DeInit();
    delete blit_engine_;
    blit_engine_ = NULL;
  }

  return 0;
}

// LayerStack operations
HWC2::Error HWCDisplay::CreateLayer(hwc2_layer_t *out_layer_id) {
  auto layer = *layer_set_.emplace(new HWCLayer(id_));
  layer_map_.emplace(std::make_pair(layer->GetId(), layer));
  *out_layer_id = layer->GetId();
  geometry_changes_ = GeometryChanges::kAdded;
  return HWC2::Error::None;
}

HWCLayer *HWCDisplay::GetHWCLayer(hwc2_layer_t layer_id) {
  const auto map_layer = layer_map_.find(layer_id);
  if (map_layer == layer_map_.end()) {
    DLOGE("[%" PRIu64 "] GetLayer(%" PRIu64 ") failed: no such layer", id_, layer_id);
    return nullptr;
  } else {
    return map_layer->second;
  }
}

HWC2::Error HWCDisplay::DestroyLayer(hwc2_layer_t layer_id) {
  const auto map_layer = layer_map_.find(layer_id);
  if (map_layer == layer_map_.end()) {
    DLOGE("[%" PRIu64 "] destroyLayer(%" PRIu64 ") failed: no such layer", id_, layer_id);
    return HWC2::Error::BadLayer;
  }
  const auto layer = map_layer->second;
  layer_map_.erase(map_layer);
  const auto z_range = layer_set_.equal_range(layer);
  for (auto current = z_range.first; current != z_range.second; ++current) {
    if (*current == layer) {
      current = layer_set_.erase(current);
      break;
    }
  }

  geometry_changes_ = GeometryChanges::kRemoved;
  return HWC2::Error::None;
}

void HWCDisplay::BuildLayerStack() {
  // TODO(user): Validate
  validated_ = true;
  layer_stack_ = LayerStack();
  display_rect_ = LayerRect();
  metadata_refresh_rate_ = 0;

  // Add one layer for fb target
  // TODO(user): Add blit target layers
  for (auto hwc_layer : layer_set_) {
    Layer *layer = hwc_layer->GetSDMLayer();

    if (swap_interval_zero_) {
      if (layer->input_buffer->acquire_fence_fd >= 0) {
        close(layer->input_buffer->acquire_fence_fd);
        layer->input_buffer->acquire_fence_fd = -1;
      }
    }

    const private_handle_t *handle =
        reinterpret_cast<const private_handle_t *>(layer->input_buffer->buffer_id);
    if (handle) {
      if (handle->bufferType == BUFFER_TYPE_VIDEO) {
        layer_stack_.flags.video_present = true;
      }
      // TZ Protected Buffer - L1
      if (handle->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
        layer_stack_.flags.secure_present = true;
      }
      // Gralloc Usage Protected Buffer - L3 - which needs to be treated as Secure & avoid fallback
      if (handle->flags & private_handle_t::PRIV_FLAGS_PROTECTED_BUFFER) {
        layer_stack_.flags.secure_present = true;
      }
    }

    if (layer->flags.skip) {
      layer_stack_.flags.skip_present = true;
    }

    if (layer->flags.cursor) {
      layer_stack_.flags.cursor_present = true;
    }
    // TODO(user): Move to a getter if this is needed at other places
    hwc_rect_t scaled_display_frame = {INT(layer->dst_rect.left), INT(layer->dst_rect.top),
                                       INT(layer->dst_rect.right), INT(layer->dst_rect.bottom)};
    ScaleDisplayFrame(&scaled_display_frame);
    ApplyScanAdjustment(&scaled_display_frame);
    hwc_layer->SetLayerDisplayFrame(scaled_display_frame);
    ApplyDeInterlaceAdjustment(layer);
    // TODO(user): Verify if we still need to configure the solid fill layerbuffer,
    // it should already have a valid dst_rect by this point

    if (layer->frame_rate > metadata_refresh_rate_) {
      metadata_refresh_rate_ = SanitizeRefreshRate(layer->frame_rate);
    } else {
      layer->frame_rate = current_refresh_rate_;
    }
    display_rect_ = Union(display_rect_, layer->dst_rect);
    // TODO(user): Set correctly when implementing caching
    layer->flags.updating = true;
    geometry_changes_ |= hwc_layer->GetGeometryChanges();

    layer_stack_.layers.push_back(layer);
  }
  layer_stack_.flags.geometry_changed = UINT32(geometry_changes_ > 0);
  // Append client target to the layer stack
  layer_stack_.layers.push_back(client_target_->GetSDMLayer());
}

HWC2::Error HWCDisplay::SetLayerZOrder(hwc2_layer_t layer_id, uint32_t z) {
  const auto map_layer = layer_map_.find(layer_id);
  if (map_layer == layer_map_.end()) {
    DLOGE("[%" PRIu64 "] updateLayerZ failed to find layer", id_);
    return HWC2::Error::BadLayer;
  }

  const auto layer = map_layer->second;
  const auto z_range = layer_set_.equal_range(layer);
  bool layer_on_display = false;
  for (auto current = z_range.first; current != z_range.second; ++current) {
    if (*current == layer) {
      if ((*current)->GetZ() == z) {
        // Don't change anything if the Z hasn't changed
        return HWC2::Error::None;
      }
      current = layer_set_.erase(current);
      layer_on_display = true;
      break;
    }
  }

  if (!layer_on_display) {
    DLOGE("[%" PRIu64 "] updateLayerZ failed to find layer on display", id_);
    return HWC2::Error::BadLayer;
  }

  layer->SetLayerZOrder(z);
  layer_set_.emplace(layer);
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::SetVsyncEnabled(HWC2::Vsync enabled) {
  DLOGV("Display ID: %d enabled: %s", id_, to_string(enabled).c_str());
  DisplayError error = kErrorNone;

  if (shutdown_pending_) {
    return HWC2::Error::None;
  }

  bool state;
  if (enabled == HWC2::Vsync::Enable)
    state = true;
  else if (enabled == HWC2::Vsync::Disable)
    state = false;
  else
    return HWC2::Error::BadParameter;

  error = display_intf_->SetVSyncState(state);

  if (error != kErrorNone) {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return HWC2::Error::None;
    }
    DLOGE("Failed. enabled = %s, error = %d", to_string(enabled).c_str(), error);
    return HWC2::Error::BadDisplay;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::SetPowerMode(HWC2::PowerMode mode) {
  DLOGV("display = %d, mode = %s", id_, to_string(mode).c_str());
  DisplayState state = kStateOff;
  bool flush_on_error = flush_on_error_;

  if (shutdown_pending_) {
    return HWC2::Error::None;
  }

  switch (mode) {
    case HWC2::PowerMode::Off:
      // During power off, all of the buffers are released.
      // Do not flush until a buffer is successfully submitted again.
      flush_on_error = false;
      state = kStateOff;
      break;
    case HWC2::PowerMode::On:
      state = kStateOn;
      last_power_mode_ = HWC2::PowerMode::On;
      break;
    case HWC2::PowerMode::Doze:
      state = kStateDoze;
      last_power_mode_ = HWC2::PowerMode::Doze;
      break;
    case HWC2::PowerMode::DozeSuspend:
      state = kStateDozeSuspend;
      last_power_mode_ = HWC2::PowerMode::DozeSuspend;
      break;
    default:
      return HWC2::Error::BadParameter;
  }

  DisplayError error = display_intf_->SetDisplayState(state);
  if (error == kErrorNone) {
    flush_on_error_ = flush_on_error;
  } else {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return HWC2::Error::None;
    }
    DLOGE("Set state failed. Error = %d", error);
    return HWC2::Error::BadParameter;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetClientTargetSupport(uint32_t width, uint32_t height, int32_t format,
                                               int32_t dataspace) {
  // TODO(user): Support scaled configurations, other formats and other dataspaces
  if (format != HAL_PIXEL_FORMAT_RGBA_8888 || dataspace != HAL_DATASPACE_UNKNOWN ||
      width != framebuffer_config_->x_pixels || height != framebuffer_config_->y_pixels) {
    return HWC2::Error::Unsupported;
  } else {
    return HWC2::Error::None;
  }
}

HWC2::Error HWCDisplay::GetDisplayConfigs(uint32_t *out_num_configs, hwc2_config_t *out_configs) {
  // TODO(user): Actually handle multiple configs
  if (out_configs == nullptr) {
    *out_num_configs = 1;
  } else {
    *out_num_configs = 1;
    out_configs[0] = 0;
  }
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetDisplayAttribute(hwc2_config_t config, HWC2::Attribute attribute,
                                            int32_t *out_value) {
  DisplayConfigVariableInfo variable_config = *framebuffer_config_;

  switch (attribute) {
    case HWC2::Attribute::VsyncPeriod:
      *out_value = INT32(variable_config.vsync_period_ns);
      break;
    case HWC2::Attribute::Width:
      *out_value = INT32(variable_config.x_pixels);
      break;
    case HWC2::Attribute::Height:
      *out_value = INT32(variable_config.y_pixels);
      break;
    case HWC2::Attribute::DpiX:
      *out_value = INT32(variable_config.x_dpi * 1000.0f);
      break;
    case HWC2::Attribute::DpiY:
      *out_value = INT32(variable_config.y_dpi * 1000.0f);
      break;
    default:
      DLOGW("Spurious attribute type = %s", to_string(attribute).c_str());
      return HWC2::Error::BadConfig;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetDisplayName(uint32_t *out_size, char *out_name) {
  // TODO(user): Get panel name and EDID name and populate it here
  if (out_name == nullptr) {
    *out_size = 32;
  } else {
    std::string name;
    switch (id_) {
      case HWC_DISPLAY_PRIMARY:
        name = "Primary Display";
        break;
      case HWC_DISPLAY_EXTERNAL:
        name = "External Display";
        break;
      case HWC_DISPLAY_VIRTUAL:
        name = "Virtual Display";
        break;
      default:
        name = "Unknown";
        break;
    }
    std::strncpy(out_name, name.c_str(), name.size());
    *out_size = UINT32(name.size());
  }
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetDisplayType(int32_t *out_type) {
  if (out_type != nullptr) {
    if (id_ == HWC_DISPLAY_VIRTUAL) {
      *out_type = HWC2_DISPLAY_TYPE_VIRTUAL;
    } else {
      *out_type = HWC2_DISPLAY_TYPE_PHYSICAL;
    }
    return HWC2::Error::None;
  } else {
    return HWC2::Error::BadParameter;
  }
}

// TODO(user): Store configurations and hook them up here
HWC2::Error HWCDisplay::GetActiveConfig(hwc2_config_t *out_config) {
  if (out_config != nullptr) {
    *out_config = 0;
    return HWC2::Error::None;
  } else {
    return HWC2::Error::BadParameter;
  }
}

HWC2::Error HWCDisplay::SetClientTarget(buffer_handle_t target, int32_t acquire_fence,
                                        int32_t dataspace) {
  client_target_->SetLayerBuffer(target, acquire_fence);
  // Ignoring dataspace for now
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::SetActiveConfig(hwc2_config_t config) {
  // We have only one config right now - do nothing
  return HWC2::Error::None;
}

void HWCDisplay::SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type) {
  dump_frame_count_ = count;
  dump_frame_index_ = 0;
  dump_input_layers_ = ((bit_mask_layer_type & (1 << INPUT_LAYER_DUMP)) != 0);

  if (blit_engine_) {
    blit_engine_->SetFrameDumpConfig(count);
  }

  DLOGI("num_frame_dump %d, input_layer_dump_enable %d", dump_frame_count_, dump_input_layers_);
}

HWC2::PowerMode HWCDisplay::GetLastPowerMode() {
  return last_power_mode_;
}

DisplayError HWCDisplay::VSync(const DisplayEventVSync &vsync) {
  callbacks_->Vsync(id_, vsync.timestamp);
  return kErrorNone;
}

DisplayError HWCDisplay::Refresh() {
  return kErrorNotSupported;
}

DisplayError HWCDisplay::CECMessage(char *message) {
  if (qservice_) {
    qservice_->onCECMessageReceived(message, 0);
  } else {
    DLOGW("Qservice instance not available.");
  }

  return kErrorNone;
}

HWC2::Error HWCDisplay::PrepareLayerStack(uint32_t *out_num_types, uint32_t *out_num_requests) {
  layer_changes_.clear();
  layer_requests_.clear();
  if (shutdown_pending_) {
    return HWC2::Error::BadDisplay;
  }

  if (!skip_prepare_) {
    DisplayError error = display_intf_->Prepare(&layer_stack_);
    if (error != kErrorNone) {
      if (error == kErrorShutDown) {
        shutdown_pending_ = true;
      } else if (error != kErrorPermission) {
        DLOGE("Prepare failed. Error = %d", error);
        // To prevent surfaceflinger infinite wait, flush the previous frame during Commit()
        // so that previous buffer and fences are released, and override the error.
        flush_ = true;
      }
      return HWC2::Error::BadDisplay;
    }
  } else {
    // Skip is not set
    MarkLayersForGPUBypass();
    skip_prepare_ = false;
    DLOGI("SecureDisplay %s, Skip Prepare/Commit and Flush",
          secure_display_active_ ? "Starting" : "Stopping");
    flush_ = true;
  }

  // If current draw cycle has different set of layers updating in comparison to previous cycle,
  // cache content using GPU again.
  // If set of updating layers remains same, use cached buffer and replace layers marked for GPU
  // composition with SDE so that SurfaceFlinger does not compose them. Set cache inuse here.
  bool needs_fb_refresh = NeedsFrameBufferRefresh();
  for (auto hwc_layer : layer_set_) {
    Layer *layer = hwc_layer->GetSDMLayer();
    LayerComposition &composition = layer->composition;

    if ((composition == kCompositionSDE) || (composition == kCompositionHybrid) ||
        (composition == kCompositionBlit)) {
      layer_requests_[hwc_layer->GetId()] = HWC2::LayerRequest::ClearClientTarget;
    }

    if (!needs_fb_refresh && composition == kCompositionGPU) {
      composition = kCompositionSDE;
    }
    HWC2::Composition current_hwc_composition  = hwc_layer->GetCompositionType();
    // Convert the SDM layer composition to HWC2 type
    hwc_layer->SetComposition(composition);
    // Update the changes list only if the HWC2 comp type changed from the previous cycle
    if (current_hwc_composition != hwc_layer->GetCompositionType()) {
      layer_changes_[hwc_layer->GetId()] = hwc_layer->GetCompositionType();
    }
  }
  *out_num_types = UINT32(layer_changes_.size());
  *out_num_requests = UINT32(layer_requests_.size());
  validated_ = true;
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::AcceptDisplayChanges() {
  if (!validated_) {
    return HWC2::Error::NotValidated;
  }
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetChangedCompositionTypes(uint32_t *out_num_elements,
                                                   hwc2_layer_t *out_layers, int32_t *out_types) {
  *out_num_elements = UINT32(layer_changes_.size());
  if (out_layers != nullptr && out_types != nullptr) {
    int i = 0;
    for (auto change : layer_changes_) {
      out_layers[i] = change.first;
      out_types[i] = INT32(change.second);
      i++;
    }
  }
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetReleaseFences(uint32_t *out_num_elements, hwc2_layer_t *out_layers,
                                         int32_t *out_fences) {
  if (out_layers != nullptr && out_fences != nullptr) {
    int i = 0;
    for (auto hwc_layer : layer_set_) {
      out_layers[i] = hwc_layer->GetId();
      out_fences[i] = hwc_layer->PopReleaseFence();
      i++;
    }
  }
  *out_num_elements = UINT32(layer_set_.size());
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetDisplayRequests(int32_t *out_display_requests,
                                           uint32_t *out_num_elements, hwc2_layer_t *out_layers,
                                           int32_t *out_layer_requests) {
  // No display requests for now
  // Use for sharing blit buffers and
  // writing wfd buffer directly to output if there is full GPU composition
  // and no color conversion needed
  *out_display_requests = 0;
  *out_num_elements = UINT32(layer_requests_.size());
  if (out_layers != nullptr && out_layer_requests != nullptr) {
    int i = 0;
    for (auto &request : layer_requests_) {
      out_layers[i] = request.first;
      out_layer_requests[i] = INT32(request.second);
      i++;
    }
  }
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::CommitLayerStack(void) {
  if (shutdown_pending_) {
    return HWC2::Error::None;
  }

  if (!validated_) {
    DLOGW("Display is not validated");
    return HWC2::Error::NotValidated;
  }

  DumpInputBuffers();

  if (!flush_) {
    DisplayError error = kErrorUndefined;
    error = display_intf_->Commit(&layer_stack_);

    if (error == kErrorNone) {
      // A commit is successfully submitted, start flushing on failure now onwards.
      flush_on_error_ = true;
    } else {
      if (error == kErrorShutDown) {
        shutdown_pending_ = true;
        return HWC2::Error::Unsupported;
      } else if (error != kErrorPermission) {
        DLOGE("Commit failed. Error = %d", error);
        // To prevent surfaceflinger infinite wait, flush the previous frame during Commit()
        // so that previous buffer and fences are released, and override the error.
        flush_ = true;
      }
    }
  }

  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::PostCommitLayerStack(int32_t *out_retire_fence) {
  auto status = HWC2::Error::None;

  // Do no call flush on errors, if a successful buffer is never submitted.
  if (flush_ && flush_on_error_) {
    display_intf_->Flush();
  }

  // TODO(user): No way to set the client target release fence on SF
  int32_t &client_target_release_fence =
      client_target_->GetSDMLayer()->input_buffer->release_fence_fd;
  if (client_target_release_fence >= 0) {
    close(client_target_release_fence);
    client_target_release_fence = -1;
  }

  for (auto hwc_layer : layer_set_) {
    hwc_layer->ResetGeometryChanges();
    Layer *layer = hwc_layer->GetSDMLayer();
    LayerBuffer *layer_buffer = layer->input_buffer;

    if (!flush_) {
      // If swapinterval property is set to 0 or for single buffer layers, do not update f/w
      // release fences and discard fences from driver
      if (swap_interval_zero_ || layer->flags.single_buffer) {
        close(layer_buffer->release_fence_fd);
        layer_buffer->release_fence_fd = -1;
      } else if (layer->composition != kCompositionGPU) {
        hwc_layer->PushReleaseFence(layer_buffer->release_fence_fd);
      }
    }

    if (layer_buffer->acquire_fence_fd >= 0) {
      close(layer_buffer->acquire_fence_fd);
      layer_buffer->acquire_fence_fd = -1;
    }
  }

  if (!flush_) {
    // if swapinterval property is set to 0 then close and reset the list retire fence
    if (swap_interval_zero_) {
      close(layer_stack_.retire_fence_fd);
      layer_stack_.retire_fence_fd = -1;
    }
    *out_retire_fence = stored_retire_fence_;
    stored_retire_fence_ = layer_stack_.retire_fence_fd;

    if (dump_frame_count_) {
      dump_frame_count_--;
      dump_frame_index_++;
    }
  }
  geometry_changes_ = GeometryChanges::kNone;
  flush_ = false;

  return status;
}

bool HWCDisplay::NeedsFrameBufferRefresh(void) {
  // Frame buffer needs to be refreshed for the following reasons:
  // 1. Any layer is marked skip in the current layer stack.
  // 2. Any layer is added/removed/layer properties changes in the current layer stack.
  // 3. Any layer handle is changed and it is marked for GPU composition
  // 4. Any layer's current composition is different from previous composition.
  if (layer_stack_.flags.skip_present || layer_stack_.flags.geometry_changed) {
    return true;
  }

  for (auto layer : layer_stack_.layers) {
    // need FB refresh for s3d case
    if (layer->input_buffer->s3d_format != kS3dFormatNone) {
      return true;
    }

    if (layer->composition == kCompositionGPUTarget ||
        layer->composition == kCompositionBlitTarget) {
      continue;
    }
  }

  return false;
}

void HWCDisplay::SetIdleTimeoutMs(uint32_t timeout_ms) {
  return;
}

DisplayError HWCDisplay::SetMaxMixerStages(uint32_t max_mixer_stages) {
  DisplayError error = kErrorNone;

  if (display_intf_) {
    error = display_intf_->SetMaxMixerStages(max_mixer_stages);
  }

  return error;
}

DisplayError HWCDisplay::ControlPartialUpdate(bool enable, uint32_t *pending) {
  DisplayError error = kErrorNone;

  if (display_intf_) {
    error = display_intf_->ControlPartialUpdate(enable, pending);
  }

  return error;
}

LayerBufferFormat HWCDisplay::GetSDMFormat(const int32_t &source, const int flags) {
  LayerBufferFormat format = kFormatInvalid;
  if (flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
    switch (source) {
      case HAL_PIXEL_FORMAT_RGBA_8888:
        format = kFormatRGBA8888Ubwc;
        break;
      case HAL_PIXEL_FORMAT_RGBX_8888:
        format = kFormatRGBX8888Ubwc;
        break;
      case HAL_PIXEL_FORMAT_BGR_565:
        format = kFormatBGR565Ubwc;
        break;
      case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
      case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
      case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
        format = kFormatYCbCr420SPVenusUbwc;
        break;
      case HAL_PIXEL_FORMAT_RGBA_1010102:
        format = kFormatRGBA1010102Ubwc;
        break;
      case HAL_PIXEL_FORMAT_RGBX_1010102:
        format = kFormatRGBX1010102Ubwc;
        break;
      case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
        format = kFormatYCbCr420TP10Ubwc;
        break;
      default:
        DLOGE("Unsupported format type for UBWC %d", source);
        return kFormatInvalid;
    }
    return format;
  }

  switch (source) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
      format = kFormatRGBA8888;
      break;
    case HAL_PIXEL_FORMAT_RGBA_5551:
      format = kFormatRGBA5551;
      break;
    case HAL_PIXEL_FORMAT_RGBA_4444:
      format = kFormatRGBA4444;
      break;
    case HAL_PIXEL_FORMAT_BGRA_8888:
      format = kFormatBGRA8888;
      break;
    case HAL_PIXEL_FORMAT_RGBX_8888:
      format = kFormatRGBX8888;
      break;
    case HAL_PIXEL_FORMAT_BGRX_8888:
      format = kFormatBGRX8888;
      break;
    case HAL_PIXEL_FORMAT_RGB_888:
      format = kFormatRGB888;
      break;
    case HAL_PIXEL_FORMAT_RGB_565:
      format = kFormatRGB565;
      break;
    case HAL_PIXEL_FORMAT_BGR_565:
      format = kFormatBGR565;
      break;
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
      format = kFormatYCbCr420SemiPlanarVenus;
      break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
      format = kFormatYCrCb420SemiPlanarVenus;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
      format = kFormatYCbCr420SPVenusUbwc;
      break;
    case HAL_PIXEL_FORMAT_YV12:
      format = kFormatYCrCb420PlanarStride16;
      break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
      format = kFormatYCrCb420SemiPlanar;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
      format = kFormatYCbCr420SemiPlanar;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
      format = kFormatYCbCr422H2V1SemiPlanar;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
      format = kFormatYCbCr422H2V1Packed;
      break;
    case HAL_PIXEL_FORMAT_RGBA_1010102:
      format = kFormatRGBA1010102;
      break;
    case HAL_PIXEL_FORMAT_ARGB_2101010:
      format = kFormatARGB2101010;
      break;
    case HAL_PIXEL_FORMAT_RGBX_1010102:
      format = kFormatRGBX1010102;
      break;
    case HAL_PIXEL_FORMAT_XRGB_2101010:
      format = kFormatXRGB2101010;
      break;
    case HAL_PIXEL_FORMAT_BGRA_1010102:
      format = kFormatBGRA1010102;
      break;
    case HAL_PIXEL_FORMAT_ABGR_2101010:
      format = kFormatABGR2101010;
      break;
    case HAL_PIXEL_FORMAT_BGRX_1010102:
      format = kFormatBGRX1010102;
      break;
    case HAL_PIXEL_FORMAT_XBGR_2101010:
      format = kFormatXBGR2101010;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_P010:
      format = kFormatYCbCr420P010;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
      format = kFormatYCbCr420TP10Ubwc;
      break;
    default:
      DLOGW("Unsupported format type = %d", source);
      return kFormatInvalid;
  }

  return format;
}

void HWCDisplay::DumpInputBuffers() {
  char dir_path[PATH_MAX];

  if (!dump_frame_count_ || flush_ || !dump_input_layers_) {
    return;
  }

  snprintf(dir_path, sizeof(dir_path), "/data/misc/display/frame_dump_%s", GetDisplayString());

  if (mkdir(dir_path, 0777) != 0 && errno != EEXIST) {
    DLOGW("Failed to create %s directory errno = %d, desc = %s", dir_path, errno, strerror(errno));
    return;
  }

  // if directory exists already, need to explicitly change the permission.
  if (errno == EEXIST && chmod(dir_path, 0777) != 0) {
    DLOGW("Failed to change permissions on %s directory", dir_path);
    return;
  }

  for (uint32_t i = 0; i < layer_stack_.layers.size(); i++) {
    auto layer = layer_stack_.layers.at(i);
    const private_handle_t *pvt_handle =
        reinterpret_cast<const private_handle_t *>(layer->input_buffer->buffer_id);
    auto acquire_fence_fd = layer->input_buffer->acquire_fence_fd;

    if (acquire_fence_fd >= 0) {
      int error = sync_wait(acquire_fence_fd, 1000);
      if (error < 0) {
        DLOGW("sync_wait error errno = %d, desc = %s", errno, strerror(errno));
        return;
      }
    }

    if (pvt_handle && pvt_handle->base) {
      char dump_file_name[PATH_MAX];
      size_t result = 0;

      snprintf(dump_file_name, sizeof(dump_file_name), "%s/input_layer%d_%dx%d_%s_frame%d.raw",
               dir_path, i, pvt_handle->width, pvt_handle->height,
               GetHALPixelFormatString(pvt_handle->format), dump_frame_index_);

      FILE *fp = fopen(dump_file_name, "w+");
      if (fp) {
        result = fwrite(reinterpret_cast<void *>(pvt_handle->base), pvt_handle->size, 1, fp);
        fclose(fp);
      }

      DLOGI("Frame Dump %s: is %s", dump_file_name, result ? "Successful" : "Failed");
    }
  }
}

void HWCDisplay::DumpOutputBuffer(const BufferInfo &buffer_info, void *base, int fence) {
  char dir_path[PATH_MAX];

  snprintf(dir_path, sizeof(dir_path), "/data/misc/display/frame_dump_%s", GetDisplayString());

  if (mkdir(dir_path, 777) != 0 && errno != EEXIST) {
    DLOGW("Failed to create %s directory errno = %d, desc = %s", dir_path, errno, strerror(errno));
    return;
  }

  // if directory exists already, need to explicitly change the permission.
  if (errno == EEXIST && chmod(dir_path, 0777) != 0) {
    DLOGW("Failed to change permissions on %s directory", dir_path);
    return;
  }

  if (base) {
    char dump_file_name[PATH_MAX];
    size_t result = 0;

    if (fence >= 0) {
      int error = sync_wait(fence, 1000);
      if (error < 0) {
        DLOGW("sync_wait error errno = %d, desc = %s", errno, strerror(errno));
        return;
      }
    }

    snprintf(dump_file_name, sizeof(dump_file_name), "%s/output_layer_%dx%d_%s_frame%d.raw",
             dir_path, buffer_info.buffer_config.width, buffer_info.buffer_config.height,
             GetFormatString(buffer_info.buffer_config.format), dump_frame_index_);

    FILE *fp = fopen(dump_file_name, "w+");
    if (fp) {
      result = fwrite(base, buffer_info.alloc_buffer_info.size, 1, fp);
      fclose(fp);
    }

    DLOGI("Frame Dump of %s is %s", dump_file_name, result ? "Successful" : "Failed");
  }
}

const char *HWCDisplay::GetHALPixelFormatString(int format) {
  switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
      return "RGBA_8888";
    case HAL_PIXEL_FORMAT_RGBX_8888:
      return "RGBX_8888";
    case HAL_PIXEL_FORMAT_RGB_888:
      return "RGB_888";
    case HAL_PIXEL_FORMAT_RGB_565:
      return "RGB_565";
    case HAL_PIXEL_FORMAT_BGR_565:
      return "BGR_565";
    case HAL_PIXEL_FORMAT_BGRA_8888:
      return "BGRA_8888";
    case HAL_PIXEL_FORMAT_RGBA_5551:
      return "RGBA_5551";
    case HAL_PIXEL_FORMAT_RGBA_4444:
      return "RGBA_4444";
    case HAL_PIXEL_FORMAT_YV12:
      return "YV12";
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
      return "YCbCr_422_SP_NV16";
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
      return "YCrCb_420_SP_NV21";
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
      return "YCbCr_422_I_YUY2";
    case HAL_PIXEL_FORMAT_YCrCb_422_I:
      return "YCrCb_422_I_YVYU";
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
      return "NV12_ENCODEABLE";
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
      return "YCbCr_420_SP_TILED_TILE_4x2";
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
      return "YCbCr_420_SP";
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO:
      return "YCrCb_420_SP_ADRENO";
    case HAL_PIXEL_FORMAT_YCrCb_422_SP:
      return "YCrCb_422_SP";
    case HAL_PIXEL_FORMAT_R_8:
      return "R_8";
    case HAL_PIXEL_FORMAT_RG_88:
      return "RG_88";
    case HAL_PIXEL_FORMAT_INTERLACE:
      return "INTERLACE";
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
      return "YCbCr_420_SP_VENUS";
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
      return "YCrCb_420_SP_VENUS";
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
      return "YCbCr_420_SP_VENUS_UBWC";
    case HAL_PIXEL_FORMAT_RGBA_1010102:
      return "RGBA_1010102";
    case HAL_PIXEL_FORMAT_ARGB_2101010:
      return "ARGB_2101010";
    case HAL_PIXEL_FORMAT_RGBX_1010102:
      return "RGBX_1010102";
    case HAL_PIXEL_FORMAT_XRGB_2101010:
      return "XRGB_2101010";
    case HAL_PIXEL_FORMAT_BGRA_1010102:
      return "BGRA_1010102";
    case HAL_PIXEL_FORMAT_ABGR_2101010:
      return "ABGR_2101010";
    case HAL_PIXEL_FORMAT_BGRX_1010102:
      return "BGRX_1010102";
    case HAL_PIXEL_FORMAT_XBGR_2101010:
      return "XBGR_2101010";
    case HAL_PIXEL_FORMAT_YCbCr_420_P010:
      return "YCbCr_420_P010";
    case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
      return "YCbCr_420_TP10_UBWC";
    default:
      return "Unknown_format";
  }
}

const char *HWCDisplay::GetDisplayString() {
  switch (type_) {
    case kPrimary:
      return "primary";
    case kHDMI:
      return "hdmi";
    case kVirtual:
      return "virtual";
    default:
      return "invalid";
  }
}

int HWCDisplay::SetFrameBufferResolution(uint32_t x_pixels, uint32_t y_pixels) {
  if (x_pixels <= 0 || y_pixels <= 0) {
    DLOGV("Unsupported config: x_pixels=%d, y_pixels=%d", x_pixels, y_pixels);
    return -EINVAL;
  }

  if (framebuffer_config_->x_pixels == x_pixels && framebuffer_config_->y_pixels == y_pixels) {
    return 0;
  }

  DisplayConfigVariableInfo active_config;
  uint32_t active_config_index = 0;
  display_intf_->GetActiveConfig(&active_config_index);
  DisplayError error = display_intf_->GetConfig(active_config_index, &active_config);
  if (error != kErrorNone) {
    DLOGV("GetConfig variable info failed. Error = %d", error);
    return -EINVAL;
  }

  if (active_config.x_pixels <= 0 || active_config.y_pixels <= 0) {
    DLOGV("Invalid panel resolution (%dx%d)", active_config.x_pixels, active_config.y_pixels);
    return -EINVAL;
  }

  // Create rects to represent the new source and destination crops
  LayerRect crop = LayerRect(0, 0, FLOAT(x_pixels), FLOAT(y_pixels));
  LayerRect dst = LayerRect(0, 0, FLOAT(active_config.x_pixels), FLOAT(active_config.y_pixels));
  // Set rotate90 to false since this is taken care of during regular composition.
  bool rotate90 = false;
  error = display_intf_->IsScalingValid(crop, dst, rotate90);
  if (error != kErrorNone) {
    DLOGV("Unsupported resolution: (%dx%d)", x_pixels, y_pixels);
    return -EINVAL;
  }

  framebuffer_config_->x_pixels = x_pixels;
  framebuffer_config_->y_pixels = y_pixels;
  framebuffer_config_->vsync_period_ns = active_config.vsync_period_ns;
  framebuffer_config_->x_dpi = active_config.x_dpi;
  framebuffer_config_->y_dpi = active_config.y_dpi;

  auto client_target_layer = client_target_->GetSDMLayer();
  client_target_layer->src_rect = crop;
  client_target_layer->dst_rect = dst;

  int aligned_width;
  int aligned_height;
  int usage = GRALLOC_USAGE_HW_FB;
  int format = HAL_PIXEL_FORMAT_RGBA_8888;
  int ubwc_enabled = 0;
  int flags = 0;
  HWCDebugHandler::Get()->GetProperty("debug.gralloc.enable_fb_ubwc", &ubwc_enabled);
  if (ubwc_enabled == 1) {
    usage |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
    flags |= private_handle_t::PRIV_FLAGS_UBWC_ALIGNED;
  }
  AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(INT(x_pixels), INT(y_pixels), format, usage,
                                                        aligned_width, aligned_height);

  // TODO(user): How does the dirty region get set on the client target? File bug on Google
  client_target_layer->composition = kCompositionGPUTarget;
  client_target_layer->input_buffer->format = GetSDMFormat(format, flags);
  client_target_layer->input_buffer->width = UINT32(aligned_width);
  client_target_layer->input_buffer->height = UINT32(aligned_height);
  client_target_layer->plane_alpha = 255;

  DLOGI("New framebuffer resolution (%dx%d)", framebuffer_config_->x_pixels,
        framebuffer_config_->y_pixels);

  return 0;
}

void HWCDisplay::GetFrameBufferResolution(uint32_t *x_pixels, uint32_t *y_pixels) {
  *x_pixels = framebuffer_config_->x_pixels;
  *y_pixels = framebuffer_config_->y_pixels;
}

void HWCDisplay::ScaleDisplayFrame(hwc_rect_t *display_frame) {
  if (!IsFrameBufferScaled()) {
    return;
  }

  uint32_t active_config_index = 0;
  display_intf_->GetActiveConfig(&active_config_index);
  DisplayConfigVariableInfo active_config;
  DisplayError error = display_intf_->GetConfig(active_config_index, &active_config);
  if (error != kErrorNone) {
    DLOGE("GetConfig variable info failed. Error = %d", error);
    return;
  }

  float custom_x_pixels = FLOAT(framebuffer_config_->x_pixels);
  float custom_y_pixels = FLOAT(framebuffer_config_->y_pixels);
  float active_x_pixels = FLOAT(active_config.x_pixels);
  float active_y_pixels = FLOAT(active_config.y_pixels);
  float x_pixels_ratio = active_x_pixels / custom_x_pixels;
  float y_pixels_ratio = active_y_pixels / custom_y_pixels;
  float layer_width = FLOAT(display_frame->right - display_frame->left);
  float layer_height = FLOAT(display_frame->bottom - display_frame->top);

  display_frame->left = INT(x_pixels_ratio * FLOAT(display_frame->left));
  display_frame->top = INT(y_pixels_ratio * FLOAT(display_frame->top));
  display_frame->right = INT(FLOAT(display_frame->left) + layer_width * x_pixels_ratio);
  display_frame->bottom = INT(FLOAT(display_frame->top) + layer_height * y_pixels_ratio);
}

bool HWCDisplay::IsFrameBufferScaled() {
  if (framebuffer_config_->x_pixels == 0 || framebuffer_config_->y_pixels == 0) {
    return false;
  }
  uint32_t panel_x_pixels = 0;
  uint32_t panel_y_pixels = 0;
  GetPanelResolution(&panel_x_pixels, &panel_y_pixels);
  return (framebuffer_config_->x_pixels != panel_x_pixels) ||
         (framebuffer_config_->y_pixels != panel_y_pixels);
}

void HWCDisplay::GetPanelResolution(uint32_t *x_pixels, uint32_t *y_pixels) {
  DisplayConfigVariableInfo active_config;
  uint32_t active_config_index = 0;
  display_intf_->GetActiveConfig(&active_config_index);
  DisplayError error = display_intf_->GetConfig(active_config_index, &active_config);
  if (error != kErrorNone) {
    DLOGE("GetConfig variable info failed. Error = %d", error);
    return;
  }
  *x_pixels = active_config.x_pixels;
  *y_pixels = active_config.y_pixels;
}

int HWCDisplay::SetDisplayStatus(uint32_t display_status) {
  int status = 0;

  switch (display_status) {
    case kDisplayStatusResume:
      display_paused_ = false;
    case kDisplayStatusOnline:
      status = INT32(SetPowerMode(HWC2::PowerMode::On));
      break;
    case kDisplayStatusPause:
      display_paused_ = true;
    case kDisplayStatusOffline:
      status = INT32(SetPowerMode(HWC2::PowerMode::Off));
      break;
    default:
      DLOGW("Invalid display status %d", display_status);
      return -EINVAL;
  }

  if (display_status == kDisplayStatusResume || display_status == kDisplayStatusPause) {
    callbacks_->Refresh(HWC_DISPLAY_PRIMARY);
  }

  return status;
}

HWC2::Error HWCDisplay::SetCursorPosition(hwc2_layer_t layer, int x, int y) {
  if (shutdown_pending_) {
    return HWC2::Error::None;
  }

  // TODO(user): Validate layer
  // TODO(user): Check if we're in a validate/present cycle

  auto error = display_intf_->SetCursorPosition(x, y);
  if (error != kErrorNone) {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return HWC2::Error::None;
    }

    DLOGE("Failed for x = %d y = %d, Error = %d", x, y, error);
    return HWC2::Error::BadDisplay;
  }

  return HWC2::Error::None;
}

int HWCDisplay::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  DisplayError error = display_intf_->OnMinHdcpEncryptionLevelChange(min_enc_level);
  if (error != kErrorNone) {
    DLOGE("Failed. Error = %d", error);
    return -1;
  }

  return 0;
}

void HWCDisplay::MarkLayersForGPUBypass() {
  for (auto hwc_layer : layer_set_) {
    auto layer = hwc_layer->GetSDMLayer();
    layer->composition = kCompositionSDE;
  }
}

void HWCDisplay::ApplyScanAdjustment(hwc_rect_t *display_frame) {
}

int HWCDisplay::SetPanelBrightness(int level) {
  int ret = 0;
  if (display_intf_)
    ret = display_intf_->SetPanelBrightness(level);
  else
    ret = -EINVAL;

  return ret;
}

int HWCDisplay::GetPanelBrightness(int *level) {
  return display_intf_->GetPanelBrightness(level);
}

int HWCDisplay::ToggleScreenUpdates(bool enable) {
  display_paused_ = enable ? false : true;
  callbacks_->Refresh(HWC_DISPLAY_PRIMARY);
  return 0;
}

int HWCDisplay::ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload,
                                     PPDisplayAPIPayload *out_payload,
                                     PPPendingParams *pending_action) {
  int ret = 0;

  if (display_intf_)
    ret = display_intf_->ColorSVCRequestRoute(in_payload, out_payload, pending_action);
  else
    ret = -EINVAL;

  return ret;
}

int HWCDisplay::GetVisibleDisplayRect(hwc_rect_t *visible_rect) {
  if (!IsValid(display_rect_)) {
    return -EINVAL;
  }

  visible_rect->left = INT(display_rect_.left);
  visible_rect->top = INT(display_rect_.top);
  visible_rect->right = INT(display_rect_.right);
  visible_rect->bottom = INT(display_rect_.bottom);
  DLOGI("Dpy = %d Visible Display Rect(%d %d %d %d)", visible_rect->left, visible_rect->top,
        visible_rect->right, visible_rect->bottom);

  return 0;
}

void HWCDisplay::SetSecureDisplay(bool secure_display_active) {
  secure_display_active_ = secure_display_active;
  return;
}

int HWCDisplay::SetActiveDisplayConfig(int config) {
  return display_intf_->SetActiveConfig(UINT32(config)) == kErrorNone ? 0 : -1;
}

int HWCDisplay::GetActiveDisplayConfig(uint32_t *config) {
  return display_intf_->GetActiveConfig(config) == kErrorNone ? 0 : -1;
}

int HWCDisplay::GetDisplayConfigCount(uint32_t *count) {
  return display_intf_->GetNumVariableInfoConfigs(count) == kErrorNone ? 0 : -1;
}

int HWCDisplay::GetDisplayAttributesForConfig(int config, DisplayConfigVariableInfo *attributes) {
  return display_intf_->GetConfig(UINT32(config), attributes) == kErrorNone ? 0 : -1;
}

bool HWCDisplay::SingleLayerUpdating(void) {
  uint32_t updating_count = 0;

  for (uint i = 0; i < layer_set_.size(); i++) {
    auto layer = layer_stack_.layers.at(i);
    if (layer->flags.updating) {
      updating_count++;
    }
  }

  return (updating_count == 1);
}

uint32_t HWCDisplay::SanitizeRefreshRate(uint32_t req_refresh_rate) {
  uint32_t refresh_rate = req_refresh_rate;

  if (refresh_rate < min_refresh_rate_) {
    // Pick the next multiple of request which is within the range
    refresh_rate =
        (((min_refresh_rate_ / refresh_rate) + ((min_refresh_rate_ % refresh_rate) ? 1 : 0)) *
         refresh_rate);
  }

  if (refresh_rate > max_refresh_rate_) {
    refresh_rate = max_refresh_rate_;
  }

  return refresh_rate;
}

DisplayClass HWCDisplay::GetDisplayClass() {
  return display_class_;
}

void HWCDisplay::CloseAcquireFds() {
  for (auto hwc_layer : layer_set_) {
    auto layer = hwc_layer->GetSDMLayer();
    if (layer->input_buffer->acquire_fence_fd >= 0) {
      close(layer->input_buffer->acquire_fence_fd);
    }
  }
  int32_t &client_target_acquire_fence =
      client_target_->GetSDMLayer()->input_buffer->acquire_fence_fd;
  if (client_target_acquire_fence >= 0) {
    close(client_target_acquire_fence);
    client_target_acquire_fence = -1;
  }
}

}  // namespace sdm
