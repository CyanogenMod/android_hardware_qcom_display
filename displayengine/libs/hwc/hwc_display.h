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

#ifndef __HWC_DISPLAY_H__
#define __HWC_DISPLAY_H__

#include <hardware/hwcomposer.h>
#include <core/core_interface.h>

namespace sde {

class HWCDisplay : public DisplayEventHandler {
 public:
  virtual int Init();
  virtual int Deinit();
  virtual int Prepare(hwc_display_contents_1_t *content_list) = 0;
  virtual int Commit(hwc_display_contents_1_t *content_list) = 0;
  virtual int EventControl(int event, int enable);
  virtual int SetPowerMode(int mode);
  virtual int GetDisplayConfigs(uint32_t *configs, size_t *num_configs);
  virtual int GetDisplayAttributes(uint32_t config, const uint32_t *attributes, int32_t *values);
  virtual int GetActiveConfig();
  virtual int SetActiveConfig(int index);
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);
  virtual int SetActiveConfig(hwc_display_contents_1_t *content_list);
  virtual void SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type);

 protected:
  // Maximum number of layers supported by display engine.
  static const uint32_t kMaxLayerCount = 32;

  // Structure to track memory allocation for layer stack (layers, rectangles) object.
  struct LayerStackMemory {
    static const size_t kSizeSteps = 1024;  // Default memory allocation.
    uint8_t *raw;  // Pointer to byte array.
    size_t size;  // Current number of allocated bytes.

    LayerStackMemory() : raw(NULL), size(0) { }
  };

  struct LayerCache {
    buffer_handle_t handle;
    LayerComposition composition;

    LayerCache() : handle(NULL), composition(kCompositionGPU) { }
  };

  struct LayerStackCache {
    LayerCache layer_cache[kMaxLayerCount];
    uint32_t layer_count;

    LayerStackCache() : layer_count(0) { }
  };

  HWCDisplay(CoreInterface *core_intf, hwc_procs_t const **hwc_procs, DisplayType type, int id);
  virtual ~HWCDisplay() { }

  // DisplayEventHandler methods
  virtual DisplayError VSync(const DisplayEventVSync &vsync);
  virtual DisplayError Refresh();

  virtual int AllocateLayerStack(hwc_display_contents_1_t *content_list);
  virtual int PrepareLayerStack(hwc_display_contents_1_t *content_list);
  virtual int CommitLayerStack(hwc_display_contents_1_t *content_list);
  virtual int PostCommitLayerStack(hwc_display_contents_1_t *content_list);
  bool NeedsFrameBufferRefresh(hwc_display_contents_1_t *content_list);
  void CacheLayerStackInfo(hwc_display_contents_1_t *content_list);
  inline void SetRect(const hwc_rect_t &source, LayerRect *target);
  inline void SetRect(const hwc_frect_t &source, LayerRect *target);
  inline void SetComposition(const int32_t &source, LayerComposition *target);
  inline void SetComposition(const int32_t &source, int32_t *target);
  inline void SetBlending(const int32_t &source, LayerBlending *target);
  int SetFormat(const int32_t &source, const int flags, LayerBufferFormat *target);
  LayerBufferFormat GetSDEFormat(const int32_t &source, const int flags);
  void DumpInputBuffers(hwc_display_contents_1_t *content_list);
  const char *GetHALPixelFormatString(int format);
  const char *GetDisplayString();

  enum {
    INPUT_LAYER_DUMP,
    OUTPUT_LAYER_DUMP,
  };

  CoreInterface *core_intf_;
  hwc_procs_t const **hwc_procs_;
  DisplayType type_;
  int id_;
  DisplayInterface *display_intf_;
  LayerStackMemory layer_stack_memory_;
  LayerStack layer_stack_;
  LayerStackCache layer_stack_cache_;
  bool flush_;
  LayerBuffer *output_buffer_;
  uint32_t dump_frame_count_;
  uint32_t dump_frame_index_;
  bool dump_input_layers_;
};

}  // namespace sde

#endif  // __HWC_DISPLAY_H__

