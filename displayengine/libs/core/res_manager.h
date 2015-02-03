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

#ifndef __RES_MANAGER_H__
#define __RES_MANAGER_H__

#include <core/display_interface.h>
#include <utils/locker.h>

#include "hw_interface.h"
#include "dump_impl.h"
#include "buffer_manager.h"

namespace sde {

class ResManager : public DumpImpl {
 public:
  ResManager();
  DisplayError Init(const HWResourceInfo &hw_res_info, BufferAllocator *buffer_allocator,
                    BufferSyncHandler *buffer_sync_handler);
  DisplayError Deinit();
  DisplayError RegisterDisplay(DisplayType type, const HWDisplayAttributes &attributes,
                               Handle *display_ctx);
  DisplayError UnregisterDisplay(Handle display_ctx);
  DisplayError Start(Handle display_ctx);
  DisplayError Stop(Handle display_ctx);
  DisplayError Acquire(Handle display_ctx, HWLayers *hw_layers);
  DisplayError PostPrepare(Handle display_ctx, HWLayers *hw_layers);
  DisplayError PostCommit(Handle display_ctx, HWLayers *hw_layers);
  void Purge(Handle display_ctx);

  // DumpImpl method
  virtual void AppendDump(char *buffer, uint32_t length);

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

  // todo: retrieve all these from kernel
  enum {
    kMaxSourcePipeWidth = 2048,
    kMaxInterfaceWidth = 2048,
    kMaxRotateDownScaleRatio = 8,
    kMaxNumRotator = 2,
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
    HWBlockType reserved_hw_block;
    HWBlockType dedicated_hw_block;

    SourcePipe() : type(kPipeTypeUnused), mdss_pipe_id(kPipeIdMax), index(0),
                   state(kPipeStateIdle), hw_block_id(kHWBlockMax), at_right(false),
                   state_frame_count(0), priority(0), reserved_hw_block(kHWBlockMax),
                   dedicated_hw_block(kHWBlockMax) { }

    inline void ResetState() { state = kPipeStateIdle; hw_block_id = kHWBlockMax;
        at_right = false; reserved_hw_block = kHWBlockMax; dedicated_hw_block = kHWBlockMax; }
  };

  struct DisplayResourceContext {
    HWDisplayAttributes display_attributes;
    BufferManager *buffer_manager;
    DisplayType display_type;
    HWBlockType hw_block_id;
    uint64_t frame_count;
    int32_t session_id;  // applicable for virtual display sessions only
    uint32_t rotate_count;
    bool frame_start;

    DisplayResourceContext() : hw_block_id(kHWBlockMax), frame_count(0), session_id(-1),
                    rotate_count(0), frame_start(false) { }

    ~DisplayResourceContext() {
      if (buffer_manager) {
        delete buffer_manager;
        buffer_manager = NULL;
      }
    }
  };

  struct HWBlockContext {
    bool is_in_use;
    HWBlockContext() : is_in_use(false) { }
  };

  struct HWRotator {
    uint32_t pipe_index;
    HWBlockType writeback_id;
    uint32_t client_bit_mask;
    uint32_t request_bit_mask;
    HWRotator() : pipe_index(0), writeback_id(kHWBlockMax), client_bit_mask(0),
                     request_bit_mask(0) { }

    inline void ClearState(HWBlockType block) { CLEAR_BIT(client_bit_mask, block);
        CLEAR_BIT(request_bit_mask, block); }
  };

  static const int kPipeIdNeedsAssignment = -1;

  uint32_t GetMdssPipeId(PipeType pipe_type, uint32_t index);
  uint32_t NextPipe(PipeType pipe_type, HWBlockType hw_block_id, bool at_right);
  uint32_t SearchPipe(HWBlockType hw_block_id, SourcePipe *src_pipes, uint32_t num_pipe,
                      bool at_right);
  uint32_t GetPipe(HWBlockType hw_block_id, bool is_yuv, bool need_scale, bool at_right,
                   bool use_non_dma_pipe);
  bool IsScalingNeeded(const HWPipeInfo *pipe_info);
  DisplayError Config(DisplayResourceContext *display_resource_ctx, HWLayers *hw_layers,
                      uint32_t *rotate_count);
  DisplayError DisplaySplitConfig(DisplayResourceContext *display_resource_ctx,
                                  const LayerTransform &transform, const LayerRect &src_rect,
                                  const LayerRect &dst_rect, HWLayerConfig *layer_config);
  DisplayError ValidateScaling(const Layer &layer, const LayerRect &crop,
                               const LayerRect &dst, float *rot_scale_x, float *rot_scale_y);
  DisplayError SrcSplitConfig(DisplayResourceContext *display_resource_ctx,
                              const LayerTransform &transform, const LayerRect &src_rect,
                              const LayerRect &dst_rect, HWLayerConfig *layer_config);
  void CalculateCut(const LayerTransform &transform, float *left_cut_ratio, float *top_cut_ratio,
                    float *right_cut_ratio, float *bottom_cut_ratio);
  void CalculateCropRects(const LayerRect &scissor, const LayerTransform &transform,
                          LayerRect *crop, LayerRect *dst);
  bool IsValidDimension(const LayerRect &src, const LayerRect &dst);
  bool CheckBandwidth(DisplayResourceContext *display_ctx, HWLayers *hw_layers);
  float GetPipeBw(DisplayResourceContext *display_ctx, HWPipeInfo *pipe, float bpp);
  float GetClockForPipe(DisplayResourceContext *display_ctx, HWPipeInfo *pipe);
  float GetOverlapBw(HWLayers *hw_layers, float *pipe_bw, bool left_mixer);
  DisplayError SetDecimationFactor(HWPipeInfo *pipe);
  float GetBpp(LayerBufferFormat format);
  void SplitRect(bool flip_horizontal, const LayerRect &src_rect, const LayerRect &dst_rect,
                 LayerRect *src_left, LayerRect *dst_left, LayerRect *src_right,
                 LayerRect *dst_right);
  bool IsMacroTileFormat(const LayerBuffer *buffer) { return buffer->flags.macro_tile; }
  bool IsYuvFormat(LayerBufferFormat format) { return (format >= kFormatYCbCr420Planar); }
  bool IsRotationNeeded(float rotation)
         { return (UINT32(rotation) == 90 || UINT32(rotation) == 270); }
  void LogRectVerbose(const char *prefix, const LayerRect &roi);
  void RotationConfig(const LayerTransform &transform, const float &scale_x,
                      const float &scale_y, LayerRect *src_rect,
                      struct HWLayerConfig *layer_config, uint32_t *rotate_count);
  DisplayError AcquireRotator(DisplayResourceContext *display_resource_ctx,
                              const uint32_t roate_cnt);
  void AssignRotator(HWRotateInfo *rotate, uint32_t *rotate_cnt);
  void ClearRotator(DisplayResourceContext *display_resource_ctx);
  void NormalizeRect(const uint32_t &factor, LayerRect *rect);
  DisplayError AllocRotatorBuffer(Handle display_ctx, HWLayers *hw_layers);
  void SetRotatorOutputFormat(const LayerBufferFormat &input_format, bool bwc, bool rot90,
                              LayerBufferFormat *output_format);

  template <class T>
  inline void Swap(T &a, T &b) {
    T c(a);
    a = b;
    b = c;
  }

  // factor value should be in powers of 2(eg: 1, 2, 4, 8)
  template <class T1, class T2>
  inline T1 FloorToMultipleOf(const T1 &value, const T2 &factor) {
    return (T1)(value & (~(factor - 1)));
  }

  template <class T1, class T2>
  inline T1 CeilToMultipleOf(const T1 &value, const T2 &factor) {
    return (T1)((value + (factor - 1)) & (~(factor - 1)));
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
  float bw_claimed_;  // Bandwidth claimed by other display
  float clk_claimed_;  // Clock claimed by other display
  float last_primary_bw_;
  uint32_t virtual_count_;
  struct HWRotator rotators_[kMaxNumRotator];
  BufferAllocator *buffer_allocator_;
  BufferSyncHandler *buffer_sync_handler_;  // Pointer to buffer sync handler that was defined by
                                            // the display engine's client
};

}  // namespace sde

#endif  // __RES_MANAGER_H__

