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
#define SDE_MODULE_NAME "ResManager"
#include <utils/debug.h>

#include <utils/constants.h>

#include "res_manager.h"

namespace sde {

ResManager::ResManager()
  : num_pipe_(0), vig_pipes_(NULL), rgb_pipes_(NULL), dma_pipes_(NULL), frame_start_(false) {
}

DisplayError ResManager::Init(const HWResourceInfo &hw_res_info) {
  DLOGV("Init");

  hw_res_info_ = hw_res_info;

  DisplayError error = kErrorNone;

  num_pipe_ = hw_res_info_.num_vig_pipe + hw_res_info_.num_rgb_pipe + hw_res_info_.num_dma_pipe;

  if (UNLIKELY(num_pipe_ > kPipeIdMax)) {
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
    if (UNLIKELY(!hw_block_ctx_[kHWPrimary].is_in_use)) {
      hw_block_id = kHWPrimary;
    }
    break;

  case kHDMI:
    if (UNLIKELY(!hw_block_ctx_[kHWHDMI].is_in_use)) {
      hw_block_id = kHWHDMI;
    }
    break;

  case kVirtual:
    // assume only WB2 can be used for vitrual display
    if (UNLIKELY(!hw_block_ctx_[kHWWriteback2].is_in_use)) {
      hw_block_id = kHWWriteback2;
    }
    break;

  default:
    DLOGW("RegisterDisplay, invalid type %d", type);
    return kErrorParameters;
  }

  if (UNLIKELY(hw_block_id == kHWBlockMax)) {
    return kErrorResources;
  }

  DisplayResourceContext *display_resource_ctx = new DisplayResourceContext();
  if (UNLIKELY(!display_resource_ctx)) {
    return kErrorMemory;
  }

  hw_block_ctx_[hw_block_id].is_in_use = true;

  display_resource_ctx->display_attributes = attributes;
  display_resource_ctx->display_type = type;
  display_resource_ctx->hw_block_id = hw_block_id;

  *display_ctx = display_resource_ctx;

  return kErrorNone;
}

DisplayError ResManager::UnregisterDisplay(Handle display_ctx) {
  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);

  Purge(display_ctx);
  hw_block_ctx_[display_resource_ctx->hw_block_id].is_in_use = false;
  delete display_resource_ctx;

  return kErrorNone;
}


DisplayError ResManager::Start(Handle display_ctx) {
  locker_.Lock();

  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);

  if (frame_start_) {
    return kErrorNone;  // keep context locked.
  }

  // First call in the cycle
  frame_start_ = true;
  display_resource_ctx->frame_count++;

  // Release the pipes not used in the previous cycle
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;
  for (uint32_t i = 0; i < num_pipe_; i++) {
    if ((src_pipes_[i].hw_block_id == hw_block_id) &&
        (src_pipes_[i].state == kPipeStateToRelease)) {
      src_pipes_[i].state = kPipeStateIdle;
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

  if (UNLIKELY(layer_info.count > num_pipe_)) {
    return kErrorResources;
  }

  error = Config(display_resource_ctx, hw_layers);
  if (UNLIKELY(error != kErrorNone)) {
    return error;
  }

  uint32_t left_index = 0;
  bool need_scale = false;
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;

  // Clear reserved marking
  for (uint32_t i = 0; i < num_pipe_; i++) {
    src_pipes_[i].reserved = false;
  }

  for (uint32_t i = 0; i < layer_info.count; i++) {
    Layer &layer = layer_info.stack->layers[layer_info.index[i]];
    bool use_non_dma_pipe = hw_layers->config[i].use_non_dma_pipe;

    // Temp setting, this should be set by comp_manager
    if (hw_block_id == kHWPrimary) {
      use_non_dma_pipe = true;
    }

    HWPipeInfo *pipe_info = &hw_layers->config[i].left_pipe;

    need_scale = IsScalingNeeded(pipe_info);

    // Should have a generic macro
    bool is_yuv = (layer.input_buffer->format >= kFormatYCbCr420Planar);

    left_index = GetPipe(hw_block_id, is_yuv, need_scale, false, use_non_dma_pipe);
    if (left_index >= num_pipe_) {
      goto Acquire_failed;
    }

    src_pipes_[left_index].reserved = true;

    pipe_info =  &hw_layers->config[i].right_pipe;
    if (pipe_info->pipe_id == 0) {
      // assign single pipe
      hw_layers->config[i].left_pipe.pipe_id = src_pipes_[left_index].mdss_pipe_id;
      src_pipes_[left_index].at_right = false;
      continue;
    }

    need_scale = IsScalingNeeded(pipe_info);

    uint32_t right_index;
    right_index = GetPipe(hw_block_id, is_yuv, need_scale, true, use_non_dma_pipe);
    if (right_index >= num_pipe_) {
      goto Acquire_failed;
    }

    if (src_pipes_[right_index].priority < src_pipes_[left_index].priority) {
      // Swap pipe based on priority
      Swap(left_index, right_index);
    }

    // assign dual pipes
    hw_layers->config[i].right_pipe.pipe_id = src_pipes_[right_index].mdss_pipe_id;
    src_pipes_[right_index].reserved = true;
    src_pipes_[right_index].at_right = true;
    src_pipes_[left_index].reserved = true;
    src_pipes_[left_index].at_right = false;
    hw_layers->config[i].left_pipe.pipe_id = src_pipes_[left_index].mdss_pipe_id;
  }

  return kErrorNone;

Acquire_failed:
  for (uint32_t i = 0; i < num_pipe_; i++)
    src_pipes_[i].reserved = false;
  return kErrorResources;
}

void ResManager::PostCommit(Handle display_ctx, HWLayers *hw_layers) {
  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;
  uint64_t frame_count = display_resource_ctx->frame_count;

  DLOGV("Resource for hw_block=%d frame_count=%d", hw_block_id, frame_count);

  for (uint32_t i = 0; i < num_pipe_; i++) {
    if (src_pipes_[i].reserved) {
      src_pipes_[i].hw_block_id = hw_block_id;
      src_pipes_[i].state = kPipeStateAcquired;
      src_pipes_[i].state_frame_count = frame_count;
      DLOGV("Pipe acquired index=%d type=%d pipe_id=%x", i, src_pipes_[i].type,
            src_pipes_[i].mdss_pipe_id);
    } else if ((src_pipes_[i].hw_block_id == hw_block_id) &&
               (src_pipes_[i].state == kPipeStateAcquired)) {
      src_pipes_[i].state = kPipeStateToRelease;
      src_pipes_[i].state_frame_count = frame_count;
      DLOGV("Pipe to release index=%d type=%d pipe_id=%x", i, src_pipes_[i].type,
            src_pipes_[i].mdss_pipe_id);
    }
  }

  // handoff pipes which are used by splash screen
  if (UNLIKELY((frame_count == 1) && (hw_block_id == kHWPrimary))) {
    for (uint32_t i = 0; i < num_pipe_; i++) {
      if ((src_pipes_[i].state == kPipeStateOwnedByKernel)) {
        src_pipes_[i].state = kPipeStateToRelease;
        src_pipes_[i].hw_block_id = kHWPrimary;
      }
    }
  }

  frame_start_ = false;
}

void ResManager::Purge(Handle display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;

  for (uint32_t i = 0; i < num_pipe_; i++) {
    if (src_pipes_[i].hw_block_id == hw_block_id)
      src_pipes_[i].state = kPipeStateIdle;
  }
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

uint32_t ResManager::NextPipe(PipeType type, HWBlockType hw_block_id, bool at_right) {
  uint32_t num_pipe = 0;
  uint32_t index = kPipeIdMax;
  SourcePipe *src_pipe = NULL;

  switch (type) {
  case kPipeTypeVIG:
    src_pipe = vig_pipes_;
    num_pipe = hw_res_info_.num_vig_pipe;
    break;
  case kPipeTypeRGB:
    src_pipe = rgb_pipes_;
    num_pipe = hw_res_info_.num_rgb_pipe;
    break;
  case kPipeTypeDMA:
  default:
    src_pipe = dma_pipes_;
    num_pipe = hw_res_info_.num_dma_pipe;
    break;
  }

  // search the pipe being used
  for (uint32_t i = 0; i < num_pipe; i++) {
    if (!src_pipe[i].reserved &&
        (src_pipe[i].state == kPipeStateAcquired) &&
        (src_pipe[i].hw_block_id == hw_block_id) &&
        (src_pipe[i].at_right == at_right)) {
      index = src_pipe[i].index;
      break;
    }
  }

  // found
  if (index < num_pipe_) {
    return index;
  }

  for (uint32_t i = 0; i < num_pipe; i++) {
    if (!src_pipe[i].reserved &&
        ((src_pipe[i].state == kPipeStateIdle) ||
         ((src_pipe[i].state == kPipeStateAcquired) &&
         (src_pipe[i].hw_block_id == hw_block_id)))) {
      index = src_pipe[i].index;
      break;
    }
  }

  return index;
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

    if ((index >= num_pipe_) && (!need_scale || hw_res_info_.has_non_scalar_rgb)) {
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
}

}  // namespace sde

