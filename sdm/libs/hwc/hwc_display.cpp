/*
* Copyright (c) 2014 - 2015, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>
#include <errno.h>
#include <gralloc_priv.h>
#include <gr.h>
#include <utils/constants.h>
#include <sync/sync.h>
#include <cutils/properties.h>

#include "hwc_display.h"
#include "hwc_debugger.h"
#include "blit_engine_c2d.h"

#define __CLASS__ "HWCDisplay"

namespace sdm {

HWCDisplay::HWCDisplay(CoreInterface *core_intf, hwc_procs_t const **hwc_procs, DisplayType type,
                       int id, bool needs_blit)
  : core_intf_(core_intf), hwc_procs_(hwc_procs), type_(type), id_(id), needs_blit_(needs_blit),
    display_intf_(NULL), flush_(false), dump_frame_count_(0), dump_frame_index_(0),
    dump_input_layers_(false), swap_interval_zero_(false), framebuffer_config_(NULL),
    display_paused_(false), use_metadata_refresh_rate_(false), metadata_refresh_rate_(0),
    boot_animation_completed_(false), use_blit_comp_(false), blit_engine_(NULL) {
}

int HWCDisplay::Init() {
  DisplayError error = core_intf_->CreateDisplay(type_, this, &display_intf_);
  if (error != kErrorNone) {
    DLOGE("Display create failed. Error = %d display_type %d event_handler %p disp_intf %p",
      error, type_, this, &display_intf_);
    return -EINVAL;
  }

  int property_swap_interval = 1;
  if (HWCDebugHandler::Get()->GetProperty("debug.egl.swapinterval", &property_swap_interval)) {
    if (property_swap_interval == 0) {
      swap_interval_zero_ = true;
    }
  }

  framebuffer_config_ = new DisplayConfigVariableInfo();
  if (!framebuffer_config_) {
    DLOGV("Failed to allocate memory for custom framebuffer config.");
    core_intf_->DestroyDisplay(display_intf_);
    return -EINVAL;
  }

  if (needs_blit_) {
    blit_engine_ = new BlitEngineC2d();
    if (!blit_engine_) {
      DLOGI("Create Blit Engine C2D failed");
    } else {
      if (blit_engine_->Init() < 0) {
        DLOGI("Blit Engine Init failed, Blit Composition will not be used!!");
        delete blit_engine_;
        blit_engine_= NULL;
      }
    }
  }

  return 0;
}

int HWCDisplay::Deinit() {
  DisplayError error = core_intf_->DestroyDisplay(display_intf_);
  if (error != kErrorNone) {
    DLOGE("Display destroy failed. Error = %d", error);
    return -EINVAL;
  }

  if (layer_stack_memory_.raw) {
    delete[] layer_stack_memory_.raw;
    layer_stack_memory_.raw = NULL;
  }

  delete framebuffer_config_;

  if (blit_engine_) {
    blit_engine_->DeInit();
    delete blit_engine_;
    blit_engine_= NULL;
  }

  return 0;
}

int HWCDisplay::EventControl(int event, int enable) {
  DisplayError error = kErrorNone;

  switch (event) {
  case HWC_EVENT_VSYNC:
    error = display_intf_->SetVSyncState(enable);
    break;
  case HWC_EVENT_ORIENTATION:
    // TODO(user): Need to handle this case
    break;
  default:
    DLOGW("Unsupported event = %d", event);
  }

  if (error != kErrorNone) {
    DLOGE("Failed. event = %d, enable = %d, error = %d", event, enable, error);
    return -EINVAL;
  }

  return 0;
}

int HWCDisplay::SetPowerMode(int mode) {
  DLOGI("display = %d, mode = %d", id_, mode);
  DisplayState state = kStateOff;

  switch (mode) {
  case HWC_POWER_MODE_OFF:
    state = kStateOff;
    break;
  case HWC_POWER_MODE_NORMAL:
    state = kStateOn;
    last_power_mode_ = HWC_POWER_MODE_NORMAL;
    break;
  case HWC_POWER_MODE_DOZE:
    state = kStateDoze;
    last_power_mode_ = HWC_POWER_MODE_DOZE;
    break;
  case HWC_POWER_MODE_DOZE_SUSPEND:
    state = kStateDozeSuspend;
    last_power_mode_ = HWC_POWER_MODE_DOZE_SUSPEND;
    break;
  default:
    return -EINVAL;
  }

  DisplayError error = display_intf_->SetDisplayState(state);
  if (error != kErrorNone) {
    DLOGE("Set state failed. Error = %d", error);
    return -EINVAL;
  }

  return 0;
}

int HWCDisplay::GetDisplayConfigs(uint32_t *configs, size_t *num_configs) {
  if (*num_configs > 0) {
    configs[0] = 0;
    *num_configs = 1;
  }

  return 0;
}

int HWCDisplay::GetDisplayAttributes(uint32_t config, const uint32_t *attributes, int32_t *values) {
  DisplayError error = kErrorNone;

  DisplayConfigVariableInfo variable_config;
  uint32_t active_config = UINT32(GetActiveConfig());
  if (IsFrameBufferScaled() && config == active_config) {
    variable_config = *framebuffer_config_;
  } else {
    error = display_intf_->GetConfig(config, &variable_config);
    if (error != kErrorNone) {
      DLOGE("GetConfig variable info failed. Error = %d", error);
      return -EINVAL;
    }
  }

  for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
    switch (attributes[i]) {
    case HWC_DISPLAY_VSYNC_PERIOD:
      values[i] = variable_config.vsync_period_ns;
      break;
    case HWC_DISPLAY_WIDTH:
      values[i] = variable_config.x_pixels;
      break;
    case HWC_DISPLAY_HEIGHT:
      values[i] = variable_config.y_pixels;
      break;
    case HWC_DISPLAY_DPI_X:
      values[i] = INT32(variable_config.x_dpi * 1000.0f);
      break;
    case HWC_DISPLAY_DPI_Y:
      values[i] = INT32(variable_config.y_dpi * 1000.0f);
      break;
    case HWC_DISPLAY_SECURE:
      values[i] = INT32(true);  // For backward compatibility. All Physical displays are secure
      break;
    default:
      DLOGW("Spurious attribute type = %d", attributes[i]);
      return -EINVAL;
    }
  }

  return 0;
}

int HWCDisplay::GetActiveConfig() {
  DisplayError error = kErrorNone;
  uint32_t index = 0;

  error = display_intf_->GetActiveConfig(&index);
  if (error != kErrorNone) {
    DLOGE("GetActiveConfig failed. Error = %d", error);
    return -1;
  }

  return index;
}

int HWCDisplay::SetActiveConfig(int index) {
  DisplayError error = kErrorNone;

  error = display_intf_->SetActiveConfig(index);
  if (error != kErrorNone) {
    DLOGE("SetActiveConfig failed. Error = %d", error);
    return -1;
  }

  return 0;
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

uint32_t HWCDisplay::GetLastPowerMode() {
  return last_power_mode_;
}

DisplayError HWCDisplay::VSync(const DisplayEventVSync &vsync) {
  if (*hwc_procs_) {
    (*hwc_procs_)->vsync(*hwc_procs_, id_, vsync.timestamp);
  }

  return kErrorNone;
}

DisplayError HWCDisplay::Refresh() {
  if (*hwc_procs_) {
    (*hwc_procs_)->invalidate(*hwc_procs_);
  }

  return kErrorNone;
}

int HWCDisplay::AllocateLayerStack(hwc_display_contents_1_t *content_list) {
  if (!content_list || !content_list->numHwLayers) {
    DLOGW("Invalid content list");
    return -EINVAL;
  }

  size_t num_hw_layers = content_list->numHwLayers;
  uint32_t blit_target_count = 0;

  if (needs_blit_ && blit_engine_) {
    blit_target_count = kMaxBlitTargetLayers;
  }

  // Allocate memory for a) total number of layers b) buffer handle for each layer c) number of
  // visible rectangles in each layer d) dirty rectangle for each layer
  size_t required_size = (num_hw_layers + blit_target_count) *
                         (sizeof(Layer) + sizeof(LayerBuffer));

  for (size_t i = 0; i < num_hw_layers + blit_target_count; i++) {
    uint32_t num_visible_rects = 1;
    if (i < num_hw_layers) {
      num_visible_rects = INT32(content_list->hwLayers[i].visibleRegionScreen.numRects);
    }

    // visible rectangles + 1 dirty rectangle + blit rectangle
    size_t num_rects = num_visible_rects + 1 + blit_target_count;
    required_size += num_rects * sizeof(LayerRect);
  }

  // Layer array may be large enough to hold current number of layers.
  // If not, re-allocate it now.
  if (layer_stack_memory_.size < required_size) {
    if (layer_stack_memory_.raw) {
      delete[] layer_stack_memory_.raw;
      layer_stack_memory_.size = 0;
    }

    // Allocate in multiple of kSizeSteps.
    required_size = ROUND_UP(required_size, layer_stack_memory_.kSizeSteps);
    layer_stack_memory_.raw = new uint8_t[required_size];
    if (!layer_stack_memory_.raw) {
      return -ENOMEM;
    }

    layer_stack_memory_.size = required_size;
  }

  // Assign memory addresses now.
  uint8_t *current_address = layer_stack_memory_.raw;

  // Layer array address
  layer_stack_ = LayerStack();
  layer_stack_.layers = reinterpret_cast<Layer *>(current_address);
  layer_stack_.layer_count = INT32(num_hw_layers + blit_target_count);
  current_address += (num_hw_layers + blit_target_count) * sizeof(Layer);

  for (size_t i = 0; i < num_hw_layers + blit_target_count; i++) {
    uint32_t num_visible_rects = 1;
    if (i < num_hw_layers) {
      num_visible_rects =
        static_cast<uint32_t>(content_list->hwLayers[i].visibleRegionScreen.numRects);
    }

    Layer &layer = layer_stack_.layers[i];
    layer = Layer();

    // Layer buffer handle address
    layer.input_buffer = reinterpret_cast<LayerBuffer *>(current_address);
    *layer.input_buffer = LayerBuffer();
    current_address += sizeof(LayerBuffer);

    // Visible rectangle address
    layer.visible_regions.rect = reinterpret_cast<LayerRect *>(current_address);
    layer.visible_regions.count = num_visible_rects;
    for (size_t i = 0; i < layer.visible_regions.count; i++) {
      layer.visible_regions.rect[i] = LayerRect();
    }
    current_address += num_visible_rects * sizeof(LayerRect);

    // Dirty rectangle address
    layer.dirty_regions.rect = reinterpret_cast<LayerRect *>(current_address);
    layer.dirty_regions.count = 1;
    *layer.dirty_regions.rect = LayerRect();
    current_address += sizeof(LayerRect);

    // Blit rectangle address
    layer.blit_regions.rect = reinterpret_cast<LayerRect *>(current_address);
    layer.blit_regions.count = blit_target_count;
    for (size_t i = 0; i < layer.blit_regions.count; i++) {
      layer.blit_regions.rect[i] = LayerRect();
    }
    current_address += layer.blit_regions.count * sizeof(LayerRect);
  }

  return 0;
}

int HWCDisplay::PrepareLayerParams(hwc_layer_1_t *hwc_layer, Layer *layer, uint32_t fps) {
  const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer->handle);

  LayerBuffer *layer_buffer = layer->input_buffer;

  if (pvt_handle) {
    layer_buffer->format = GetSDMFormat(pvt_handle->format, pvt_handle->flags);
    if (layer_buffer->format == kFormatInvalid) {
      return -EINVAL;
    }

    layer_buffer->width = pvt_handle->width;
    layer_buffer->height = pvt_handle->height;
    if (pvt_handle->bufferType == BUFFER_TYPE_VIDEO) {
      layer_stack_.flags.video_present = true;
      layer_buffer->flags.video = true;
    }
    if (pvt_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
      layer_stack_.flags.secure_present = true;
      layer_buffer->flags.secure = true;
    }

    layer->frame_rate = fps;
    const MetaData_t *meta_data = reinterpret_cast<MetaData_t *>(pvt_handle->base_metadata);
    if (meta_data && (SetMetaData(*meta_data, layer) != kErrorNone)) {
      return -EINVAL;
    }

    if (pvt_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_DISPLAY) {
      layer_buffer->flags.secure_display = true;
    }
  } else {
    // for FBT layer
    if (hwc_layer->compositionType == HWC_FRAMEBUFFER_TARGET) {
      uint32_t x_pixels;
      uint32_t y_pixels;
      int aligned_width;
      int aligned_height;
      int usage = GRALLOC_USAGE_HW_FB;
      int format = HAL_PIXEL_FORMAT_RGBA_8888;
      int ubwc_enabled = 0;
      HWCDebugHandler::Get()->GetProperty("debug.gralloc.enable_fb_ubwc", &ubwc_enabled);
      if (ubwc_enabled == 1) {
        usage |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
      }

      GetFrameBufferResolution(&x_pixels, &y_pixels);

      AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(INT(x_pixels), INT(y_pixels), format,
                                                            usage, aligned_width, aligned_height);
      layer_buffer->width = aligned_width;
      layer_buffer->height = aligned_height;
      layer->frame_rate = fps;
    }
  }

  return 0;
}

void HWCDisplay::CommitLayerParams(hwc_layer_1_t *hwc_layer, Layer *layer) {
  const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer->handle);
  LayerBuffer *layer_buffer = layer->input_buffer;

  if (pvt_handle) {
    layer_buffer->planes[0].fd = pvt_handle->fd;
    layer_buffer->planes[0].offset = pvt_handle->offset;
    layer_buffer->planes[0].stride = pvt_handle->width;
  }

  // if swapinterval property is set to 0 then close and reset the acquireFd
  if (swap_interval_zero_ && hwc_layer->acquireFenceFd >= 0) {
    close(hwc_layer->acquireFenceFd);
    hwc_layer->acquireFenceFd = -1;
  }
  layer_buffer->acquire_fence_fd = hwc_layer->acquireFenceFd;
}

int HWCDisplay::PrepareLayerStack(hwc_display_contents_1_t *content_list) {
  if (!content_list || !content_list->numHwLayers) {
    DLOGW("Invalid content list");
    return -EINVAL;
  }

  size_t num_hw_layers = content_list->numHwLayers;
  if (num_hw_layers <= 1) {
    flush_ = true;
    return 0;
  }

  DisplayConfigVariableInfo active_config;
  uint32_t active_config_index = 0;
  display_intf_->GetActiveConfig(&active_config_index);
  int ret;

  display_intf_->GetConfig(active_config_index, &active_config);
  use_blit_comp_ = false;

  // Configure each layer
  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer.handle);

    Layer &layer = layer_stack_.layers[i];
    LayerBuffer *layer_buffer = layer.input_buffer;

    ret = PrepareLayerParams(&content_list->hwLayers[i], &layer_stack_.layers[i],
                             active_config.fps);

    if (ret != kErrorNone) {
      return ret;
    }

    hwc_rect_t scaled_display_frame = hwc_layer.displayFrame;
    ScaleDisplayFrame(&scaled_display_frame);
    ApplyScanAdjustment(&scaled_display_frame);

    SetRect(scaled_display_frame, &layer.dst_rect);
    SetRect(hwc_layer.sourceCropf, &layer.src_rect);
    for (size_t j = 0; j < hwc_layer.visibleRegionScreen.numRects; j++) {
      SetRect(hwc_layer.visibleRegionScreen.rects[j], &layer.visible_regions.rect[j]);
    }
    SetRect(hwc_layer.dirtyRect, &layer.dirty_regions.rect[0]);
    SetComposition(hwc_layer.compositionType, &layer.composition);
    SetBlending(hwc_layer.blending, &layer.blending);

    LayerTransform &layer_transform = layer.transform;
    uint32_t &hwc_transform = hwc_layer.transform;
    layer_transform.flip_horizontal = ((hwc_transform & HWC_TRANSFORM_FLIP_H) > 0);
    layer_transform.flip_vertical = ((hwc_transform & HWC_TRANSFORM_FLIP_V) > 0);
    layer_transform.rotation = ((hwc_transform & HWC_TRANSFORM_ROT_90) ? 90.0f : 0.0f);

    layer.plane_alpha = hwc_layer.planeAlpha;
    layer.flags.skip = ((hwc_layer.flags & HWC_SKIP_LAYER) > 0);
    if (num_hw_layers <= kMaxLayerCount) {
      layer.flags.updating = (layer_stack_cache_.layer_cache[i].handle != hwc_layer.handle);
    } else {
      layer.flags.updating = true;
    }

    if (hwc_layer.flags & HWC_SCREENSHOT_ANIMATOR_LAYER) {
      layer_stack_.flags.animating = true;
    }

    if (layer.flags.skip) {
      layer_stack_.flags.skip_present = true;
    }
  }

  // Prepare the Blit Target
  if (blit_engine_) {
    int ret = blit_engine_->Prepare(&layer_stack_);
    if (ret) {
      // Blit engine cannot handle this layer stack, hence set the layer stack
      // count to num_hw_layers
      layer_stack_.layer_count -= kMaxBlitTargetLayers;
    } else {
      use_blit_comp_ = true;
    }
  }

  // Configure layer stack
  layer_stack_.flags.geometry_changed = ((content_list->flags & HWC_GEOMETRY_CHANGED) > 0);

  DisplayError error = display_intf_->Prepare(&layer_stack_);
  if (error != kErrorNone) {
    DLOGE("Prepare failed. Error = %d", error);

    // To prevent surfaceflinger infinite wait, flush the previous frame during Commit() so that
    // previous buffer and fences are released, and override the error.
    flush_ = true;

    return 0;
  }

  bool needs_fb_refresh = NeedsFrameBufferRefresh(content_list);

  metadata_refresh_rate_ = 0;

  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    Layer &layer = layer_stack_.layers[i];
    LayerComposition composition = layer.composition;

    if ((composition == kCompositionSDE) || (composition == kCompositionHybrid) ||
        (composition == kCompositionBlit)) {
      hwc_layer.hints |= HWC_HINT_CLEAR_FB;

      if (use_metadata_refresh_rate_ && layer.frame_rate > metadata_refresh_rate_) {
        metadata_refresh_rate_ = layer.frame_rate;
      }
    }

    // If current layer does not need frame buffer redraw, then mark it as HWC_OVERLAY
    if (!needs_fb_refresh && (composition != kCompositionGPUTarget)) {
      composition = kCompositionSDE;
    }

    SetComposition(composition, &hwc_layer.compositionType);
  }

  // Cache the current layer stack information like layer_count, composition type and layer handle
  // for the future.
  CacheLayerStackInfo(content_list);

  return 0;
}

int HWCDisplay::CommitLayerStack(hwc_display_contents_1_t *content_list) {
  if (!content_list || !content_list->numHwLayers) {
    DLOGW("Invalid content list");
    return -EINVAL;
  }

  int status = 0;

  size_t num_hw_layers = content_list->numHwLayers;

  DumpInputBuffers(content_list);

  if (!flush_) {
    for (size_t i = 0; i < num_hw_layers; i++) {
      CommitLayerParams(&content_list->hwLayers[i], &layer_stack_.layers[i]);
    }

    if (use_blit_comp_) {
      status = blit_engine_->PreCommit(content_list, &layer_stack_);
      if (status == 0) {
        status = blit_engine_->Commit(content_list, &layer_stack_);
        if (status != 0) {
          DLOGE("Blit Comp Failed!");
        }
      }
    }

    DisplayError error = kErrorUndefined;
    if (status == 0) {
      error = display_intf_->Commit(&layer_stack_);
      status = 0;
    }
    if (error != kErrorNone) {
      DLOGE("Commit failed. Error = %d", error);
      // To prevent surfaceflinger infinite wait, flush the previous frame during Commit() so that
      // previous buffer and fences are released, and override the error.
      flush_ = true;
    }
  }

  return status;
}

int HWCDisplay::PostCommitLayerStack(hwc_display_contents_1_t *content_list) {
  size_t num_hw_layers = content_list->numHwLayers;
  int status = 0;

  if (flush_) {
    DisplayError error = display_intf_->Flush();
    if (error != kErrorNone) {
      DLOGE("Flush failed. Error = %d", error);
    }
  }

  // Set the release fence fd to the blit engine
  if (use_blit_comp_ && blit_engine_->BlitActive()) {
    blit_engine_->PostCommit(&layer_stack_);
  }

  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    Layer &layer = layer_stack_.layers[i];
    LayerBuffer *layer_buffer = layer_stack_.layers[i].input_buffer;

    if (!flush_) {
      // if swapinterval property is set to 0 then do not update f/w release fences with driver
      // values
      if (swap_interval_zero_) {
        hwc_layer.releaseFenceFd = -1;
        close(layer_buffer->release_fence_fd);
        layer_buffer->release_fence_fd = -1;
      }

      if (layer.composition != kCompositionGPU) {
        hwc_layer.releaseFenceFd = layer_buffer->release_fence_fd;
      }

      // During animation on external/virtual display, SDM will use the cached
      // framebuffer layer throughout animation and do not allow framework to do eglswapbuffer on
      // framebuffer target. So graphics doesn't close the release fence fd of framebuffer target,
      // Hence close the release fencefd of framebuffer target here.
      if (layer.composition == kCompositionGPUTarget && layer_stack_cache_.animating) {
        close(hwc_layer.releaseFenceFd);
        hwc_layer.releaseFenceFd = -1;
      }
    }

    if (hwc_layer.acquireFenceFd >= 0) {
      close(hwc_layer.acquireFenceFd);
      hwc_layer.acquireFenceFd = -1;
    }
  }

  if (!flush_) {
    layer_stack_cache_.animating = layer_stack_.flags.animating;

    // if swapinterval property is set to 0 then close and reset the list retire fence
    if (swap_interval_zero_) {
      close(layer_stack_.retire_fence_fd);
      layer_stack_.retire_fence_fd = -1;
    }
    content_list->retireFenceFd = layer_stack_.retire_fence_fd;

    if (dump_frame_count_) {
      dump_frame_count_--;
      dump_frame_index_++;
    }
  }

  flush_ = false;

  return status;
}


bool HWCDisplay::NeedsFrameBufferRefresh(hwc_display_contents_1_t *content_list) {
  uint32_t layer_count = layer_stack_.layer_count;

  // Frame buffer needs to be refreshed for the following reasons:
  // 1. Any layer is marked skip in the current layer stack.
  // 2. Any layer is added/removed/layer properties changes in the current layer stack.
  // 3. Any layer handle is changed and it is marked for GPU composition
  // 4. Any layer's current composition is different from previous composition.
  if (((layer_stack_cache_.layer_count != layer_count) || layer_stack_.flags.skip_present ||
      layer_stack_.flags.geometry_changed) && !layer_stack_cache_.animating) {
    return true;
  }

  for (uint32_t i = 0; i < layer_count; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    Layer &layer = layer_stack_.layers[i];
    LayerCache &layer_cache = layer_stack_cache_.layer_cache[i];

    if (layer.composition == kCompositionGPUTarget) {
      continue;
    }

    if (layer_cache.composition != layer.composition) {
      return true;
    }

    if ((layer.composition == kCompositionGPU) && (layer_cache.handle != hwc_layer.handle) &&
        !layer_stack_cache_.animating) {
      return true;
    }
  }

  return false;
}

void HWCDisplay::CacheLayerStackInfo(hwc_display_contents_1_t *content_list) {
  uint32_t layer_count = layer_stack_.layer_count;

  if (layer_count > kMaxLayerCount) {
    return;
  }

  for (uint32_t i = 0; i < layer_count; i++) {
    Layer &layer = layer_stack_.layers[i];

    if (layer.composition == kCompositionGPUTarget ||
        layer.composition == kCompositionBlitTarget) {
      continue;
    }

    layer_stack_cache_.layer_cache[i].handle = content_list->hwLayers[i].handle;
    layer_stack_cache_.layer_cache[i].composition = layer.composition;
  }

  layer_stack_cache_.layer_count = layer_count;
}

void HWCDisplay::SetRect(const hwc_rect_t &source, LayerRect *target) {
  target->left = FLOAT(source.left);
  target->top = FLOAT(source.top);
  target->right = FLOAT(source.right);
  target->bottom = FLOAT(source.bottom);
}

void HWCDisplay::SetRect(const hwc_frect_t &source, LayerRect *target) {
  target->left = floorf(source.left);
  target->top = floorf(source.top);
  target->right = ceilf(source.right);
  target->bottom = ceilf(source.bottom);
}

void HWCDisplay::SetComposition(const int32_t &source, LayerComposition *target) {
  switch (source) {
  case HWC_FRAMEBUFFER_TARGET:  *target = kCompositionGPUTarget;  break;
  default:                      *target = kCompositionGPU;        break;
  }
}

void HWCDisplay::SetComposition(const int32_t &source, int32_t *target) {
  switch (source) {
  case kCompositionGPUTarget:   *target = HWC_FRAMEBUFFER_TARGET; break;
  case kCompositionGPU:         *target = HWC_FRAMEBUFFER;        break;
  default:                      *target = HWC_OVERLAY;            break;
  }
}

void HWCDisplay::SetBlending(const int32_t &source, LayerBlending *target) {
  switch (source) {
  case HWC_BLENDING_PREMULT:    *target = kBlendingPremultiplied;   break;
  case HWC_BLENDING_COVERAGE:   *target = kBlendingCoverage;        break;
  default:                      *target = kBlendingOpaque;          break;
  }
}

void HWCDisplay::SetIdleTimeoutMs(uint32_t timeout_ms) {
  if (display_intf_) {
    display_intf_->SetIdleTimeoutMs(timeout_ms);
  }
}

DisplayError HWCDisplay::SetMaxMixerStages(uint32_t max_mixer_stages) {
  DisplayError error = kErrorNone;

  if (display_intf_) {
    error = display_intf_->SetMaxMixerStages(max_mixer_stages);
  }

  return error;
}

LayerBufferFormat HWCDisplay::GetSDMFormat(const int32_t &source, const int flags) {
  LayerBufferFormat format = kFormatInvalid;
  if (flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
    switch (source) {
    case HAL_PIXEL_FORMAT_RGBA_8888:          format = kFormatRGBA8888Ubwc;            break;
    case HAL_PIXEL_FORMAT_RGBX_8888:          format = kFormatRGBX8888Ubwc;            break;
    case HAL_PIXEL_FORMAT_RGB_565:            format = kFormatRGB565Ubwc;              break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:    format = kFormatYCbCr420SPVenusUbwc;     break;
    default:
      DLOGE("Unsupported format type for UBWC %d", source);
      return kFormatInvalid;
    }
    return format;
  }

  switch (source) {
  case HAL_PIXEL_FORMAT_RGBA_8888:                format = kFormatRGBA8888;                 break;
  case HAL_PIXEL_FORMAT_RGBA_5551:                format = kFormatRGBA5551;                 break;
  case HAL_PIXEL_FORMAT_RGBA_4444:                format = kFormatRGBA4444;                 break;
  case HAL_PIXEL_FORMAT_BGRA_8888:                format = kFormatBGRA8888;                 break;
  case HAL_PIXEL_FORMAT_RGBX_8888:                format = kFormatRGBX8888;                 break;
  case HAL_PIXEL_FORMAT_BGRX_8888:                format = kFormatBGRX8888;                 break;
  case HAL_PIXEL_FORMAT_RGB_888:                  format = kFormatRGB888;                   break;
  case HAL_PIXEL_FORMAT_RGB_565:                  format = kFormatRGB565;                   break;
  case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
  case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:       format = kFormatYCbCr420SemiPlanarVenus;  break;
  case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:  format = kFormatYCbCr420SPVenusUbwc;      break;
  case HAL_PIXEL_FORMAT_YCrCb_420_SP:             format = kFormatYCrCb420SemiPlanar;       break;
  case HAL_PIXEL_FORMAT_YCbCr_422_SP:             format = kFormatYCbCr422H2V1SemiPlanar;   break;
  case HAL_PIXEL_FORMAT_YCbCr_422_I:              format = kFormatYCbCr422H2V1Packed;       break;
  default:
    DLOGW("Unsupported format type = %d", source);
    return kFormatInvalid;
  }

  return format;
}

void HWCDisplay::DumpInputBuffers(hwc_display_contents_1_t *content_list) {
  size_t num_hw_layers = content_list->numHwLayers;
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

  for (uint32_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer.handle);

    if (hwc_layer.acquireFenceFd >= 0) {
      int error = sync_wait(hwc_layer.acquireFenceFd, 1000);
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

      FILE* fp = fopen(dump_file_name, "w+");
      if (fp) {
        result = fwrite(reinterpret_cast<void *>(pvt_handle->base), pvt_handle->size, 1, fp);
        fclose(fp);
      }

      DLOGI("Frame Dump %s: is %s", dump_file_name, result ? "Successful" : "Failed");
    }
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
  case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
    return "YCbCr_420_SP_VENUS_UBWC";
  default:
    return "Unknown pixel format";
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
  int active_config_index = GetActiveConfig();
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

  uint32_t panel_width =
          UINT32((FLOAT(active_config.x_pixels) * 25.4f) / FLOAT(active_config.x_dpi));
  uint32_t panel_height =
          UINT32((FLOAT(active_config.y_pixels) * 25.4f) / FLOAT(active_config.y_dpi));
  framebuffer_config_->x_pixels = x_pixels;
  framebuffer_config_->y_pixels = y_pixels;
  framebuffer_config_->vsync_period_ns = active_config.vsync_period_ns;
  framebuffer_config_->x_dpi =
          (FLOAT(framebuffer_config_->x_pixels) * 25.4f) / FLOAT(panel_width);
  framebuffer_config_->y_dpi =
          (FLOAT(framebuffer_config_->y_pixels) * 25.4f) / FLOAT(panel_height);

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

  int active_config_index = GetActiveConfig();
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
  int active_config_index = GetActiveConfig();
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
    status = SetPowerMode(HWC_POWER_MODE_NORMAL);
    break;
  case kDisplayStatusPause:
    display_paused_ = true;
  case kDisplayStatusOffline:
    status = SetPowerMode(HWC_POWER_MODE_OFF);
    break;
  default:
    DLOGW("Invalid display status %d", display_status);
    return -EINVAL;
  }

  return status;
}

void HWCDisplay::MarkLayersForGPUBypass(hwc_display_contents_1_t *content_list) {
  for (size_t i = 0 ; i < (content_list->numHwLayers - 1); i++) {
    hwc_layer_1_t *layer = &content_list->hwLayers[i];
    layer->compositionType = HWC_OVERLAY;
  }
}

void HWCDisplay::CloseAcquireFences(hwc_display_contents_1_t *content_list) {
  for (size_t i = 0; i < content_list->numHwLayers; i++) {
    if (content_list->hwLayers[i].acquireFenceFd >= 0) {
      close(content_list->hwLayers[i].acquireFenceFd);
      content_list->hwLayers[i].acquireFenceFd = -1;
    }
  }
}

uint32_t HWCDisplay::RoundToStandardFPS(uint32_t fps) {
  static const uint32_t standard_fps[4] = {30, 24, 48, 60};

  int count = INT(sizeof(standard_fps) / sizeof(standard_fps[0]));
  for (int i = 0; i < count; i++) {
    if (abs(standard_fps[i] - fps) < 2) {
      // Most likely used for video, the fps can fluctuate
      // Ex: b/w 29 and 30 for 30 fps clip
      return standard_fps[i];
    }
  }

  return fps;
}

void HWCDisplay::ApplyScanAdjustment(hwc_rect_t *display_frame) {
}

DisplayError HWCDisplay::SetColorSpace(const ColorSpace_t source, LayerColorSpace *target) {
  switch (source) {
  case ITU_R_601:      *target = kLimitedRange601;   break;
  case ITU_R_601_FR:   *target = kFullRange601;      break;
  case ITU_R_709:      *target = kLimitedRange709;   break;
  default:
    DLOGE("Unsupported Color Space: %d", source);
    return kErrorNotSupported;
  }

  return kErrorNone;
}

DisplayError HWCDisplay::SetMetaData(const MetaData_t &meta_data, Layer *layer) {
  if (meta_data.operation & UPDATE_REFRESH_RATE) {
    layer->frame_rate = RoundToStandardFPS(meta_data.refreshrate);
  }

  if ((meta_data.operation & PP_PARAM_INTERLACED) && meta_data.interlaced) {
    layer->input_buffer->flags.interlace = true;
  }

  if (meta_data.operation & UPDATE_COLOR_SPACE) {
    if (SetColorSpace(meta_data.colorSpace, &layer->color_space) != kErrorNone) {
      return kErrorNotSupported;
    }
  }

  return kErrorNone;
}

int HWCDisplay::ColorSVCRequestRoute(PPDisplayAPIPayload &in_payload,
                               PPDisplayAPIPayload *out_payload, PPPendingParams *pending_action) {
  int ret = 0;

  if (display_intf_)
    ret = display_intf_->ColorSVCRequestRoute(in_payload, out_payload, pending_action);
  else
    ret = -EINVAL;

  return ret;
}

}  // namespace sdm

