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

#include <math.h>
#include <utils/constants.h>
#include <utils/debug.h>

#include "res_manager.h"

#define __CLASS__ "ResManager"

namespace sde {

ResManager::ResManager()
  : num_pipe_(0), vig_pipes_(NULL), rgb_pipes_(NULL), dma_pipes_(NULL), virtual_count_(0),
    buffer_allocator_(NULL), buffer_sync_handler_(NULL) {
}

DisplayError ResManager::Init(const HWResourceInfo &hw_res_info, BufferAllocator *buffer_allocator,
                              BufferSyncHandler *buffer_sync_handler) {
  hw_res_info_ = hw_res_info;

  buffer_sync_handler_ = buffer_sync_handler;

  if (!hw_res_info_.max_mixer_width)
    hw_res_info_.max_mixer_width = kMaxSourcePipeWidth;

  buffer_allocator_ = buffer_allocator;

  buffer_sync_handler_ = buffer_sync_handler;

  DisplayError error = kErrorNone;

  // TODO(user): Remove this. Disable src_split as kernel not supported yet
  hw_res_info_.is_src_split = false;
  num_pipe_ = hw_res_info_.num_vig_pipe + hw_res_info_.num_rgb_pipe + hw_res_info_.num_dma_pipe;

  if (num_pipe_ > kPipeIdMax) {
    DLOGE("Number of pipe is over the limit! %d", num_pipe_);
    return kErrorParameters;
  }

  // Init pipe info
  vig_pipes_ = &src_pipes_[0];
  rgb_pipes_ = &src_pipes_[hw_res_info_.num_vig_pipe];
  dma_pipes_ = &src_pipes_[hw_res_info_.num_vig_pipe + hw_res_info_.num_rgb_pipe];

  for (uint32_t i = 0; i < hw_res_info_.num_vig_pipe; i++) {
    vig_pipes_[i].type = kPipeTypeVIG;
    vig_pipes_[i].index = i;
    vig_pipes_[i].mdss_pipe_id = GetMdssPipeId(vig_pipes_[i].type, i);
  }

  for (uint32_t i = 0; i < hw_res_info_.num_rgb_pipe; i++) {
    rgb_pipes_[i].type = kPipeTypeRGB;
    rgb_pipes_[i].index = i + hw_res_info_.num_vig_pipe;
    rgb_pipes_[i].mdss_pipe_id = GetMdssPipeId(rgb_pipes_[i].type, i);
  }

  for (uint32_t i = 0; i < hw_res_info_.num_dma_pipe; i++) {
    dma_pipes_[i].type = kPipeTypeDMA;
    dma_pipes_[i].index = i + hw_res_info_.num_vig_pipe + hw_res_info_.num_rgb_pipe;
    dma_pipes_[i].mdss_pipe_id = GetMdssPipeId(dma_pipes_[i].type, i);
  }

  for (uint32_t i = 0; i < num_pipe_; i++) {
    src_pipes_[i].priority = i;
  }

  DLOGI("hw_rev=%x, DMA=%d RGB=%d VIG=%d", hw_res_info_.hw_revision, hw_res_info_.num_dma_pipe,
    hw_res_info_.num_rgb_pipe, hw_res_info_.num_vig_pipe);

  // TODO(user): Need to get it from HW capability
  hw_res_info_.num_rotator = 2;

  if (hw_res_info_.num_rotator > kMaxNumRotator) {
    DLOGE("Number of rotator is over the limit! %d", hw_res_info_.num_rotator);
    return kErrorParameters;
  }

  if (hw_res_info_.num_rotator > 0) {
    rotators_[0].pipe_index = dma_pipes_[0].index;
    rotators_[0].writeback_id = kHWWriteback0;
  }
  if (hw_res_info_.num_rotator > 1) {
    rotators_[1].pipe_index = dma_pipes_[1].index;
    rotators_[1].writeback_id = kHWWriteback1;
  }

  // Used by splash screen
  rgb_pipes_[0].state = kPipeStateOwnedByKernel;
  rgb_pipes_[1].state = kPipeStateOwnedByKernel;

  return kErrorNone;
}

DisplayError ResManager::Deinit() {
  return kErrorNone;
}

DisplayError ResManager::RegisterDisplay(DisplayType type, const HWDisplayAttributes &attributes,
                                         Handle *display_ctx) {
  DisplayError error = kErrorNone;

  HWBlockType hw_block_id = kHWBlockMax;
  switch (type) {
  case kPrimary:
    if (!hw_block_ctx_[kHWPrimary].is_in_use) {
      hw_block_id = kHWPrimary;
    }
    break;

  case kHDMI:
    if (!hw_block_ctx_[kHWHDMI].is_in_use) {
      hw_block_id = kHWHDMI;
    }
    break;

  case kVirtual:
    // TODO(user): read this block id from kernel
    virtual_count_++;
    hw_block_id = kHWWriteback2;
    break;

  default:
    DLOGW("RegisterDisplay, invalid type %d", type);
    return kErrorParameters;
  }

  if (hw_block_id == kHWBlockMax) {
    return kErrorResources;
  }

  DisplayResourceContext *display_resource_ctx = new DisplayResourceContext();
  if (!display_resource_ctx) {
    return kErrorMemory;
  }

  display_resource_ctx->buffer_manager = new BufferManager(buffer_allocator_, buffer_sync_handler_);
  if (display_resource_ctx->buffer_manager == NULL) {
    delete display_resource_ctx;
    display_resource_ctx = NULL;
    return kErrorMemory;
  }

  hw_block_ctx_[hw_block_id].is_in_use = true;

  display_resource_ctx->display_attributes = attributes;
  display_resource_ctx->display_type = type;
  display_resource_ctx->hw_block_id = hw_block_id;
  if (!display_resource_ctx->display_attributes.is_device_split)
    display_resource_ctx->display_attributes.split_left =
      display_resource_ctx->display_attributes.x_pixels;

  *display_ctx = display_resource_ctx;
  return error;
}

DisplayError ResManager::UnregisterDisplay(Handle display_ctx) {
  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);

  if (display_resource_ctx->hw_block_id == kHWWriteback2) {
    virtual_count_--;
    if (!virtual_count_)
      hw_block_ctx_[display_resource_ctx->hw_block_id].is_in_use = false;
  } else {
    hw_block_ctx_[display_resource_ctx->hw_block_id].is_in_use = false;
  }

  if (!hw_block_ctx_[display_resource_ctx->hw_block_id].is_in_use)
    Purge(display_ctx);

  delete display_resource_ctx;

  return kErrorNone;
}

DisplayError ResManager::Start(Handle display_ctx) {
  locker_.Lock();

  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);

  if (display_resource_ctx->frame_start) {
    return kErrorNone;  // keep context locked.
  }

  // First call in the cycle
  display_resource_ctx->frame_start = true;
  display_resource_ctx->frame_count++;

  // Release the pipes not used in the previous cycle
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;
  for (uint32_t i = 0; i < num_pipe_; i++) {
    if ((src_pipes_[i].hw_block_id == hw_block_id) &&
        (src_pipes_[i].state == kPipeStateToRelease)) {
      src_pipes_[i].state = kPipeStateIdle;
    }
  }

  // Clear rotator usage
  for (uint32_t i = 0; i < hw_res_info_.num_rotator; i++) {
    uint32_t pipe_index;
    pipe_index = rotators_[i].pipe_index;
    rotators_[i].ClearState(display_resource_ctx->hw_block_id);
    if (rotators_[i].client_bit_mask == 0 &&
        src_pipes_[pipe_index].state == kPipeStateToRelease &&
        src_pipes_[pipe_index].hw_block_id == rotators_[i].writeback_id) {
      src_pipes_[pipe_index].dedicated_hw_block = kHWBlockMax;
      src_pipes_[pipe_index].state = kPipeStateIdle;
    }
  }

  return kErrorNone;
}

DisplayError ResManager::Stop(Handle display_ctx) {
  locker_.Unlock();

  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);

  return kErrorNone;
}

DisplayError ResManager::Acquire(Handle display_ctx, HWLayers *hw_layers) {
  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);

  DisplayError error = kErrorNone;
  const struct HWLayersInfo &layer_info = hw_layers->info;

  if (layer_info.count > num_pipe_) {
    return kErrorResources;
  }

  uint32_t rotate_count = 0;
  error = Config(display_resource_ctx, hw_layers, &rotate_count);
  if (error != kErrorNone) {
    return error;
  }

  uint32_t left_index = kPipeIdMax;
  bool need_scale = false;
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;
  HWBlockType rotator_block = kHWBlockMax;

  // Clear reserved marking
  for (uint32_t i = 0; i < num_pipe_; i++) {
    if (src_pipes_[i].reserved_hw_block == hw_block_id)
      src_pipes_[i].reserved_hw_block = kHWBlockMax;
  }

  // allocate rotator
  error = AcquireRotator(display_resource_ctx, rotate_count);
  if (error != kErrorNone) {
    return error;
  }

  rotate_count = 0;
  for (uint32_t i = 0; i < layer_info.count; i++) {
    Layer &layer = layer_info.stack->layers[layer_info.index[i]];
    struct HWLayerConfig &layer_config = hw_layers->config[i];
    bool use_non_dma_pipe = layer_config.use_non_dma_pipe;

    // TODO(user): set this from comp_manager
    if (hw_block_id == kHWPrimary) {
      use_non_dma_pipe = true;
    }

    for (uint32_t j = 0; j < layer_config.num_rotate; j++) {
      AssignRotator(&layer_config.rotates[j], &rotate_count);
    }

    HWPipeInfo *pipe_info = &layer_config.left_pipe;

    // Should have a generic macro
    bool is_yuv = IsYuvFormat(layer.input_buffer->format);

    // left pipe is needed
    if (pipe_info->valid) {
      need_scale = IsScalingNeeded(pipe_info);
      left_index = GetPipe(hw_block_id, is_yuv, need_scale, false, use_non_dma_pipe);
      if (left_index >= num_pipe_) {
        goto CleanupOnError;
      }
      src_pipes_[left_index].reserved_hw_block = hw_block_id;
    }

    error = SetDecimationFactor(pipe_info);
    if (error != kErrorNone) {
      goto CleanupOnError;
    }

    pipe_info =  &layer_config.right_pipe;
    if (!pipe_info->valid) {
      // assign single pipe
      if (left_index < num_pipe_) {
        layer_config.left_pipe.pipe_id = src_pipes_[left_index].mdss_pipe_id;
        src_pipes_[left_index].at_right = false;
      }
      continue;
    }

    need_scale = IsScalingNeeded(pipe_info);

    uint32_t right_index;
    right_index = GetPipe(hw_block_id, is_yuv, need_scale, true, use_non_dma_pipe);
    if (right_index >= num_pipe_) {
      goto CleanupOnError;
    }

    if (src_pipes_[right_index].priority < src_pipes_[left_index].priority) {
      // Swap pipe based on priority
      Swap(left_index, right_index);
    }

    // assign dual pipes
    pipe_info->pipe_id = src_pipes_[right_index].mdss_pipe_id;
    src_pipes_[right_index].reserved_hw_block = hw_block_id;
    src_pipes_[right_index].at_right = true;
    src_pipes_[left_index].reserved_hw_block = hw_block_id;
    src_pipes_[left_index].at_right = false;
    layer_config.left_pipe.pipe_id = src_pipes_[left_index].mdss_pipe_id;
    error = SetDecimationFactor(pipe_info);
    if (error != kErrorNone) {
      goto CleanupOnError;
    }

    DLOGV_IF(kTagResources, "Pipe acquired, layer index = %d, left_pipe = %x, right_pipe = %x",
            i, layer_config.left_pipe.pipe_id,  pipe_info->pipe_id);
  }

  if (!CheckBandwidth(display_resource_ctx, hw_layers)) {
    DLOGV_IF(kTagResources, "Bandwidth check failed!");
    goto CleanupOnError;
  }

  error = AllocRotatorBuffer(display_ctx, hw_layers);
  if (error != kErrorNone) {
    DLOGV_IF(kTagResources, "Rotator buffer allocation failed");
    goto CleanupOnError;
  }

  return kErrorNone;

CleanupOnError:
  DLOGV_IF(kTagResources, "Resource reserving failed! hw_block = %d", hw_block_id);
  for (uint32_t i = 0; i < num_pipe_; i++) {
    if (src_pipes_[i].reserved_hw_block == hw_block_id)
      src_pipes_[i].reserved_hw_block = kHWBlockMax;
  }
  return kErrorResources;
}

bool ResManager::CheckBandwidth(DisplayResourceContext *display_ctx, HWLayers *hw_layers) {
  // No need to check bandwidth for virtual displays
  if (display_ctx->display_type == kVirtual) {
    return true;
  }

  float max_pipe_bw = FLOAT(hw_res_info_.max_pipe_bw) / 1000000;  // KBps to GBps
  float max_sde_clk = FLOAT(hw_res_info_.max_sde_clk) / 1000000;  // Hz to MHz
  const struct HWLayersInfo &layer_info = hw_layers->info;

  float left_pipe_bw[kMaxSDELayers] = {0};
  float right_pipe_bw[kMaxSDELayers] = {0};
  float left_max_clk = 0;
  float right_max_clk = 0;

  for (uint32_t i = 0; i < layer_info.count; i++) {
    Layer &layer = layer_info.stack->layers[layer_info.index[i]];
    float bpp = GetBpp(layer.input_buffer->format);
    HWPipeInfo *left_pipe = &hw_layers->config[i].left_pipe;
    HWPipeInfo *right_pipe = &hw_layers->config[i].right_pipe;

    left_pipe_bw[i] = left_pipe->valid ? GetPipeBw(display_ctx, left_pipe, bpp) : 0;
    right_pipe_bw[i] = right_pipe->valid ? GetPipeBw(display_ctx, right_pipe, bpp) : 0;

    if ((left_pipe_bw[i] > max_pipe_bw) || (right_pipe_bw[i] > max_pipe_bw)) {
      DLOGV_IF(kTagResources, "Pipe bandwidth exceeds limit for layer index = %d", i);
      return false;
    }

    float left_clk = left_pipe->valid ? GetClockForPipe(display_ctx, left_pipe) : 0;
    float right_clk = right_pipe->valid ? GetClockForPipe(display_ctx, right_pipe) : 0;

    left_max_clk = MAX(left_clk, left_max_clk);
    right_max_clk = MAX(right_clk, right_max_clk);
  }

  float left_mixer_bw = GetOverlapBw(hw_layers, left_pipe_bw, true);
  float right_mixer_bw = GetOverlapBw(hw_layers, right_pipe_bw, false);
  float display_bw = left_mixer_bw + right_mixer_bw;

  // Check system bandwidth (nth External + max(nth, n-1th) Primary)
  if (display_ctx->hw_block_id == kHWPrimary) {
    display_bw = MAX(display_bw, last_primary_bw_);
    last_primary_bw_ = left_mixer_bw + right_mixer_bw;
  }

  // If system has Video mode panel, use max_bandwidth_low, else use max_bandwidth_high
  if ((display_bw + bw_claimed_) > (hw_res_info_.max_bandwidth_low / 1000000)) {
    DLOGV_IF(kTagResources, "Overlap bandwidth exceeds limit!");
    return false;
  }

  // Max clock requirement of display
  float display_clk = MAX(left_max_clk, right_max_clk);

  // Check max clock requirement of system
  float system_clk = MAX(display_clk, clk_claimed_);

  // Apply fudge factor to consider in-efficieny
  if ((system_clk * hw_res_info_.clk_fudge_factor) > max_sde_clk) {
    DLOGV_IF(kTagResources, "Clock requirement exceeds limit!");
    return false;
  }

  // If Primary display, reset claimed bw & clk for next cycle
  if (display_ctx->hw_block_id == kHWPrimary) {
    bw_claimed_ = 0.0f;
    clk_claimed_ = 0.0f;
  } else {
    bw_claimed_ = display_bw;
    clk_claimed_ = display_clk;
  }

  return true;
}

float ResManager::GetPipeBw(DisplayResourceContext *display_ctx, HWPipeInfo *pipe, float bpp) {
  HWDisplayAttributes &display_attributes = display_ctx->display_attributes;
  float src_w = pipe->src_roi.right - pipe->src_roi.left;
  float src_h = pipe->src_roi.bottom - pipe->src_roi.top;
  float dst_h = pipe->dst_roi.bottom - pipe->dst_roi.top;

  // Adjust src_h with pipe decimation
  float decimation = powf(2.0f, pipe->vertical_decimation);
  src_h /= decimation;

  float bw = src_w * src_h * bpp * display_attributes.fps;

  // Consider panel dimension
  // (v_total / v_active) * (v_active / dst_h)
  bw *= FLOAT(display_attributes.v_total) / dst_h;

  // Bandwidth in GBps
  return (bw / 1000000000.0f);
}

float ResManager::GetClockForPipe(DisplayResourceContext *display_ctx, HWPipeInfo *pipe) {
  HWDisplayAttributes &display_attributes = display_ctx->display_attributes;
  float v_total = FLOAT(display_attributes.v_total);
  float fps = display_attributes.fps;

  float src_h = pipe->src_roi.bottom - pipe->src_roi.top;
  float dst_h = pipe->dst_roi.bottom - pipe->dst_roi.top;
  float dst_w = pipe->dst_roi.right - pipe->dst_roi.left;

  // Adjust src_h with pipe decimation
  float decimation = powf(2.0f, pipe->vertical_decimation);
  src_h /= decimation;


  // SDE Clock requirement in MHz
  float clk = (dst_w * v_total * fps) / 1000000.0f;

  // Consider down-scaling
  if (src_h > dst_h)
    clk *= (src_h / dst_h);

  return clk;
}

float ResManager::GetOverlapBw(HWLayers *hw_layers, float *pipe_bw, bool left_mixer) {
  uint32_t count = hw_layers->info.count;
  float overlap_bw[count][count];
  float overall_max = 0;

  memset(overlap_bw, 0, sizeof(overlap_bw));

  // Algorithm:
  // 1.Create an 'n' by 'n' sized 2D array, overlap_bw[n][n] (n = # of layers).
  // 2.Get overlap_bw between two layers, i and j, and account for other overlaps (prev_max) if any.
  //   This will fill the bottom-left half of the array including diagonal (0 <= i < n, 0 <= j <= i)
  //                      {1. pipe_bw[i],                         where i == j
  //   overlap_bw[i][j] = {2. 0,                                  where i != j && !Overlap(i, j)
  //                      {3. pipe_bw[i] + pipe_bw[j] + prev_max, where i != j && Overlap(i, j)
  //
  //   Overlap(i, j) = !(bottom_i <= top_j || top_i >= bottom_j)
  //   prev_max = max(prev_max, overlap_bw[j, k]), where 0 <= k < j and prev_max initially 0
  //   prev_max = prev_max ? (prev_max - pipe_bw[j]) : 0; (to account for "double counting")
  // 3.Get the max value in 2D array, overlap_bw[n][n], for the final overall_max bandwidth.
  //   overall_max = max(overlap_bw[i, j]), where 0 <= i < n, 0 <= j <= i

  for (uint32_t i = 0; i < count; i++) {
    HWPipeInfo &pipe1 = left_mixer ? hw_layers->config[i].left_pipe :
                        hw_layers->config[i].right_pipe;

    // Non existing pipe never overlaps
    if (pipe_bw[i] == 0)
      continue;

    float top1 = pipe1.dst_roi.top;
    float bottom1 = pipe1.dst_roi.bottom;
    float row_max = 0;

    for (uint32_t j = 0; j <= i; j++) {
      HWPipeInfo &pipe2 = left_mixer ? hw_layers->config[j].left_pipe :
                          hw_layers->config[j].right_pipe;

      if ((pipe_bw[j] == 0) || (i == j)) {
        overlap_bw[i][j] = pipe_bw[j];
        row_max = MAX(pipe_bw[j], row_max);
        continue;
      }

      float top2 = pipe2.dst_roi.top;
      float bottom2 = pipe2.dst_roi.bottom;

      if ((bottom1 <= top2) || (top1 >= bottom2)) {
        overlap_bw[i][j] = 0;
        continue;
      }

      overlap_bw[i][j] = pipe_bw[i] + pipe_bw[j];

      float prev_max = 0;
      for (uint32_t k = 0; k < j; k++) {
        if (overlap_bw[j][k])
          prev_max = MAX(overlap_bw[j][k], prev_max);
      }
      overlap_bw[i][j] += (prev_max > 0) ? (prev_max - pipe_bw[j]) : 0;
      row_max = MAX(overlap_bw[i][j], row_max);
    }

    overall_max = MAX(row_max, overall_max);
  }

  return overall_max;
}

float ResManager::GetBpp(LayerBufferFormat format) {
  switch (format) {
    case kFormatARGB8888:
    case kFormatRGBA8888:
    case kFormatBGRA8888:
    case kFormatXRGB8888:
    case kFormatRGBX8888:
    case kFormatBGRX8888:
      return 4.0f;
    case kFormatRGB888:
      return 3.0f;
    case kFormatRGB565:
    case kFormatYCbCr422Packed:
      return 2.0f;
    case kFormatYCbCr420Planar:
    case kFormatYCrCb420Planar:
    case kFormatYCbCr420SemiPlanar:
    case kFormatYCrCb420SemiPlanar:
    case kFormatYCbCr420SemiPlanarVenus:
      return 1.5f;
    default:
      DLOGE("GetBpp: Invalid buffer format: %x", format);
      return 0.0f;
  }
}

DisplayError ResManager::AllocRotatorBuffer(Handle display_ctx, HWLayers *hw_layers) {
  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);
  DisplayError error = kErrorNone;

  BufferManager *buffer_manager = display_resource_ctx->buffer_manager;
  HWLayersInfo &layer_info = hw_layers->info;

  // TODO(user): Do session management during prepare and allocate buffer and associate that with
  // the session during commit.
  buffer_manager->Start();

  for (uint32_t i = 0; i < layer_info.count; i++) {
    Layer& layer = layer_info.stack->layers[layer_info.index[i]];
    HWRotateInfo *rotate = &hw_layers->config[i].rotates[0];

    if (rotate->valid) {
      LayerBufferFormat rot_ouput_format;
      SetRotatorOutputFormat(layer.input_buffer->format, false, true, &rot_ouput_format);

      HWBufferInfo *hw_buffer_info = &rotate->hw_buffer_info;
      hw_buffer_info->buffer_config.width = UINT32(rotate->dst_roi.right - rotate->dst_roi.left);
      hw_buffer_info->buffer_config.height = UINT32(rotate->dst_roi.bottom - rotate->dst_roi.top);
      hw_buffer_info->buffer_config.format = rot_ouput_format;
      hw_buffer_info->buffer_config.buffer_count = 2;
      hw_buffer_info->buffer_config.secure = layer.input_buffer->flags.secure;

      error = buffer_manager->GetNextBuffer(hw_buffer_info);
      if (error != kErrorNone) {
        return error;
      }
    }

    rotate = &hw_layers->config[i].rotates[1];
    if (rotate->valid) {
      LayerBufferFormat rot_ouput_format;
      SetRotatorOutputFormat(layer.input_buffer->format, false, true, &rot_ouput_format);

      HWBufferInfo *hw_buffer_info = &rotate->hw_buffer_info;
      hw_buffer_info->buffer_config.width = UINT32(rotate->dst_roi.right - rotate->dst_roi.left);
      hw_buffer_info->buffer_config.height = UINT32(rotate->dst_roi.bottom - rotate->dst_roi.top);
      hw_buffer_info->buffer_config.format = rot_ouput_format;
      hw_buffer_info->buffer_config.buffer_count = 2;
      hw_buffer_info->buffer_config.secure = layer.input_buffer->flags.secure;

      error = buffer_manager->GetNextBuffer(hw_buffer_info);
      if (error != kErrorNone) {
        return error;
      }
    }
  }
  buffer_manager->Stop(hw_layers->closed_session_ids);

  return kErrorNone;
}

DisplayError ResManager::PostPrepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);
  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);
  DisplayError error = kErrorNone;

  BufferManager *buffer_manager = display_resource_ctx->buffer_manager;
  HWLayersInfo &layer_info = hw_layers->info;

  for (uint32_t i = 0; i < layer_info.count; i++) {
    Layer& layer = layer_info.stack->layers[layer_info.index[i]];
    HWRotateInfo *rotate = &hw_layers->config[i].rotates[0];

    if (rotate->valid) {
      HWBufferInfo *hw_buffer_info = &rotate->hw_buffer_info;

      error = buffer_manager->SetSessionId(hw_buffer_info->slot, hw_buffer_info->session_id);
      if (error != kErrorNone) {
        return error;
      }
    }

    rotate = &hw_layers->config[i].rotates[1];
    if (rotate->valid) {
      HWBufferInfo *hw_buffer_info = &rotate->hw_buffer_info;

      error = buffer_manager->SetSessionId(hw_buffer_info->slot, hw_buffer_info->session_id);
      if (error != kErrorNone) {
        return error;
      }
    }
  }

  return kErrorNone;
}

DisplayError ResManager::PostCommit(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);
  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;
  uint64_t frame_count = display_resource_ctx->frame_count;
  DisplayError error = kErrorNone;

  DLOGV_IF(kTagResources, "Resource for hw_block = %d, frame_count = %d", hw_block_id, frame_count);

  for (uint32_t i = 0; i < num_pipe_; i++) {
    if (src_pipes_[i].reserved_hw_block == hw_block_id) {
      src_pipes_[i].hw_block_id = hw_block_id;
      src_pipes_[i].state = kPipeStateAcquired;
      src_pipes_[i].state_frame_count = frame_count;
      DLOGV_IF(kTagResources, "Pipe acquired index = %d, type = %d, pipe_id = %x", i,
               src_pipes_[i].type, src_pipes_[i].mdss_pipe_id);
    } else if ((src_pipes_[i].hw_block_id == hw_block_id) &&
               (src_pipes_[i].state == kPipeStateAcquired)) {
      src_pipes_[i].state = kPipeStateToRelease;
      src_pipes_[i].state_frame_count = frame_count;
      DLOGV_IF(kTagResources, "Pipe to release index = %d, type = %d, pipe_id = %x", i,
               src_pipes_[i].type, src_pipes_[i].mdss_pipe_id);
    }
  }

  // handoff pipes which are used by splash screen
  if ((frame_count == 1) && (hw_block_id == kHWPrimary)) {
    for (uint32_t i = 0; i < num_pipe_; i++) {
      if ((src_pipes_[i].state == kPipeStateOwnedByKernel)) {
        src_pipes_[i].state = kPipeStateToRelease;
        src_pipes_[i].hw_block_id = kHWPrimary;
      }
    }
  }
  // set rotator pipes
  for (uint32_t i = 0; i < hw_res_info_.num_rotator; i++) {
    uint32_t pipe_index = rotators_[i].pipe_index;

    if (IS_BIT_SET(rotators_[i].client_bit_mask, hw_block_id)) {
      src_pipes_[pipe_index].hw_block_id = rotators_[i].writeback_id;
      src_pipes_[pipe_index].state = kPipeStateAcquired;
    } else if (!rotators_[i].client_bit_mask &&
               src_pipes_[pipe_index].hw_block_id == rotators_[i].writeback_id &&
               src_pipes_[pipe_index].state == kPipeStateAcquired) {
      src_pipes_[pipe_index].state = kPipeStateToRelease;
      src_pipes_[pipe_index].state_frame_count = frame_count;
    }
    // If no request on the rotation, release the pipe.
    if (!rotators_[i].request_bit_mask) {
      src_pipes_[pipe_index].dedicated_hw_block = kHWBlockMax;
    }
  }
  display_resource_ctx->frame_start = false;

  BufferManager *buffer_manager = display_resource_ctx->buffer_manager;
  HWLayersInfo &layer_info = hw_layers->info;

  for (uint32_t i = 0; i < layer_info.count; i++) {
    Layer& layer = layer_info.stack->layers[layer_info.index[i]];
    HWRotateInfo *rotate = &hw_layers->config[i].rotates[0];

    if (rotate->valid) {
      HWBufferInfo *rot_buf_info = &rotate->hw_buffer_info;

      error = buffer_manager->SetReleaseFd(rot_buf_info->slot,
                                                rot_buf_info->output_buffer.release_fence_fd);
      if (error != kErrorNone) {
        return error;
      }
    }

    rotate = &hw_layers->config[i].rotates[1];
    if (rotate->valid) {
      HWBufferInfo *rot_buf_info = &rotate->hw_buffer_info;

      error = buffer_manager->SetReleaseFd(rot_buf_info->slot,
                                                rot_buf_info->output_buffer.release_fence_fd);
      if (error != kErrorNone) {
        return error;
      }
    }
  }

  return kErrorNone;
}

void ResManager::Purge(Handle display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;

  for (uint32_t i = 0; i < num_pipe_; i++) {
    if (src_pipes_[i].hw_block_id == hw_block_id)
      src_pipes_[i].ResetState();
  }
  ClearRotator(display_resource_ctx);
}

uint32_t ResManager::GetMdssPipeId(PipeType type, uint32_t index) {
  uint32_t mdss_id = kPipeIdMax;
  switch (type) {
  case kPipeTypeVIG:
    if (index < 3) {
      mdss_id = kPipeIdVIG0 + index;
    } else if (index == 3) {
      mdss_id = kPipeIdVIG3;
    } else {
      DLOGE("vig pipe index is over the limit! %d", index);
    }
    break;
  case kPipeTypeRGB:
    if (index < 3) {
      mdss_id = kPipeIdRGB0 + index;
    } else if (index == 3) {
      mdss_id = kPipeIdRGB3;
    } else {
      DLOGE("rgb pipe index is over the limit! %d", index);
    }
    break;
  case kPipeTypeDMA:
    if (index < 2) {
      mdss_id = kPipeIdDMA0 + index;
    } else {
      DLOGE("dma pipe index is over the limit! %d", index);
    }
    break;
  default:
    DLOGE("wrong pipe type! %d", type);
    break;
  }

  return (1 << mdss_id);
}

uint32_t ResManager::SearchPipe(HWBlockType hw_block_id, SourcePipe *src_pipes,
                                uint32_t num_pipe, bool at_right) {
  uint32_t index = kPipeIdMax;
  SourcePipe *src_pipe;
  HWBlockType dedicated_block;

  // search dedicated idle pipes
  for (uint32_t i = 0; i < num_pipe; i++) {
    src_pipe = &src_pipes[i];
    if (src_pipe->reserved_hw_block == kHWBlockMax &&
        src_pipe->state == kPipeStateIdle &&
        src_pipe->dedicated_hw_block == hw_block_id) {
      index = src_pipe->index;
      break;
    }
  }

  // found
  if (index < num_pipe_) {
    return index;
  }

  // search the pipe being used
  for (uint32_t i = 0; i < num_pipe; i++) {
    src_pipe = &src_pipes[i];
    dedicated_block = src_pipe->dedicated_hw_block;
    if (src_pipe->reserved_hw_block == kHWBlockMax &&
        (src_pipe->state == kPipeStateAcquired) &&
        (src_pipe->hw_block_id == hw_block_id) &&
        (src_pipe->at_right == at_right) &&
        (dedicated_block == hw_block_id || dedicated_block == kHWBlockMax)) {
      index = src_pipe->index;
      break;
    }
  }

  // found
  if (index < num_pipe_) {
    return index;
  }

  // search the pipes idle or being used but not at the same side
  for (uint32_t i = 0; i < num_pipe; i++) {
    src_pipe = &src_pipes[i];
    dedicated_block = src_pipe->dedicated_hw_block;
    if (src_pipe->reserved_hw_block == kHWBlockMax &&
        ((src_pipe->state == kPipeStateIdle) ||
         (src_pipe->state == kPipeStateAcquired && src_pipe->hw_block_id == hw_block_id)) &&
         (dedicated_block == hw_block_id || dedicated_block == kHWBlockMax)) {
      index = src_pipe->index;
      break;
    }
  }
  return index;
}

uint32_t ResManager::NextPipe(PipeType type, HWBlockType hw_block_id, bool at_right) {
  uint32_t num_pipe = 0;
  SourcePipe *src_pipes = NULL;

  switch (type) {
  case kPipeTypeVIG:
    src_pipes = vig_pipes_;
    num_pipe = hw_res_info_.num_vig_pipe;
    break;
  case kPipeTypeRGB:
    src_pipes = rgb_pipes_;
    num_pipe = hw_res_info_.num_rgb_pipe;
    break;
  case kPipeTypeDMA:
  default:
    src_pipes = dma_pipes_;
    num_pipe = hw_res_info_.num_dma_pipe;
    break;
  }

  return SearchPipe(hw_block_id, src_pipes, num_pipe, at_right);
}

uint32_t ResManager::GetPipe(HWBlockType hw_block_id, bool is_yuv, bool need_scale, bool at_right,
                             bool use_non_dma_pipe) {
  uint32_t index = kPipeIdMax;

  // The default behavior is to assume RGB and VG pipes have scalars
  if (is_yuv) {
    return NextPipe(kPipeTypeVIG, hw_block_id, at_right);
  } else {
    if (!need_scale && !use_non_dma_pipe) {
      index = NextPipe(kPipeTypeDMA, hw_block_id, at_right);
    }

    if ((index >= num_pipe_) && (!need_scale || !hw_res_info_.has_non_scalar_rgb)) {
      index = NextPipe(kPipeTypeRGB, hw_block_id, at_right);
    }

    if (index >= num_pipe_) {
      index = NextPipe(kPipeTypeVIG, hw_block_id, at_right);
    }
  }

  return index;
}

bool ResManager::IsScalingNeeded(const HWPipeInfo *pipe_info) {
  const LayerRect &src_roi = pipe_info->src_roi;
  const LayerRect &dst_roi = pipe_info->dst_roi;

  return ((dst_roi.right - dst_roi.left) != (src_roi.right - src_roi.left)) ||
          ((dst_roi.bottom - dst_roi.top) != (src_roi.bottom - src_roi.top));
}

void ResManager::AppendDump(char *buffer, uint32_t length) {
  SCOPE_LOCK(locker_);
  AppendString(buffer, length, "\nresource manager pipe state");
  uint32_t i;
  for (i = 0; i < num_pipe_; i++) {
    SourcePipe *src_pipe = &src_pipes_[i];
    AppendString(buffer, length,
                 "\nindex = %d, id = %x, reserved = %d, state = %d, hw_block = %d, dedicated = %d",
                 src_pipe->index, src_pipe->mdss_pipe_id, src_pipe->reserved_hw_block,
                 src_pipe->state, src_pipe->hw_block_id, src_pipe->dedicated_hw_block);
  }

  for (i = 0; i < hw_res_info_.num_rotator; i++) {
    if (rotators_[i].client_bit_mask || rotators_[i].request_bit_mask) {
      AppendString(buffer, length,
                   "\nrotator = %d, pipe index = %x, client_bit_mask = %x, request_bit_mask = %x",
                   i, rotators_[i].pipe_index, rotators_[i].client_bit_mask,
                   rotators_[i].request_bit_mask);
    }
  }
}

DisplayError ResManager::AcquireRotator(DisplayResourceContext *display_resource_ctx,
                                        const uint32_t rotate_count) {
  if (rotate_count == 0)
    return kErrorNone;
  if (hw_res_info_.num_rotator == 0) {
    DLOGV_IF(kTagResources, "Rotator hardware is not available");
    return kErrorResources;
  }

  uint32_t i, j, pipe_index, num_rotator;
  if (rotate_count > hw_res_info_.num_rotator)
    num_rotator = hw_res_info_.num_rotator;
  else
    num_rotator = rotate_count;

  for (i = 0; i < num_rotator; i++) {
    uint32_t rotate_pipe_index = rotators_[i].pipe_index;
    SourcePipe *src_pipe = &src_pipes_[rotate_pipe_index];

    if (src_pipe->dedicated_hw_block != kHWBlockMax)
      DLOGV_IF(kTagResources, "Overwrite dedicated block %d", src_pipe->dedicated_hw_block);
    src_pipe->dedicated_hw_block = rotators_[i].writeback_id;
    SET_BIT(rotators_[i].request_bit_mask, display_resource_ctx->hw_block_id);
  }

  for (i = 0; i < num_rotator; i++) {
    uint32_t rotate_pipe_index = rotators_[i].pipe_index;
    if (src_pipes_[rotate_pipe_index].reserved_hw_block != kHWBlockMax) {
      DLOGV_IF(kTagResources, "pipe %x is reserved by block:%d",
               src_pipes_[rotate_pipe_index].mdss_pipe_id,
               src_pipes_[rotate_pipe_index].reserved_hw_block);
      return kErrorResources;
    }
    pipe_index = SearchPipe(rotators_[i].writeback_id, &src_pipes_[rotate_pipe_index], 1, false);
    if (pipe_index >= num_pipe_) {
      DLOGV_IF(kTagResources, "pipe %x is not ready for rotator",
               src_pipes_[rotate_pipe_index].mdss_pipe_id);
      return kErrorResources;
    }
  }

  for (i = 0; i < num_rotator; i++)
    SET_BIT(rotators_[i].client_bit_mask, display_resource_ctx->hw_block_id);

  return kErrorNone;
}

void ResManager::AssignRotator(HWRotateInfo *rotate, uint32_t *rotate_count) {
  if (!rotate->valid)
    return;
  // Interleave rotator assignment
  if ((*rotate_count & 0x1) && (hw_res_info_.num_rotator > 1)) {
    rotate->pipe_id = src_pipes_[rotators_[1].pipe_index].mdss_pipe_id;
    rotate->writeback_id = rotators_[1].writeback_id;
  } else {
    rotate->pipe_id = src_pipes_[rotators_[0].pipe_index].mdss_pipe_id;
    rotate->writeback_id = rotators_[0].writeback_id;
  }
  (*rotate_count)++;
}

void ResManager::ClearRotator(DisplayResourceContext *display_resource_ctx) {
  for (uint32_t i = 0; i < hw_res_info_.num_rotator; i++) {
    uint32_t pipe_index;
    pipe_index = rotators_[i].pipe_index;
    rotators_[i].ClearState(display_resource_ctx->hw_block_id);
    if (rotators_[i].client_bit_mask)
      continue;

    if (src_pipes_[pipe_index].hw_block_id == rotators_[i].writeback_id) {
      src_pipes_[pipe_index].ResetState();
    } else if (src_pipes_[pipe_index].dedicated_hw_block == rotators_[i].writeback_id) {
      src_pipes_[pipe_index].dedicated_hw_block = kHWBlockMax;
    }
  }
}

void ResManager::SetRotatorOutputFormat(const LayerBufferFormat &input_format, bool bwc, bool rot90,
                                        LayerBufferFormat *output_format) {
  switch (input_format) {
  case kFormatRGB565:
    if (rot90)
      *output_format = kFormatRGB888;
    else
      *output_format = input_format;
    break;
  case kFormatRGBA8888:
    if (bwc)
      *output_format = kFormatBGRA8888;
    else
      *output_format = input_format;
    break;
  case kFormatYCbCr420SemiPlanarVenus:
  case kFormatYCbCr420SemiPlanar:
    if (rot90)
      *output_format = kFormatYCrCb420SemiPlanar;
    else
      *output_format = input_format;
    break;
  case kFormatYCbCr420Planar:
  case kFormatYCrCb420Planar:
    *output_format = kFormatYCrCb420SemiPlanar;
    break;
  default:
    *output_format = input_format;
    break;
  }

  DLOGV_IF(kTagResources, "Input format %x, Output format = %x, rot90 %d", input_format,
           *output_format, rot90);

  return;
}


}  // namespace sde

