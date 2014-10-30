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

#ifndef __RES_MANAGER_H__
#define __RES_MANAGER_H__

#include <core/device_interface.h>
#include <utils/locker.h>

#include "hw_interface.h"

namespace sde {

class ResManager {
 public:
  ResManager();
  DisplayError Init(const HWResourceInfo &hw_res_info);
  DisplayError Deinit();
  DisplayError RegisterDevice(DeviceType type, const HWDeviceAttributes &attributes,
                              Handle *device);
  DisplayError UnregisterDevice(Handle device);
  DisplayError Start(Handle device);
  DisplayError Stop(Handle device);
  DisplayError Acquire(Handle device, HWLayers *hw_layers);
  void PostCommit(Handle device, HWLayers *hw_layers);
  void Purge(Handle device);

 private:
  enum PipeId {
    kPipeIdVIG0,
    kPipeIdVIG1,
    kPipeIdVIG2,
    kPipeIdRGB0,
    kPipeIdRGB1,
    kPipeIdRGB2,
    kPipeIdDMA0,
    kPipeIdDMA1,
    kPipeIdVIG3,
    kPipeIdRGB3,
    kPipeIdMax,
  };

  enum PipeType {
    kPipeTypeUnused,
    kPipeTypeVIG,
    kPipeTypeRGB,
    kPipeTypeDMA,
    kPipeTypeMax,
  };

  enum PipeState {
    kPipeStateIdle,           // Pipe state when it is available for reservation
    kPipeStateAcquired,       // Pipe state after successful commit
    kPipeStateToRelease,      // Pipe state that can be moved to Idle when releasefence is signaled
    kPipeStateOwnedByKernel,  // Pipe state when pipe is owned by kernel
  };

  enum {
    kMaxSourcePipeWidth = 2048,
    kMaxInterfaceWidth = 2048,
    kMaxCropWidth = 5,
    kMaxCropHeight = 5,
  };

  struct SourcePipe {
    PipeType type;
    uint32_t mdss_pipe_id;
    uint32_t index;
    PipeState state;
    HWBlockType hw_block_id;
    bool at_right;
    uint64_t state_frame_count;
    int priority;
    bool reserved;

    SourcePipe() : type(kPipeTypeUnused), mdss_pipe_id(kPipeIdMax), index(0), state(kPipeStateIdle),
                   hw_block_id(kHWBlockMax), at_right(false), state_frame_count(0), priority(0),
                   reserved(false) { }
  };

  struct ResManagerDevice {
    HWDeviceAttributes device_attributes;
    DeviceType device_type;
    HWBlockType hw_block_id;
    uint64_t frame_count;
    int32_t session_id;  // applicable for virtual display sessions only

    ResManagerDevice() : hw_block_id(kHWBlockMax), frame_count(0), session_id(-1) { }
  };

  struct HWBlockContext {
    bool is_in_use;
    HWBlockContext() : is_in_use(false) { }
  };

  uint32_t GetMdssPipeId(PipeType pipe_type, uint32_t index);
  uint32_t NextPipe(PipeType pipe_type, HWBlockType hw_block_id, bool at_right);
  uint32_t GetPipe(HWBlockType hw_block_id, bool is_yuv, bool need_scale, bool at_right,
                   bool use_non_dma_pipe);
  bool IsScalingNeeded(const HWPipeInfo *pipe_info);
  DisplayError Config(ResManagerDevice *res_mgr_device, HWLayers *hw_layers);
  bool IsValidDimension(const Layer &layer, float *width_scale, float *height_scale);
  void CalculateCut(float *left_cut_ratio, float *top_cut_ratio, float *right_cut_ratio,
                    float *bottom_cut_ratio, const LayerTransform &transform);
  void CalculateCropRects(LayerRect *crop, LayerRect *dst,
                          const LayerRect &scissor, const LayerTransform &transform);
  bool IsNonIntegralSrcCrop(const LayerRect &crop);
  void IntegerizeRect(LayerRect *dst_rect, const LayerRect &src_rect);

  template <class T>
  inline void Swap(T &a, T &b) {
    T c(a);
    a = b;
    b = c;
  }

  Locker locker_;
  HWResourceInfo hw_res_info_;
  HWBlockContext hw_block_ctx_[kHWBlockMax];
  SourcePipe src_pipes_[kPipeIdMax];
  uint32_t num_pipe_;
  SourcePipe *vig_pipes_;
  SourcePipe *rgb_pipes_;
  SourcePipe *dma_pipes_;
  bool frame_start_;
};

}  // namespace sde

#endif  // __RES_MANAGER_H__

