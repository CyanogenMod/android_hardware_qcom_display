/*
* Copyright (c) 2014 - 2016, The Linux Foundation. All rights reserved.
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
#include <utils/rect.h>
#include <utils/debug.h>
#include <sync/sync.h>
#include <cutils/properties.h>
#include <map>
#include <utility>

#include "hwc_display.h"
#include "hwc_debugger.h"
#include "blit_engine_c2d.h"

#ifdef QTI_BSP
#include <hardware/display_defs.h>
#endif

#define __CLASS__ "HWCDisplay"

namespace sdm {

static void AssignLayerRegionsAddress(LayerRectArray *region, uint32_t rect_count,
                                      uint8_t **base_address) {
  if (rect_count) {
    region->rect = reinterpret_cast<LayerRect *>(*base_address);
    for (uint32_t i = 0; i < rect_count; i++) {
      region->rect[i] = LayerRect();
    }
    *base_address += rect_count * sizeof(LayerRect);
  }
  region->count = rect_count;
}

static void ApplyDeInterlaceAdjustment(Layer *layer) {
  // De-interlacing adjustment
  if (layer->input_buffer->flags.interlace) {
    float height = (layer->src_rect.bottom - layer->src_rect.top) / 2.0f;
    layer->src_rect.top = ROUND_UP_ALIGN_DOWN(layer->src_rect.top / 2.0f, 2);
    layer->src_rect.bottom = layer->src_rect.top + floorf(height);
  }
}

HWCDisplay::HWCDisplay(CoreInterface *core_intf, hwc_procs_t const **hwc_procs, DisplayType type,
                       int id, bool needs_blit)
  : core_intf_(core_intf), hwc_procs_(hwc_procs), type_(type), id_(id), needs_blit_(needs_blit) {
}

int HWCDisplay::Init() {
  DisplayError error = core_intf_->CreateDisplay(type_, this, &display_intf_);
  if (error != kErrorNone) {
    DLOGE("Display create failed. Error = %d display_type %d event_handler %p disp_intf %p",
      error, type_, this, &display_intf_);
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

  if (needs_blit_) {
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

  s3d_format_hwc_to_sdm_.insert(std::pair<int, LayerBufferS3DFormat>(HAL_NO_3D, kS3dFormatNone));
  s3d_format_hwc_to_sdm_.insert(std::pair<int, LayerBufferS3DFormat>(HAL_3D_SIDE_BY_SIDE_L_R,
                                kS3dFormatLeftRight));
  s3d_format_hwc_to_sdm_.insert(std::pair<int, LayerBufferS3DFormat>(HAL_3D_SIDE_BY_SIDE_R_L,
                                kS3dFormatRightLeft));
  s3d_format_hwc_to_sdm_.insert(std::pair<int, LayerBufferS3DFormat>(HAL_3D_TOP_BOTTOM,
                                kS3dFormatTopBottom));

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
    blit_engine_ = NULL;
  }

  return 0;
}

int HWCDisplay::EventControl(int event, int enable) {
  DisplayError error = kErrorNone;

  if (shutdown_pending_) {
    return 0;
  }

  switch (event) {
  case HWC_EVENT_VSYNC:
    error = display_intf_->SetVSyncState(enable);
    break;
  default:
    DLOGW("Unsupported event = %d", event);
  }

  if (error != kErrorNone) {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return 0;
    }
    DLOGE("Failed. event = %d, enable = %d, error = %d", event, enable, error);
    return -EINVAL;
  }

  return 0;
}

int HWCDisplay::SetPowerMode(int mode) {
  DLOGI("display = %d, mode = %d", id_, mode);
  DisplayState state = kStateOff;
  bool flush_on_error = flush_on_error_;

  if (shutdown_pending_) {
    return 0;
  }

  switch (mode) {
  case HWC_POWER_MODE_OFF:
    // During power off, all of the buffers are released.
    // Do not flush until a buffer is successfully submitted again.
    flush_on_error = false;
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
  if (error == kErrorNone) {
    flush_on_error_ = flush_on_error;
  } else {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return 0;
    }
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
  DisplayConfigVariableInfo variable_config = *framebuffer_config_;

  for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
    switch (attributes[i]) {
    case HWC_DISPLAY_VSYNC_PERIOD:
      values[i] = INT32(variable_config.vsync_period_ns);
      break;
    case HWC_DISPLAY_WIDTH:
      values[i] = INT32(variable_config.x_pixels);
      break;
    case HWC_DISPLAY_HEIGHT:
      values[i] = INT32(variable_config.y_pixels);
      break;
    case HWC_DISPLAY_DPI_X:
      values[i] = INT32(variable_config.x_dpi * 1000.0f);
      break;
    case HWC_DISPLAY_DPI_Y:
      values[i] = INT32(variable_config.y_dpi * 1000.0f);
      break;
    default:
      DLOGW("Spurious attribute type = %d", attributes[i]);
      return -EINVAL;
    }
  }

  return 0;
}

int HWCDisplay::GetActiveConfig() {
  return 0;
}

int HWCDisplay::SetActiveConfig(int index) {
  return -1;
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
  const hwc_procs_t *hwc_procs = *hwc_procs_;

  if (!hwc_procs) {
    return kErrorParameters;
  }

  hwc_procs->vsync(hwc_procs, id_, vsync.timestamp);

  return kErrorNone;
}

DisplayError HWCDisplay::Refresh() {
  return kErrorNotSupported;
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

  // Allocate memory for
  //  a) total number of layers
  //  b) buffer handle for each layer
  //  c) number of visible rectangles in each layer
  //  d) number of dirty rectangles in each layer
  //  e) number of blit rectangles in each layer
  size_t required_size = (num_hw_layers + blit_target_count) *
                         (sizeof(Layer) + sizeof(LayerBuffer));

  for (size_t i = 0; i < num_hw_layers + blit_target_count; i++) {
    uint32_t num_visible_rects = 0;
    uint32_t num_dirty_rects = 0;

    if (i < num_hw_layers) {
      hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
      num_visible_rects = UINT32(hwc_layer.visibleRegionScreen.numRects);
      num_dirty_rects = UINT32(hwc_layer.surfaceDamage.numRects);
    }

    // visible rectangles + dirty rectangles + blit rectangle
    size_t num_rects = num_visible_rects + num_dirty_rects + blit_target_count;
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
  layer_stack_.layer_count = UINT32(num_hw_layers + blit_target_count);
  current_address += (num_hw_layers + blit_target_count) * sizeof(Layer);

  for (size_t i = 0; i < num_hw_layers + blit_target_count; i++) {
    uint32_t num_visible_rects = 0;
    uint32_t num_dirty_rects = 0;

    if (i < num_hw_layers) {
      hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
      num_visible_rects = UINT32(hwc_layer.visibleRegionScreen.numRects);
      num_dirty_rects = UINT32(hwc_layer.surfaceDamage.numRects);
    }

    Layer &layer = layer_stack_.layers[i];
    layer = Layer();

    // Layer buffer handle address
    layer.input_buffer = reinterpret_cast<LayerBuffer *>(current_address);
    *layer.input_buffer = LayerBuffer();
    current_address += sizeof(LayerBuffer);

    // Visible/Dirty/Blit rectangle address
    AssignLayerRegionsAddress(&layer.visible_regions, num_visible_rects, &current_address);
    AssignLayerRegionsAddress(&layer.dirty_regions, num_dirty_rects, &current_address);
    AssignLayerRegionsAddress(&layer.blit_regions, blit_target_count, &current_address);
  }

  return 0;
}

int HWCDisplay::PrepareLayerParams(hwc_layer_1_t *hwc_layer, Layer *layer) {
  const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer->handle);

  LayerBuffer *layer_buffer = layer->input_buffer;

  if (pvt_handle) {
    layer_buffer->format = GetSDMFormat(pvt_handle->format, pvt_handle->flags);
    layer_buffer->width = UINT32(pvt_handle->width);
    layer_buffer->height = UINT32(pvt_handle->height);

    if (SetMetaData(pvt_handle, layer) != kErrorNone) {
      return -EINVAL;
    }

    if (pvt_handle->bufferType == BUFFER_TYPE_VIDEO) {
      layer_stack_.flags.video_present = true;
      layer_buffer->flags.video = true;
    }
    // TZ Protected Buffer - L1
    if (pvt_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
      layer_stack_.flags.secure_present = true;
      layer_buffer->flags.secure = true;
    }
    // Gralloc Usage Protected Buffer - L3 - which needs to be treated as Secure & avoid fallback
    if (pvt_handle->flags & private_handle_t::PRIV_FLAGS_PROTECTED_BUFFER) {
      layer_stack_.flags.secure_present = true;
    }
    if (pvt_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_DISPLAY) {
      layer_buffer->flags.secure_display = true;
    }

    // check if this is special solid_fill layer without input_buffer.
    if (solid_fill_enable_ && pvt_handle->fd == -1) {
      layer->flags.solid_fill = true;
      layer->solid_fill_color = solid_fill_color_;
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
      int flags = 0;
      HWCDebugHandler::Get()->GetProperty("debug.gralloc.enable_fb_ubwc", &ubwc_enabled);
      if (ubwc_enabled == 1) {
        usage |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
        flags |= private_handle_t::PRIV_FLAGS_UBWC_ALIGNED;
      }

      GetFrameBufferResolution(&x_pixels, &y_pixels);

      AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(INT(x_pixels), INT(y_pixels), format,
                                                            usage, aligned_width, aligned_height);
      layer_buffer->width = UINT32(aligned_width);
      layer_buffer->height = UINT32(aligned_height);
      layer_buffer->format = GetSDMFormat(format, flags);
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
    layer_buffer->planes[0].stride = UINT32(pvt_handle->width);
    layer_buffer->size = pvt_handle->size;
  }

  // if swapinterval property is set to 0 then close and reset the acquireFd
  if (swap_interval_zero_ && hwc_layer->acquireFenceFd >= 0) {
    close(hwc_layer->acquireFenceFd);
    hwc_layer->acquireFenceFd = -1;
  }
  layer_buffer->acquire_fence_fd = hwc_layer->acquireFenceFd;
}

int HWCDisplay::PrePrepareLayerStack(hwc_display_contents_1_t *content_list) {
  if (shutdown_pending_) {
    return 0;
  }

  size_t num_hw_layers = content_list->numHwLayers;

  use_blit_comp_ = false;
  metadata_refresh_rate_ = 0;
  display_rect_ = LayerRect();

  // Configure each layer
  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];

    Layer &layer = layer_stack_.layers[i];

    int ret = PrepareLayerParams(&content_list->hwLayers[i], &layer_stack_.layers[i]);

    if (ret != kErrorNone) {
      return ret;
    }

    layer.flags.skip = ((hwc_layer.flags & HWC_SKIP_LAYER) > 0);
    layer.flags.solid_fill = (hwc_layer.flags & kDimLayer) || solid_fill_enable_;
    if (layer.flags.skip || layer.flags.solid_fill) {
      layer.dirty_regions.count = 0;
    }

    hwc_rect_t scaled_display_frame = hwc_layer.displayFrame;
    ScaleDisplayFrame(&scaled_display_frame);
    ApplyScanAdjustment(&scaled_display_frame);

    SetRect(scaled_display_frame, &layer.dst_rect);
    SetRect(hwc_layer.sourceCropf, &layer.src_rect);
    ApplyDeInterlaceAdjustment(&layer);

    for (uint32_t j = 0; j < layer.visible_regions.count; j++) {
      SetRect(hwc_layer.visibleRegionScreen.rects[j], &layer.visible_regions.rect[j]);
    }
    for (uint32_t j = 0; j < layer.dirty_regions.count; j++) {
      SetRect(hwc_layer.surfaceDamage.rects[j], &layer.dirty_regions.rect[j]);
    }
    SetComposition(hwc_layer.compositionType, &layer.composition);

    if (hwc_layer.compositionType != HWC_FRAMEBUFFER_TARGET) {
      display_rect_ = Union(display_rect_, layer.dst_rect);
    }


    // For dim layers, SurfaceFlinger
    //    - converts planeAlpha to per pixel alpha,
    //    - sets RGB color to 000,
    //    - sets planeAlpha to 0xff,
    //    - blending to Premultiplied.
    // This can be achieved at hardware by
    //    - solid fill ARGB to 0xff000000,
    //    - incoming planeAlpha,
    //    - blending to Coverage.
    if (hwc_layer.flags & kDimLayer) {
      layer.input_buffer->format = kFormatARGB8888;
      layer.solid_fill_color = 0xff000000;
      SetBlending(HWC_BLENDING_COVERAGE, &layer.blending);
    } else {
      SetBlending(hwc_layer.blending, &layer.blending);
      LayerTransform &layer_transform = layer.transform;
      uint32_t &hwc_transform = hwc_layer.transform;
      layer_transform.flip_horizontal = ((hwc_transform & HWC_TRANSFORM_FLIP_H) > 0);
      layer_transform.flip_vertical = ((hwc_transform & HWC_TRANSFORM_FLIP_V) > 0);
      layer_transform.rotation = ((hwc_transform & HWC_TRANSFORM_ROT_90) ? 90.0f : 0.0f);
    }

    // TODO(user): Remove below block.
    // For solid fill, only dest rect need to be specified.
    if (layer.flags.solid_fill) {
      LayerBuffer *input_buffer = layer.input_buffer;
      input_buffer->width = UINT32(layer.dst_rect.right - layer.dst_rect.left);
      input_buffer->height = UINT32(layer.dst_rect.bottom - layer.dst_rect.top);
      layer.src_rect.left = 0;
      layer.src_rect.top = 0;
      layer.src_rect.right = input_buffer->width;
      layer.src_rect.bottom = input_buffer->height;
    }

    layer.plane_alpha = hwc_layer.planeAlpha;
    layer.flags.cursor = ((hwc_layer.flags & HWC_IS_CURSOR_LAYER) > 0);
    layer.flags.updating = true;

    if (num_hw_layers <= kMaxLayerCount) {
      layer.flags.updating = IsLayerUpdating(content_list, INT32(i));
    }
#ifdef QTI_BSP
    if (hwc_layer.flags & HWC_SCREENSHOT_ANIMATOR_LAYER) {
      layer_stack_.flags.animating = true;
    }
#endif
    if (layer.flags.skip) {
      layer_stack_.flags.skip_present = true;
    }

    if (layer.flags.cursor) {
      layer_stack_.flags.cursor_present = true;
    }

    if (layer.frame_rate > metadata_refresh_rate_) {
      metadata_refresh_rate_ = SanitizeRefreshRate(layer.frame_rate);
    } else {
      layer.frame_rate = current_refresh_rate_;
    }

    layer.input_buffer->buffer_id = reinterpret_cast<uint64_t>(hwc_layer.handle);
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

  return 0;
}

int HWCDisplay::PrepareLayerStack(hwc_display_contents_1_t *content_list) {
  if (shutdown_pending_) {
    return 0;
  }

  size_t num_hw_layers = content_list->numHwLayers;

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

      return 0;
    }
  } else {
    // Skip is not set
    MarkLayersForGPUBypass(content_list);
    skip_prepare_ = false;
    DLOGI("SecureDisplay %s, Skip Prepare/Commit and Flush", secure_display_active_ ? "Starting" :
          "Stopping");
    flush_ = true;
  }

  // If current draw cycle has different set of layers updating in comparison to previous cycle,
  // cache content using GPU again.
  // If set of updating layers remains same, use cached buffer and replace layers marked for GPU
  // composition with SDE so that SurfaceFlinger does not compose them. Set cache inuse here.
  bool needs_fb_refresh = NeedsFrameBufferRefresh(content_list);
  layer_stack_cache_.in_use = false;

  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    Layer &layer = layer_stack_.layers[i];
    LayerComposition composition = layer.composition;

    if ((composition == kCompositionSDE) || (composition == kCompositionHybrid) ||
        (composition == kCompositionBlit)) {
      hwc_layer.hints |= HWC_HINT_CLEAR_FB;
    }

    if (!needs_fb_refresh && composition == kCompositionGPU) {
      composition = kCompositionSDE;
      layer_stack_cache_.in_use = true;
    }
    SetComposition(composition, &hwc_layer.compositionType);
  }

  CacheLayerStackInfo(content_list);

  return 0;
}

int HWCDisplay::CommitLayerStack(hwc_display_contents_1_t *content_list) {
  if (!content_list || !content_list->numHwLayers) {
    DLOGW("Invalid content list");
    return -EINVAL;
  }

  if (shutdown_pending_) {
    return 0;
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

    if (error == kErrorNone) {
      // A commit is successfully submitted, start flushing on failure now onwards.
      flush_on_error_ = true;
    } else {
      if (error == kErrorShutDown) {
        shutdown_pending_ = true;
        return status;
      } else if (error != kErrorPermission) {
        DLOGE("Commit failed. Error = %d", error);
        // To prevent surfaceflinger infinite wait, flush the previous frame during Commit()
        // so that previous buffer and fences are released, and override the error.
        flush_ = true;
      }
    }
  }

  return status;
}

int HWCDisplay::PostCommitLayerStack(hwc_display_contents_1_t *content_list) {
  size_t num_hw_layers = content_list->numHwLayers;
  int status = 0;

  // Do no call flush on errors, if a successful buffer is never submitted.
  if (flush_ && flush_on_error_) {
    display_intf_->Flush();
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
      // If swapinterval property is set to 0 or for single buffer layers, do not update f/w
      // release fences and discard fences from driver
      if (swap_interval_zero_ || layer.flags.single_buffer) {
        hwc_layer.releaseFenceFd = -1;
        close(layer_buffer->release_fence_fd);
        layer_buffer->release_fence_fd = -1;
      } else if (layer.composition != kCompositionGPU) {
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

  // Handle ongoing animation and end here, start is handled below
  if (layer_stack_cache_.animating) {
      if (!layer_stack_.flags.animating) {
        // Animation is ending.
        return true;
      } else {
        // Animation is going on.
        return false;
      }
  }

  // Frame buffer needs to be refreshed for the following reasons:
  // 1. Any layer is marked skip in the current layer stack.
  // 2. Any layer is added/removed/layer properties changes in the current layer stack.
  // 3. Any layer handle is changed and it is marked for GPU composition
  // 4. Any layer's current composition is different from previous composition.
  if (layer_stack_.flags.skip_present || layer_stack_.flags.geometry_changed) {
    return true;
  }

  for (uint32_t i = 0; i < layer_count; i++) {
    Layer &layer = layer_stack_.layers[i];
    LayerCache &layer_cache = layer_stack_cache_.layer_cache[i];

    // need FB refresh for s3d case
    if (layer.input_buffer->s3d_format != kS3dFormatNone) {
        return true;
    }

    if (layer.composition == kCompositionGPUTarget) {
      continue;
    }

    if (layer_cache.composition != layer.composition) {
      return true;
    }

    if ((layer.composition == kCompositionGPU) && IsLayerUpdating(content_list, INT32(i))) {
      return true;
    }
  }

  return false;
}

bool HWCDisplay::IsLayerUpdating(hwc_display_contents_1_t *content_list, int layer_index) {
  hwc_layer_1_t &hwc_layer = content_list->hwLayers[layer_index];
  LayerCache &layer_cache = layer_stack_cache_.layer_cache[layer_index];

  const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer.handle);
  const MetaData_t *meta_data = pvt_handle ?
    reinterpret_cast<MetaData_t *>(pvt_handle->base_metadata) : NULL;

  // Layer should be considered updating if
  //   a) layer is in single buffer mode, or
  //   b) layer handle has changed, or
  //   c) layer plane alpha has changed, or
  //   d) layer stack geometry has changed
  return ((meta_data && (meta_data->operation & SET_SINGLE_BUFFER_MODE) &&
              meta_data->isSingleBufferMode) ||
          (layer_cache.handle != hwc_layer.handle) ||
          (layer_cache.plane_alpha != hwc_layer.planeAlpha) ||
          (content_list->flags & HWC_GEOMETRY_CHANGED));
}

void HWCDisplay::CacheLayerStackInfo(hwc_display_contents_1_t *content_list) {
  uint32_t layer_count = layer_stack_.layer_count;

  if (layer_count > kMaxLayerCount || layer_stack_.flags.animating) {
    ResetLayerCacheStack();
    return;
  }

  for (uint32_t i = 0; i < layer_count; i++) {
    Layer &layer = layer_stack_.layers[i];
    if (layer.composition == kCompositionGPUTarget ||
        layer.composition == kCompositionBlitTarget) {
      continue;
    }

    LayerCache &layer_cache = layer_stack_cache_.layer_cache[i];
    layer_cache.handle = content_list->hwLayers[i].handle;
    layer_cache.plane_alpha = content_list->hwLayers[i].planeAlpha;
    layer_cache.composition = layer.composition;
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

void HWCDisplay::SetComposition(const LayerComposition &source, int32_t *target) {
  switch (source) {
  case kCompositionGPUTarget:   *target = HWC_FRAMEBUFFER_TARGET; break;
  case kCompositionGPU:         *target = HWC_FRAMEBUFFER;        break;
  case kCompositionHWCursor:    *target = HWC_CURSOR_OVERLAY;     break;
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
    case HAL_PIXEL_FORMAT_RGBA_8888:           format = kFormatRGBA8888Ubwc;            break;
    case HAL_PIXEL_FORMAT_RGBX_8888:           format = kFormatRGBX8888Ubwc;            break;
    case HAL_PIXEL_FORMAT_BGR_565:             format = kFormatBGR565Ubwc;              break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:     format = kFormatYCbCr420SPVenusUbwc;     break;
    case HAL_PIXEL_FORMAT_RGBA_1010102:        format = kFormatRGBA1010102Ubwc;         break;
    case HAL_PIXEL_FORMAT_RGBX_1010102:        format = kFormatRGBX1010102Ubwc;         break;
    case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC: format = kFormatYCbCr420TP10Ubwc;        break;
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
  case HAL_PIXEL_FORMAT_BGR_565:                  format = kFormatBGR565;                   break;
  case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
  case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:       format = kFormatYCbCr420SemiPlanarVenus;  break;
  case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:       format = kFormatYCrCb420SemiPlanarVenus;  break;
  case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:  format = kFormatYCbCr420SPVenusUbwc;      break;
  case HAL_PIXEL_FORMAT_YV12:                     format = kFormatYCrCb420PlanarStride16;   break;
  case HAL_PIXEL_FORMAT_YCrCb_420_SP:             format = kFormatYCrCb420SemiPlanar;       break;
  case HAL_PIXEL_FORMAT_YCbCr_420_SP:             format = kFormatYCbCr420SemiPlanar;       break;
  case HAL_PIXEL_FORMAT_YCbCr_422_SP:             format = kFormatYCbCr422H2V1SemiPlanar;   break;
  case HAL_PIXEL_FORMAT_YCbCr_422_I:              format = kFormatYCbCr422H2V1Packed;       break;
  case HAL_PIXEL_FORMAT_RGBA_1010102:             format = kFormatRGBA1010102;              break;
  case HAL_PIXEL_FORMAT_ARGB_2101010:             format = kFormatARGB2101010;              break;
  case HAL_PIXEL_FORMAT_RGBX_1010102:             format = kFormatRGBX1010102;              break;
  case HAL_PIXEL_FORMAT_XRGB_2101010:             format = kFormatXRGB2101010;              break;
  case HAL_PIXEL_FORMAT_BGRA_1010102:             format = kFormatBGRA1010102;              break;
  case HAL_PIXEL_FORMAT_ABGR_2101010:             format = kFormatABGR2101010;              break;
  case HAL_PIXEL_FORMAT_BGRX_1010102:             format = kFormatBGRX1010102;              break;
  case HAL_PIXEL_FORMAT_XBGR_2101010:             format = kFormatXBGR2101010;              break;
  case HAL_PIXEL_FORMAT_YCbCr_420_P010:           format = kFormatYCbCr420P010;             break;
  case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:      format = kFormatYCbCr420TP10Ubwc;         break;
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
  const hwc_procs_t *hwc_procs = *hwc_procs_;

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

  if (display_status == kDisplayStatusResume ||
      display_status == kDisplayStatusPause) {
    hwc_procs->invalidate(hwc_procs);
  }

  return status;
}

int HWCDisplay::SetCursorPosition(int x, int y) {
  DisplayError error = kErrorNone;

  if (shutdown_pending_) {
    return 0;
  }

  error = display_intf_->SetCursorPosition(x, y);
  if (error != kErrorNone) {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return 0;
    }
    DLOGE("Failed for x = %d y = %d, Error = %d", x, y, error);
    return -1;
  }

  return 0;
}

int HWCDisplay::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  DisplayError error = display_intf_->OnMinHdcpEncryptionLevelChange(min_enc_level);
  if (error != kErrorNone) {
    DLOGE("Failed. Error = %d", error);
    return -1;
  }

  return 0;
}

void HWCDisplay::MarkLayersForGPUBypass(hwc_display_contents_1_t *content_list) {
  for (size_t i = 0 ; i < (content_list->numHwLayers - 1); i++) {
    hwc_layer_1_t *layer = &content_list->hwLayers[i];
    layer->compositionType = HWC_OVERLAY;
  }
}

uint32_t HWCDisplay::RoundToStandardFPS(uint32_t fps) {
  static const uint32_t standard_fps[4] = {30, 24, 48, 60};

  int count = INT(sizeof(standard_fps) / sizeof(standard_fps[0]));
  for (int i = 0; i < count; i++) {
    if ((standard_fps[i] - fps) < 2) {
      // Most likely used for video, the fps can fluctuate
      // Ex: b/w 29 and 30 for 30 fps clip
      return standard_fps[i];
    }
  }

  return fps;
}

void HWCDisplay::ApplyScanAdjustment(hwc_rect_t *display_frame) {
}

DisplayError HWCDisplay::SetCSC(ColorSpace_t source, LayerCSC *target) {
  switch (source) {
  case ITU_R_601:       *target = kCSCLimitedRange601;   break;
  case ITU_R_601_FR:    *target = kCSCFullRange601;      break;
  case ITU_R_709:       *target = kCSCLimitedRange709;   break;
  default:
    DLOGE("Unsupported CSC: %d", source);
    return kErrorNotSupported;
  }

  return kErrorNone;
}

DisplayError HWCDisplay::SetIGC(IGC_t source, LayerIGC *target) {
  switch (source) {
  case IGC_NotSpecified:    *target = kIGCNotSpecified; break;
  case IGC_sRGB:            *target = kIGCsRGB;   break;
  default:
    DLOGE("Unsupported IGC: %d", source);
    return kErrorNotSupported;
  }

  return kErrorNone;
}

DisplayError HWCDisplay::SetMetaData(const private_handle_t *pvt_handle, Layer *layer) {
  const MetaData_t *meta_data = reinterpret_cast<MetaData_t *>(pvt_handle->base_metadata);
  LayerBuffer *layer_buffer = layer->input_buffer;

  if (!meta_data) {
    return kErrorNone;
  }

  if (meta_data->operation & UPDATE_COLOR_SPACE) {
    if (SetCSC(meta_data->colorSpace, &layer->csc) != kErrorNone) {
      return kErrorNotSupported;
    }
  }

  if (meta_data->operation & SET_IGC) {
    if (SetIGC(meta_data->igc, &layer->igc) != kErrorNone) {
      return kErrorNotSupported;
    }
  }

  if (meta_data->operation & UPDATE_REFRESH_RATE) {
    layer->frame_rate = RoundToStandardFPS(meta_data->refreshrate);
  }

  if ((meta_data->operation & PP_PARAM_INTERLACED) && meta_data->interlaced) {
    layer_buffer->flags.interlace = true;
  }

  if (meta_data->operation & LINEAR_FORMAT) {
    layer_buffer->format = GetSDMFormat(INT32(meta_data->linearFormat), 0);
  }

  if (meta_data->operation & UPDATE_BUFFER_GEOMETRY) {
    int actual_width = pvt_handle->width;
    int actual_height = pvt_handle->height;
    AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(pvt_handle, actual_width, actual_height);
    layer_buffer->width = UINT32(actual_width);
    layer_buffer->height = UINT32(actual_height);
  }

  if (meta_data->operation & SET_SINGLE_BUFFER_MODE) {
    layer->flags.single_buffer = meta_data->isSingleBufferMode;
    // Graphics can set this operation on all types of layers including FB and set the actual value
    // to 0. To protect against SET operations of 0 value, we need to do a logical OR.
    layer_stack_.flags.single_buffered_layer_present |= meta_data->isSingleBufferMode;
  }

  if (meta_data->operation & S3D_FORMAT) {
    std::map<int, LayerBufferS3DFormat>::iterator it =
        s3d_format_hwc_to_sdm_.find(INT32(meta_data->s3dFormat));
    if (it != s3d_format_hwc_to_sdm_.end()) {
      layer->input_buffer->s3d_format = it->second;
    } else {
      DLOGW("Invalid S3D format %d", meta_data->s3dFormat);
    }
  }

  return kErrorNone;
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
  const hwc_procs_t *hwc_procs = *hwc_procs_;
  display_paused_ = enable ? false : true;
  hwc_procs->invalidate(hwc_procs);
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

int HWCDisplay::GetVisibleDisplayRect(hwc_rect_t* visible_rect) {
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

void HWCDisplay::ResetLayerCacheStack() {
  uint32_t layer_count = layer_stack_cache_.layer_count;
  for (uint32_t i = 0; i < layer_count; i++) {
    layer_stack_cache_.layer_cache[i] = LayerCache();
  }
  layer_stack_cache_.layer_count = 0;
  layer_stack_cache_.animating = false;
  layer_stack_cache_.in_use = false;
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

bool HWCDisplay::SingleLayerUpdating(uint32_t app_layer_count) {
  uint32_t updating_count = 0;

  for (uint i = 0; i < app_layer_count; i++) {
    Layer &layer = layer_stack_.layers[i];
    if (layer.flags.updating) {
      updating_count++;
    }
  }

  return (updating_count == 1);
}

uint32_t HWCDisplay::SanitizeRefreshRate(uint32_t req_refresh_rate) {
  uint32_t refresh_rate = req_refresh_rate;

  if (refresh_rate < min_refresh_rate_) {
    // Pick the next multiple of request which is within the range
    refresh_rate = (((min_refresh_rate_ / refresh_rate) +
                     ((min_refresh_rate_ % refresh_rate) ? 1 : 0)) * refresh_rate);
  }

  if (refresh_rate > max_refresh_rate_) {
    refresh_rate = max_refresh_rate_;
  }

  return refresh_rate;
}

}  // namespace sdm
