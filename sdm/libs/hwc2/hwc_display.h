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

#ifndef __HWC_DISPLAY_H__
#define __HWC_DISPLAY_H__

#include <hardware/hwcomposer.h>
#include <core/core_interface.h>
#include <qdMetaData.h>
#include <QService.h>
#include <private/color_params.h>
#include <map>
#include <set>
#include <queue>
#include <utility>
#include "hwc_callbacks.h"
#include "hwc_layers.h"

namespace sdm {

class BlitEngine;

// Subclasses set this to their type. This has to be different from DisplayType.
// This is to avoid RTTI and dynamic_cast
enum DisplayClass {
  DISPLAY_CLASS_PRIMARY,
  DISPLAY_CLASS_EXTERNAL,
  DISPLAY_CLASS_VIRTUAL,
  DISPLAY_CLASS_NULL
};

class HWCDisplay : public DisplayEventHandler {
 public:
  virtual ~HWCDisplay() {}
  virtual int Init();
  virtual int Deinit();

  // Framebuffer configurations
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);
  virtual void SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type);
  virtual DisplayError SetMaxMixerStages(uint32_t max_mixer_stages);
  virtual DisplayError ControlPartialUpdate(bool enable, uint32_t *pending);
  virtual HWC2::PowerMode GetLastPowerMode();
  virtual int SetFrameBufferResolution(uint32_t x_pixels, uint32_t y_pixels);
  virtual void GetFrameBufferResolution(uint32_t *x_pixels, uint32_t *y_pixels);
  virtual void GetPanelResolution(uint32_t *x_pixels, uint32_t *y_pixels);
  virtual int SetDisplayStatus(uint32_t display_status);
  virtual int OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);
  virtual int Perform(uint32_t operation, ...);
  virtual void SetSecureDisplay(bool secure_display_active);

  // Captures frame output in the buffer specified by output_buffer_info. The API is
  // non-blocking and the client is expected to check operation status later on.
  // Returns -1 if the input is invalid.
  virtual int FrameCaptureAsync(const BufferInfo &output_buffer_info, bool post_processed) {
    return -1;
  }
  // Returns the status of frame capture operation requested with FrameCaptureAsync().
  // -EAGAIN : No status obtain yet, call API again after another frame.
  // < 0 : Operation happened but failed.
  // 0 : Success.
  virtual int GetFrameCaptureStatus() { return -EAGAIN; }

  // Display Configurations
  virtual int SetActiveDisplayConfig(int config);
  virtual int GetActiveDisplayConfig(uint32_t *config);
  virtual int GetDisplayConfigCount(uint32_t *count);
  virtual int GetDisplayAttributesForConfig(int config, DisplayConfigVariableInfo *attributes);

  int SetPanelBrightness(int level);
  int GetPanelBrightness(int *level);
  int ToggleScreenUpdates(bool enable);
  int ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload, PPDisplayAPIPayload *out_payload,
                           PPPendingParams *pending_action);
  DisplayClass GetDisplayClass();
  int GetVisibleDisplayRect(hwc_rect_t *rect);
  void BuildLayerStack(void);
  HWCLayer *GetHWCLayer(hwc2_layer_t layer);

  // HWC2 APIs
  virtual HWC2::Error AcceptDisplayChanges(void);
  virtual HWC2::Error GetActiveConfig(hwc2_config_t *out_config);
  virtual HWC2::Error SetActiveConfig(hwc2_config_t config);
  virtual HWC2::Error SetClientTarget(buffer_handle_t target, int32_t acquire_fence,
                                      int32_t dataspace);
  virtual HWC2::Error GetDisplayConfigs(uint32_t *out_num_configs, hwc2_config_t *out_configs);
  virtual HWC2::Error GetDisplayAttribute(hwc2_config_t config, HWC2::Attribute attribute,
                                          int32_t *out_value);
  virtual HWC2::Error GetClientTargetSupport(uint32_t width, uint32_t height, int32_t format,
                                             int32_t dataspace);
  virtual HWC2::Error GetChangedCompositionTypes(uint32_t *out_num_elements,
                                                 hwc2_layer_t *out_layers, int32_t *out_types);
  virtual HWC2::Error GetDisplayRequests(int32_t *out_display_requests, uint32_t *out_num_elements,
                                         hwc2_layer_t *out_layers, int32_t *out_layer_requests);
  virtual HWC2::Error GetDisplayName(uint32_t *out_size, char *out_name);
  virtual HWC2::Error GetDisplayType(int32_t *out_type);
  virtual HWC2::Error SetCursorPosition(hwc2_layer_t layer, int x, int y);
  virtual HWC2::Error SetVsyncEnabled(HWC2::Vsync enabled);
  virtual HWC2::Error SetPowerMode(HWC2::PowerMode mode);
  virtual HWC2::Error CreateLayer(hwc2_layer_t *out_layer_id);
  virtual HWC2::Error DestroyLayer(hwc2_layer_t layer_id);
  virtual HWC2::Error SetLayerZOrder(hwc2_layer_t layer_id, uint32_t z);
  virtual HWC2::Error Validate(uint32_t *out_num_types, uint32_t *out_num_requests) = 0;
  virtual HWC2::Error GetReleaseFences(uint32_t *out_num_elements, hwc2_layer_t *out_layers,
                                       int32_t *out_fences);
  virtual HWC2::Error Present(int32_t *out_retire_fence) = 0;

 protected:
  enum DisplayStatus {
    kDisplayStatusOffline = 0,
    kDisplayStatusOnline,
    kDisplayStatusPause,
    kDisplayStatusResume,
  };

  // Maximum number of layers supported by display manager.
  static const uint32_t kMaxLayerCount = 32;

  HWCDisplay(CoreInterface *core_intf, HWCCallbacks *callbacks, DisplayType type, hwc2_display_t id,
             bool needs_blit, qService::QService *qservice, DisplayClass display_class);

  // DisplayEventHandler methods
  virtual DisplayError VSync(const DisplayEventVSync &vsync);
  virtual DisplayError Refresh();
  virtual DisplayError CECMessage(char *message);
  virtual void DumpOutputBuffer(const BufferInfo &buffer_info, void *base, int fence);
  virtual HWC2::Error PrepareLayerStack(uint32_t *out_num_types, uint32_t *out_num_requests);
  virtual HWC2::Error CommitLayerStack(void);
  virtual HWC2::Error PostCommitLayerStack(int32_t *out_retire_fence);
  LayerBufferFormat GetSDMFormat(const int32_t &source, const int flags);
  const char *GetHALPixelFormatString(int format);
  const char *GetDisplayString();
  void ScaleDisplayFrame(hwc_rect_t *display_frame);
  void MarkLayersForGPUBypass(void);
  virtual void ApplyScanAdjustment(hwc_rect_t *display_frame);
  bool NeedsFrameBufferRefresh(void);
  bool SingleLayerUpdating(void);
  uint32_t SanitizeRefreshRate(uint32_t req_refresh_rate);
  virtual void CloseAcquireFds();

  enum {
    INPUT_LAYER_DUMP,
    OUTPUT_LAYER_DUMP,
  };

  CoreInterface *core_intf_ = nullptr;
  HWCCallbacks *callbacks_  = nullptr;
  DisplayType type_;
  hwc2_display_t id_;
  bool needs_blit_ = false;
  DisplayInterface *display_intf_ = NULL;
  LayerStack layer_stack_;
  HWCLayer *client_target_ = nullptr;                   // Also known as framebuffer target
  std::map<hwc2_layer_t, HWCLayer *> layer_map_;        // Look up by Id - TODO
  std::multiset<HWCLayer *, SortLayersByZ> layer_set_;  // Maintain a set sorted by Z
  std::map<hwc2_layer_t, HWC2::Composition> layer_changes_;
  std::map<hwc2_layer_t, HWC2::LayerRequest> layer_requests_;
  bool flush_on_error_ = false;
  bool flush_ = false;
  uint32_t dump_frame_count_ = 0;
  uint32_t dump_frame_index_ = 0;
  bool dump_input_layers_ = false;
  HWC2::PowerMode last_power_mode_;
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
  bool validated_ = false;

 private:
  bool IsFrameBufferScaled();
  void DumpInputBuffers(void);
  BlitEngine *blit_engine_ = NULL;
  qService::QService *qservice_ = NULL;
  DisplayClass display_class_;
  int32_t stored_retire_fence_ = -1;
  uint32_t geometry_changes_ = GeometryChanges::kNone;
};

inline int HWCDisplay::Perform(uint32_t operation, ...) {
  return 0;
}

}  // namespace sdm

#endif  // __HWC_DISPLAY_H__
