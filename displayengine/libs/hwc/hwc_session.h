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

#ifndef __HWC_SESSION_H__
#define __HWC_SESSION_H__

#include <hardware/hwcomposer.h>
#include <core/core_interface.h>
#include <utils/locker.h>
#include <IQClient.h>

#include "hwc_display_primary.h"
#include "hwc_display_external.h"
#include "hwc_display_virtual.h"

namespace sde {

class HWCSession : hwc_composer_device_1_t, CoreEventHandler, public qClient::BnQClient {
 public:
  struct HWCModuleMethods : public hw_module_methods_t {
    HWCModuleMethods() {
      hw_module_methods_t::open = HWCSession::Open;
    }
  };

  explicit HWCSession(const hw_module_t *module);
  int Init();
  int Deinit();

 private:
  // hwc methods
  static int Open(const hw_module_t *module, const char* name, hw_device_t **device);
  static int Close(hw_device_t *device);
  static int Prepare(hwc_composer_device_1 *device, size_t num_displays,
                     hwc_display_contents_1_t **displays);
  static int Set(hwc_composer_device_1 *device, size_t num_displays,
                 hwc_display_contents_1_t **displays);
  static int EventControl(hwc_composer_device_1 *device, int disp, int event, int enable);
  static int SetPowerMode(hwc_composer_device_1 *device, int disp, int mode);
  static int Query(hwc_composer_device_1 *device, int param, int *value);
  static void RegisterProcs(hwc_composer_device_1 *device, hwc_procs_t const *procs);
  static void Dump(hwc_composer_device_1 *device, char *buffer, int length);
  static int GetDisplayConfigs(hwc_composer_device_1 *device, int disp, uint32_t *configs,
                               size_t *numConfigs);
  static int GetDisplayAttributes(hwc_composer_device_1 *device, int disp, uint32_t config,
                                  const uint32_t *attributes, int32_t *values);
  static int GetActiveConfig(hwc_composer_device_1 *device, int disp);
  static int SetActiveConfig(hwc_composer_device_1 *device, int disp, int index);

  // Hotplug thread for HDMI connect/disconnect
  static void* HWCHotPlugThread(void *context);
  void* HWCHotPlugThreadHandler();
  int GetHDMIConnectedState(const char *uevent_data, int length);
  int HotPlugHandler(bool connected);
  bool ValidateContentList(hwc_display_contents_1_t *content_list);
  int CreateVirtualDisplay(HWCSession *hwc_session, hwc_display_contents_1_t *content_list);
  int DestroyVirtualDisplay(HWCSession *hwc_session);

  // CoreEventHandler methods
  virtual DisplayError Hotplug(const CoreEventHotplug &hotplug);

  // QClient methods
  virtual android::status_t notifyCallback(uint32_t command, const android::Parcel *input_parcel,
                                           android::Parcel *output_parcel);
  void DynamicDebug(const android::Parcel *input_parcel);
  void SetFrameDumpConfig(const android::Parcel *input_parcel);

  static Locker locker_;
  CoreInterface *core_intf_;
  hwc_procs_t const *hwc_procs_;
  HWCDisplayPrimary *display_primary_;
  HWCDisplayExternal *display_external_;
  HWCDisplayVirtual *display_virtual_;
  pthread_t hotplug_thread_;
  bool hotplug_thread_exit_;
  const char *hotplug_thread_name_;
  HWCBufferAllocator *buffer_allocator_;
  HWCBufferSyncHandler *buffer_sync_handler_;
};

}  // namespace sde

#endif  // __HWC_SESSION_H__

