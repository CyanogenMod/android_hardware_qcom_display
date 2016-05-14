/*
* Copyright (c) 2014 - 2016, The Linux Foundation. All rights reserved.
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
#include <qdMetaData.h>
#include <private/color_params.h>
#include <map>

namespace sdm {

class BlitEngine;

class HWCDisplay : public DisplayEventHandler {
 public:
  virtual ~HWCDisplay() { }
  virtual int Init();
  virtual int Deinit();
  virtual int Prepare(hwc_display_contents_1_t *content_list) = 0;
  virtual int Commit(hwc_display_contents_1_t *content_list) = 0;
  virtual int EventControl(int event, int enable);
  virtual int SetPowerMode(int mode);

  // Framebuffer configurations
  virtual int GetDisplayConfigs(uint32_t *configs, size_t *num_configs);
  virtual int GetDisplayAttributes(uint32_t config, const uint32_t *attributes, int32_t *values);
  virtual int GetActiveConfig();
  virtual int SetActiveConfig(int index);

  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);
  virtual void SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type);
  virtual DisplayError SetMaxMixerStages(uint32_t max_mixer_stages);
  virtual DisplayError ControlPartialUpdate(bool enable, uint32_t *pending);
  virtual uint32_t GetLastPowerMode();
  virtual int SetFrameBufferResolution(uint32_t x_pixels, uint32_t y_pixels);
  virtual void GetFrameBufferResolution(uint32_t *x_pixels, uint32_t *y_pixels);
  virtual void GetPanelResolution(uint32_t *x_pixels, uint32_t *y_pixels);
  virtual int SetDisplayStatus(uint32_t display_status);
  virtual int OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);
  virtual int Perform(uint32_t operation, ...);
  virtual int SetCursorPosition(int x, int y);
  virtual void SetSecureDisplay(bool secure_display_active);

  // Display Configurations
  virtual int SetActiveDisplayConfig(int config);
  virtual int GetActiveDisplayConfig(uint32_t *config);
  virtual int GetDisplayConfigCount(uint32_t *count);
  virtual int GetDisplayAttributesForConfig(int config, DisplayConfigVariableInfo *attributes);

  int SetPanelBrightness(int level);
  int GetPanelBrightness(int *level);
  int ToggleScreenUpdates(bool enable);
  int ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload,
                           PPDisplayAPIPayload *out_payload,
                           PPPendingParams *pending_action);
  int GetVisibleDisplayRect(hwc_rect_t* rect);

 protected:
  enum DisplayStatus {
    kDisplayStatusOffline = 0,
    kDisplayStatusOnline,
    kDisplayStatusPause,
    kDisplayStatusResume,
  };

  // Dim layer flag set by SurfaceFlinger service.
  static const uint32_t kDimLayer = 0x80000000;

  // Maximum number of layers supported by display manager.
  static const uint32_t kMaxLayerCount = 32;

  // Structure to track memory allocation for layer stack (layers, rectangles) object.
  struct LayerStackMemory {
    static const size_t kSizeSteps = 4096;  // Default memory allocation.
    uint8_t *raw;  // Pointer to byte array.
    size_t size;  // Current number of allocated bytes.

    LayerStackMemory() : raw(NULL), size(0) { }
  };

  struct LayerCache {
    buffer_handle_t handle;
    uint8_t plane_alpha;
    LayerComposition composition;

    LayerCache() : handle(NULL), plane_alpha(0xff), composition(kCompositionGPU) { }
  };

  struct LayerStackCache {
    LayerCache layer_cache[kMaxLayerCount];
    uint32_t layer_count;
    bool animating;
    bool in_use;

    LayerStackCache() : layer_count(0), animating(false), in_use(false) { }
  };

  HWCDisplay(CoreInterface *core_intf, hwc_procs_t const **hwc_procs, DisplayType type, int id,
             bool needs_blit);

  // DisplayEventHandler methods
  virtual DisplayError VSync(const DisplayEventVSync &vsync);
  virtual DisplayError Refresh();

  virtual int AllocateLayerStack(hwc_display_contents_1_t *content_list);
  virtual int PrePrepareLayerStack(hwc_display_contents_1_t *content_list);
  virtual int PrepareLayerStack(hwc_display_contents_1_t *content_list);
  virtual int CommitLayerStack(hwc_display_contents_1_t *content_list);
  virtual int PostCommitLayerStack(hwc_display_contents_1_t *content_list);
  inline void SetRect(const hwc_rect_t &source, LayerRect *target);
  inline void SetRect(const hwc_frect_t &source, LayerRect *target);
  inline void SetComposition(const int32_t &source, LayerComposition *target);
  inline void SetComposition(const LayerComposition &source, int32_t *target);
  inline void SetBlending(const int32_t &source, LayerBlending *target);
  int SetFormat(const int32_t &source, const int flags, LayerBufferFormat *target);
  LayerBufferFormat GetSDMFormat(const int32_t &source, const int flags);
  const char *GetHALPixelFormatString(int format);
  const char *GetDisplayString();
  void ScaleDisplayFrame(hwc_rect_t *display_frame);
  void MarkLayersForGPUBypass(hwc_display_contents_1_t *content_list);
  uint32_t RoundToStandardFPS(uint32_t fps);
  virtual void ApplyScanAdjustment(hwc_rect_t *display_frame);
  DisplayError SetCSC(ColorSpace_t source, LayerCSC *target);
  DisplayError SetIGC(IGC_t source, LayerIGC *target);
  DisplayError SetMetaData(const private_handle_t *pvt_handle, Layer *layer);
  bool NeedsFrameBufferRefresh(hwc_display_contents_1_t *content_list);
  void CacheLayerStackInfo(hwc_display_contents_1_t *content_list);
  bool IsLayerUpdating(hwc_display_contents_1_t *content_list, int layer_index);
  bool SingleLayerUpdating(uint32_t app_layer_count);
  uint32_t SanitizeRefreshRate(uint32_t req_refresh_rate);

  enum {
    INPUT_LAYER_DUMP,
    OUTPUT_LAYER_DUMP,
  };

  CoreInterface *core_intf_;
  hwc_procs_t const **hwc_procs_;
  DisplayType type_;
  int id_;
  bool needs_blit_;
  DisplayInterface *display_intf_ = NULL;
  LayerStackMemory layer_stack_memory_;
  LayerStack layer_stack_;
  LayerStackCache layer_stack_cache_;
  bool flush_on_error_ = false;
  bool flush_ = false;
  uint32_t dump_frame_count_ = 0;
  uint32_t dump_frame_index_ = 0;
  bool dump_input_layers_ = false;
  uint32_t last_power_mode_;
  bool swap_interval_zero_ = false;
  DisplayConfigVariableInfo *framebuffer_config_ = NULL;
  bool display_paused_ = false;
  uint32_t min_refresh_rate_ = 0;
  uint32_t max_refresh_rate_ = 0;
  uint32_t current_refresh_rate_ = 0;
  bool use_metadata_refresh_rate_ = false;
  uint32_t metadata_refresh_rate_ = 0;
  uint32_t force_refresh_rate_ = 0;
  bool boot_animation_completed_ = false;
  bool shutdown_pending_ = false;
  bool use_blit_comp_ = false;
  bool secure_display_active_ = false;
  bool skip_prepare_ = false;
  bool solid_fill_enable_ = false;
  uint32_t solid_fill_color_ = 0;
  LayerRect display_rect_;
  std::map<int, LayerBufferS3DFormat> s3d_format_hwc_to_sdm_;

 private:
  bool IsFrameBufferScaled();
  void DumpInputBuffers(hwc_display_contents_1_t *content_list);
  int PrepareLayerParams(hwc_layer_1_t *hwc_layer, Layer *layer);
  void CommitLayerParams(hwc_layer_1_t *hwc_layer, Layer *layer);
  void ResetLayerCacheStack();
  BlitEngine *blit_engine_ = NULL;
};

inline int HWCDisplay::Perform(uint32_t operation, ...) {
  return 0;
}

}  // namespace sdm

#endif  // __HWC_DISPLAY_H__

