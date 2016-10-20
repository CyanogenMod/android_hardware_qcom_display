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

#ifndef __HWC_SESSION_H__
#define __HWC_SESSION_H__

#include <core/core_interface.h>
#include <utils/locker.h>

#include "hwc_callbacks.h"
#include "hwc_layers.h"
#include "hwc_display.h"
#include "hwc_display_primary.h"
#include "hwc_display_external.h"
#include "hwc_display_virtual.h"
#include "hwc_color_manager.h"

namespace sdm {

class HWCSession : hwc2_device_t, public qClient::BnQClient {
 public:
  struct HWCModuleMethods : public hw_module_methods_t {
    HWCModuleMethods() { hw_module_methods_t::open = HWCSession::Open; }
  };

  explicit HWCSession(const hw_module_t *module);
  int Init();
  int Deinit();
  HWC2::Error CreateVirtualDisplayObject(uint32_t width, uint32_t height, int32_t *format);

  template <typename... Args>
  static int32_t CallDisplayFunction(hwc2_device_t *device, hwc2_display_t display,
                                     HWC2::Error (HWCDisplay::*member)(Args...), Args... args) {
    if (!device) {
      return HWC2_ERROR_BAD_DISPLAY;
    }

    HWCSession *hwc_session = static_cast<HWCSession *>(device);
    auto status = HWC2::Error::BadDisplay;
    if (hwc_session->hwc_display_[display]) {
      auto hwc_display = hwc_session->hwc_display_[display];
      status = (hwc_display->*member)(std::forward<Args>(args)...);
    }
    return INT32(status);
  }

  template <typename... Args>
  static int32_t CallLayerFunction(hwc2_device_t *device, hwc2_display_t display,
                                   hwc2_layer_t layer, HWC2::Error (HWCLayer::*member)(Args...),
                                   Args... args) {
    if (!device) {
      return HWC2_ERROR_BAD_DISPLAY;
    }

    HWCSession *hwc_session = static_cast<HWCSession *>(device);
    auto status = HWC2::Error::BadDisplay;
    if (hwc_session->hwc_display_[display]) {
      status = HWC2::Error::BadLayer;
      auto hwc_layer = hwc_session->hwc_display_[display]->GetHWCLayer(layer);
      if (hwc_layer != nullptr) {
        status = (hwc_layer->*member)(std::forward<Args>(args)...);
      }
    }
    return INT32(status);
  }

  // HWC2 Functions that require a concrete implementation in hwc session
  // and hence need to be member functions
  static int32_t AcceptDisplayChanges(hwc2_device_t *device, hwc2_display_t display);
  static int32_t CreateLayer(hwc2_device_t *device, hwc2_display_t display,
                             hwc2_layer_t *out_layer_id);
  static int32_t CreateVirtualDisplay(hwc2_device_t *device, uint32_t width, uint32_t height,
                                      int32_t *format, hwc2_display_t *out_display_id);
  static int32_t DestroyLayer(hwc2_device_t *device, hwc2_display_t display, hwc2_layer_t layer);
  static int32_t DestroyVirtualDisplay(hwc2_device_t *device, hwc2_display_t display);
  static void Dump(hwc2_device_t *device, uint32_t *out_size, char *out_buffer);
  static int32_t PresentDisplay(hwc2_device_t *device, hwc2_display_t display,
                                int32_t *out_retire_fence);
  static int32_t RegisterCallback(hwc2_device_t *device, int32_t descriptor,
                                  hwc2_callback_data_t callback_data,
                                  hwc2_function_pointer_t pointer);
  static int32_t SetOutputBuffer(hwc2_device_t *device, hwc2_display_t display,
                                 buffer_handle_t buffer, int32_t releaseFence);
  static int32_t SetLayerZOrder(hwc2_device_t *device, hwc2_display_t display, hwc2_layer_t layer,
                                uint32_t z);
  static int32_t SetPowerMode(hwc2_device_t *device, hwc2_display_t display, int32_t int_mode);
  static int32_t ValidateDisplay(hwc2_device_t *device, hwc2_display_t display,
                                 uint32_t *out_num_types, uint32_t *out_num_requests);
  static int32_t SetColorMode(hwc2_device_t *device, hwc2_display_t display,
                              int32_t /*android_color_mode_t*/ int_mode);
  static int32_t SetColorTransform(hwc2_device_t *device, hwc2_display_t display,
                                   const float *matrix, int32_t /*android_color_transform_t*/ hint);

 private:
  static const int kExternalConnectionTimeoutMs = 500;
  static const int kPartialUpdateControlTimeoutMs = 100;

  // hwc methods
  static int Open(const hw_module_t *module, const char *name, hw_device_t **device);
  static int Close(hw_device_t *device);
  static void GetCapabilities(struct hwc2_device *device, uint32_t *outCount,
                              int32_t *outCapabilities);
  static hwc2_function_pointer_t GetFunction(struct hwc2_device *device, int32_t descriptor);

  // Uevent thread
  static void *HWCUeventThread(void *context);
  void *HWCUeventThreadHandler();
  int GetEventValue(const char *uevent_data, int length, const char *event_info);
  int HotPlugHandler(bool connected);
  void ResetPanel();
  int32_t ConnectDisplay(int disp);
  int DisconnectDisplay(int disp);
  int GetVsyncPeriod(int disp);

  // QClient methods
  virtual android::status_t notifyCallback(uint32_t command, const android::Parcel *input_parcel,
                                           android::Parcel *output_parcel);
  void DynamicDebug(const android::Parcel *input_parcel);
  void SetFrameDumpConfig(const android::Parcel *input_parcel);
  android::status_t SetMaxMixerStages(const android::Parcel *input_parcel);
  android::status_t SetDisplayMode(const android::Parcel *input_parcel);
  android::status_t SetSecondaryDisplayStatus(const android::Parcel *input_parcel,
                                              android::Parcel *output_parcel);
  android::status_t ToggleScreenUpdates(const android::Parcel *input_parcel,
                                        android::Parcel *output_parcel);
  android::status_t ConfigureRefreshRate(const android::Parcel *input_parcel);
  android::status_t QdcmCMDHandler(const android::Parcel *input_parcel,
                                   android::Parcel *output_parcel);
  android::status_t ControlPartialUpdate(const android::Parcel *input_parcel, android::Parcel *out);
  android::status_t OnMinHdcpEncryptionLevelChange(const android::Parcel *input_parcel,
                                                   android::Parcel *output_parcel);
  android::status_t SetPanelBrightness(const android::Parcel *input_parcel,
                                       android::Parcel *output_parcel);
  android::status_t GetPanelBrightness(const android::Parcel *input_parcel,
                                       android::Parcel *output_parcel);
  // These functions return the actual display config info as opposed to FB
  android::status_t HandleSetActiveDisplayConfig(const android::Parcel *input_parcel,
                                                 android::Parcel *output_parcel);
  android::status_t HandleGetActiveDisplayConfig(const android::Parcel *input_parcel,
                                                 android::Parcel *output_parcel);
  android::status_t HandleGetDisplayConfigCount(const android::Parcel *input_parcel,
                                                android::Parcel *output_parcel);
  android::status_t HandleGetDisplayAttributesForConfig(const android::Parcel *input_parcel,
                                                        android::Parcel *output_parcel);
  android::status_t GetVisibleDisplayRect(const android::Parcel *input_parcel,
                                          android::Parcel *output_parcel);

  android::status_t SetDynamicBWForCamera(const android::Parcel *input_parcel,
                                          android::Parcel *output_parcel);
  android::status_t GetBWTransactionStatus(const android::Parcel *input_parcel,
                                           android::Parcel *output_parcel);
  android::status_t SetMixerResolution(const android::Parcel *input_parcel);

  android::status_t SetColorModeOverride(const android::Parcel *input_parcel);

  static Locker locker_;
  CoreInterface *core_intf_ = NULL;
  HWCDisplay *hwc_display_[HWC_NUM_DISPLAY_TYPES] = {NULL};
  HWCCallbacks callbacks_;
  pthread_t uevent_thread_;
  bool uevent_thread_exit_ = false;
  const char *uevent_thread_name_ = "HWC_UeventThread";
  HWCBufferAllocator buffer_allocator_;
  HWCBufferSyncHandler buffer_sync_handler_;
  HWCColorManager *color_mgr_ = NULL;
  bool reset_panel_ = false;
  bool secure_display_active_ = false;
  bool external_pending_connect_ = false;
  bool new_bw_mode_ = false;
  bool need_invalidate_ = false;
  int bw_mode_release_fd_ = -1;
  qService::QService *qservice_ = NULL;
};

}  // namespace sdm

#endif  // __HWC_SESSION_H__
