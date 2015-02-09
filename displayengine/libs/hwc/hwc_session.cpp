/*
* Copyright (c) 2014 - 2015, The Linux Foundation. All rights reserved.
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

#include <core/dump_interface.h>
#include <utils/constants.h>
#include <utils/String16.h>
#include <hardware_legacy/uevent.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <binder/Parcel.h>
#include <QService.h>
#include <core/buffer_allocator.h>

#include "hwc_buffer_allocator.h"
#include "hwc_buffer_sync_handler.h"
#include "hwc_session.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCSession"

static sde::HWCSession::HWCModuleMethods g_hwc_module_methods;

hwc_module_t HAL_MODULE_INFO_SYM = {
  common: {
    tag: HARDWARE_MODULE_TAG,
    version_major: 2,
    version_minor: 0,
    id: HWC_HARDWARE_MODULE_ID,
    name: "QTI Hardware Composer Module",
    author: "CodeAurora Forum",
    methods: &g_hwc_module_methods,
    dso: 0,
    reserved: {0},
  }
};

namespace sde {

Locker HWCSession::locker_;

HWCSession::HWCSession(const hw_module_t *module) : core_intf_(NULL), hwc_procs_(NULL),
            display_primary_(NULL), display_external_(NULL), display_virtual_(NULL),
            hotplug_thread_exit_(false), hotplug_thread_name_("HWC_HotPlugThread") {
  hwc_composer_device_1_t::common.tag = HARDWARE_DEVICE_TAG;
  hwc_composer_device_1_t::common.version = HWC_DEVICE_API_VERSION_1_4;
  hwc_composer_device_1_t::common.module = const_cast<hw_module_t*>(module);
  hwc_composer_device_1_t::common.close = Close;
  hwc_composer_device_1_t::prepare = Prepare;
  hwc_composer_device_1_t::set = Set;
  hwc_composer_device_1_t::eventControl = EventControl;
  hwc_composer_device_1_t::setPowerMode = SetPowerMode;
  hwc_composer_device_1_t::query = Query;
  hwc_composer_device_1_t::registerProcs = RegisterProcs;
  hwc_composer_device_1_t::dump = Dump;
  hwc_composer_device_1_t::getDisplayConfigs = GetDisplayConfigs;
  hwc_composer_device_1_t::getDisplayAttributes = GetDisplayAttributes;
  hwc_composer_device_1_t::getActiveConfig = GetActiveConfig;
  hwc_composer_device_1_t::setActiveConfig = SetActiveConfig;
}

int HWCSession::Init() {
  int status = -EINVAL;
  const char *qservice_name = "display.qservice";

  // Start QService and connect to it.
  qService::QService::init();
  android::sp<qService::IQService> qservice = android::interface_cast<qService::IQService>(
                android::defaultServiceManager()->getService(android::String16(qservice_name)));

  if (qservice.get()) {
    qservice->connect(this);
  } else {
    DLOGE("Failed to acquire %s", qservice_name);
    return -EINVAL;
  }

  buffer_allocator_ = new HWCBufferAllocator();
  if (buffer_allocator_ == NULL) {
    DLOGE("Display core initialization failed due to no memory");
    return -ENOMEM;
  }

  buffer_sync_handler_ = new HWCBufferSyncHandler();
  if (buffer_sync_handler_ == NULL) {
    DLOGE("Display core initialization failed due to no memory");
    return -ENOMEM;
  }

  DisplayError error = CoreInterface::CreateCore(this, HWCDebugHandler::Get(), buffer_allocator_,
                                                 buffer_sync_handler_, &core_intf_);
  if (error != kErrorNone) {
    DLOGE("Display core initialization failed. Error = %d", error);
    return -EINVAL;
  }

  // Create and power on primary display
  display_primary_ = new HWCDisplayPrimary(core_intf_, &hwc_procs_);
  if (!display_primary_) {
    CoreInterface::DestroyCore();
    return -ENOMEM;
  }

  status = display_primary_->Init();
  if (status) {
    CoreInterface::DestroyCore();
    delete display_primary_;
    return status;
  }

  status = display_primary_->SetPowerMode(HWC_POWER_MODE_NORMAL);
  if (status) {
    display_primary_->Deinit();
    delete display_primary_;
    CoreInterface::DestroyCore();
    return status;
  }

  if (pthread_create(&hotplug_thread_, NULL, &HWCHotPlugThread, this) < 0) {
    DLOGE("Failed to start = %s, error = %s HDMI display Not supported", hotplug_thread_name_);
    display_primary_->Deinit();
    delete display_primary_;
    CoreInterface::DestroyCore();
    return -errno;
  }

  return 0;
}

int HWCSession::Deinit() {
  display_primary_->SetPowerMode(HWC_POWER_MODE_OFF);
  display_primary_->Deinit();
  delete display_primary_;
  hotplug_thread_exit_ = true;
  pthread_join(hotplug_thread_, NULL);

  DisplayError error = CoreInterface::DestroyCore();
  if (error != kErrorNone) {
    DLOGE("Display core de-initialization failed. Error = %d", error);
  }

  return 0;
}

int HWCSession::Open(const hw_module_t *module, const char *name, hw_device_t **device) {
  if (!module || !name || !device) {
    DLOGE("Invalid parameters.");
    return -EINVAL;
  }

  if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
    HWCSession *hwc_session = new HWCSession(module);
    if (!hwc_session) {
      return -ENOMEM;
    }

    int status = hwc_session->Init();
    if (status != 0) {
      delete hwc_session;
      return status;
    }

    hwc_composer_device_1_t *composer_device = hwc_session;
    *device = reinterpret_cast<hw_device_t *>(composer_device);
  }

  return 0;
}

int HWCSession::Close(hw_device_t *device) {
  if (!device) {
    return -EINVAL;
  }

  hwc_composer_device_1_t *composer_device = reinterpret_cast<hwc_composer_device_1_t *>(device);
  HWCSession *hwc_session = static_cast<HWCSession *>(composer_device);

  hwc_session->Deinit();
  delete hwc_session;

  return 0;
}

int HWCSession::Prepare(hwc_composer_device_1 *device, size_t num_displays,
                        hwc_display_contents_1_t **displays) {
  DTRACE_SCOPED();

  SEQUENCE_ENTRY_SCOPE_LOCK(locker_);

  if (!device || !displays) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);

  for (ssize_t i = (num_displays-1); i >= 0; i--) {
    hwc_display_contents_1_t *content_list = displays[i];

    switch (i) {
    case HWC_DISPLAY_PRIMARY:
      hwc_session->display_primary_->Prepare(content_list);
      break;
    case HWC_DISPLAY_EXTERNAL:
      if (hwc_session->display_external_) {
        hwc_session->display_external_->Prepare(content_list);
      }
      break;
    case HWC_DISPLAY_VIRTUAL:
      if (hwc_session->ValidateContentList(content_list)) {
        hwc_session->CreateVirtualDisplay(hwc_session, content_list);
      } else {
        hwc_session->DestroyVirtualDisplay(hwc_session);
      }

      if (hwc_session->display_virtual_) {
        hwc_session->display_virtual_->Prepare(content_list);
      }
      break;
    default:
      break;
    }
  }

  // Return 0, else client will go into bad state
  return 0;
}

int HWCSession::Set(hwc_composer_device_1 *device, size_t num_displays,
                    hwc_display_contents_1_t **displays) {
  DTRACE_SCOPED();

  SEQUENCE_EXIT_SCOPE_LOCK(locker_);

  if (!device || !displays) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);

  for (size_t i = 0; i < num_displays; i++) {
    hwc_display_contents_1_t *content_list = displays[i];

    switch (i) {
    case HWC_DISPLAY_PRIMARY:
      hwc_session->display_primary_->Commit(content_list);
      break;
    case HWC_DISPLAY_EXTERNAL:
      if (hwc_session->display_external_) {
        hwc_session->display_external_->Commit(content_list);
      }
      break;
    case HWC_DISPLAY_VIRTUAL:
      if (hwc_session->display_virtual_) {
        hwc_session->display_virtual_->Commit(content_list);
      }
      break;
    default:
      break;
    }
  }

  // Return 0, else client will go into bad state
  return 0;
}

int HWCSession::EventControl(hwc_composer_device_1 *device, int disp, int event, int enable) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  switch (disp) {
  case HWC_DISPLAY_PRIMARY:
    status = hwc_session->display_primary_->EventControl(event, enable);
    break;
  case HWC_DISPLAY_EXTERNAL:
    if (hwc_session->display_external_) {
      status = hwc_session->display_external_->EventControl(event, enable);
    }
    break;
  case HWC_DISPLAY_VIRTUAL:
    break;
  default:
    status = -EINVAL;
  }

  return status;
}

int HWCSession::SetPowerMode(hwc_composer_device_1 *device, int disp, int mode) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  switch (disp) {
  case HWC_DISPLAY_PRIMARY:
    status = hwc_session->display_primary_->SetPowerMode(mode);
    break;
  case HWC_DISPLAY_EXTERNAL:
    if (hwc_session->display_external_) {
      status = hwc_session->display_external_->SetPowerMode(mode);
    }
    break;
  case HWC_DISPLAY_VIRTUAL:
    if (hwc_session->display_virtual_) {
      status = hwc_session->display_virtual_->SetPowerMode(mode);
    }
    break;
  default:
    status = -EINVAL;
  }

  return status;
}

int HWCSession::Query(hwc_composer_device_1 *device, int param, int *value) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device || !value) {
    return -EINVAL;
  }

  return -EINVAL;
}

void HWCSession::RegisterProcs(hwc_composer_device_1 *device, hwc_procs_t const *procs) {
  if (!device || !procs) {
    return;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  hwc_session->hwc_procs_ = procs;
}

void HWCSession::Dump(hwc_composer_device_1 *device, char *buffer, int length) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device || !buffer || !length) {
    return;
  }

  DumpInterface::GetDump(buffer, length);
}

int HWCSession::GetDisplayConfigs(hwc_composer_device_1 *device, int disp, uint32_t *configs,
                                  size_t *num_configs) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device || !configs || !num_configs) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  switch (disp) {
  case HWC_DISPLAY_PRIMARY:
    status = hwc_session->display_primary_->GetDisplayConfigs(configs, num_configs);
    break;
  case HWC_DISPLAY_EXTERNAL:
    if (hwc_session->display_external_) {
      status = hwc_session->display_external_->GetDisplayConfigs(configs, num_configs);
    }
    break;
  case HWC_DISPLAY_VIRTUAL:
    if (hwc_session->display_virtual_) {
      status = hwc_session->display_virtual_->GetDisplayConfigs(configs, num_configs);
    }
    break;
  default:
    status = -EINVAL;
  }

  return status;
}

int HWCSession::GetDisplayAttributes(hwc_composer_device_1 *device, int disp, uint32_t config,
                                     const uint32_t *attributes, int32_t *values) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device || !attributes || !values) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  switch (disp) {
  case HWC_DISPLAY_PRIMARY:
    status = hwc_session->display_primary_->GetDisplayAttributes(config, attributes, values);
    break;
  case HWC_DISPLAY_EXTERNAL:
    if (hwc_session->display_external_) {
      status = hwc_session->display_external_->GetDisplayAttributes(config, attributes, values);
    }
    break;
  case HWC_DISPLAY_VIRTUAL:
    if (hwc_session->display_virtual_) {
      status = hwc_session->display_virtual_->GetDisplayAttributes(config, attributes, values);
    }
    break;
  default:
    status = -EINVAL;
  }

  return status;
}

int HWCSession::GetActiveConfig(hwc_composer_device_1 *device, int disp) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device) {
    return -1;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int active_config = -1;

  switch (disp) {
  case HWC_DISPLAY_PRIMARY:
    active_config = hwc_session->display_primary_->GetActiveConfig();
    break;
  case HWC_DISPLAY_EXTERNAL:
    if (hwc_session->display_external_) {
      active_config = hwc_session->display_external_->GetActiveConfig();
    }
    break;
  case HWC_DISPLAY_VIRTUAL:
    if (hwc_session->display_virtual_) {
      active_config = hwc_session->display_virtual_->GetActiveConfig();
    }
    break;
  default:
    active_config = -1;
  }

  return active_config;
}

int HWCSession::SetActiveConfig(hwc_composer_device_1 *device, int disp, int index) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  switch (disp) {
  case HWC_DISPLAY_PRIMARY:
    status = hwc_session->display_primary_->SetActiveConfig(index);
    break;
  case HWC_DISPLAY_EXTERNAL:
    if (hwc_session->display_external_) {
      // TODO(user): Uncomment it. HDMI does not support resolution change currently.
      status = 0;  // hwc_session->display_external_->SetActiveConfig(index);
    }
    break;
  case HWC_DISPLAY_VIRTUAL:
    break;
  default:
    status = -EINVAL;
  }

  return status;
}

bool HWCSession::ValidateContentList(hwc_display_contents_1_t *content_list) {
  return (content_list && content_list->numHwLayers > 0 && content_list->outbuf);
}

int HWCSession::CreateVirtualDisplay(HWCSession *hwc_session,
                                     hwc_display_contents_1_t *content_list) {
  int status = 0;

  if (!hwc_session->display_virtual_) {
    // Create virtual display device
    hwc_session->display_virtual_ = new HWCDisplayVirtual(hwc_session->core_intf_,
                                                          &hwc_session->hwc_procs_);
    if (!hwc_session->display_virtual_) {
      // This is not catastrophic. Leave a warning message for now.
      DLOGW("Virtual Display creation failed");
      return -ENOMEM;
    }

    status = hwc_session->display_virtual_->Init();
    if (status) {
      goto CleanupOnError;
    }

    status = hwc_session->display_virtual_->SetPowerMode(HWC_POWER_MODE_NORMAL);
    if (status) {
      goto CleanupOnError;
    }
  }

  if (hwc_session->display_virtual_) {
    status = hwc_session->display_virtual_->SetActiveConfig(content_list);
  }

  return status;

CleanupOnError:
  return hwc_session->DestroyVirtualDisplay(hwc_session);
}

int HWCSession::DestroyVirtualDisplay(HWCSession *hwc_session) {
  int status = 0;

  if (hwc_session->display_virtual_) {
    status = hwc_session->display_virtual_->Deinit();
    if (!status) {
      delete hwc_session->display_virtual_;
      hwc_session->display_virtual_ = NULL;
    }
  }

  return status;
}

DisplayError HWCSession::Hotplug(const CoreEventHotplug &hotplug) {
  return kErrorNone;
}

android::status_t HWCSession::notifyCallback(uint32_t command, const android::Parcel *input_parcel,
                                             android::Parcel */*output_parcel*/) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  switch (command) {
  case qService::IQService::DYNAMIC_DEBUG:
    DynamicDebug(input_parcel);
    break;

  case qService::IQService::SCREEN_REFRESH:
    hwc_procs_->invalidate(hwc_procs_);
    break;

  case qService::IQService::SET_IDLE_TIMEOUT:
    if (display_primary_) {
      uint32_t timeout = (uint32_t)input_parcel->readInt32();
      display_primary_->SetIdleTimeoutMs(timeout);
    }
    break;

  case qService::IQService::SET_FRAME_DUMP_CONFIG:
    SetFrameDumpConfig(input_parcel);
    break;

  default:
    DLOGW("QService command = %d is not supported", command);
    return -EINVAL;
  }

  return 0;
}

void HWCSession::SetFrameDumpConfig(const android::Parcel *input_parcel) {
  uint32_t frame_dump_count = UINT32(input_parcel->readInt32());
  uint32_t bit_mask_display_type = UINT32(input_parcel->readInt32());
  uint32_t bit_mask_layer_type = UINT32(input_parcel->readInt32());

  if (bit_mask_display_type & (1 << qService::IQService::DUMP_PRIMARY_DISPLAY)) {
    if (display_primary_) {
      display_primary_->SetFrameDumpConfig(frame_dump_count, bit_mask_layer_type);
    }
  }

  if (bit_mask_display_type & (1 << qService::IQService::DUMP_HDMI_DISPLAY)) {
    if (display_external_) {
      display_external_->SetFrameDumpConfig(frame_dump_count, bit_mask_layer_type);
    }
  }

  if (bit_mask_display_type & (1 << qService::IQService::DUMP_VIRTUAL_DISPLAY)) {
    if (display_virtual_) {
      display_virtual_->SetFrameDumpConfig(frame_dump_count, bit_mask_layer_type);
    }
  }
}

void HWCSession::DynamicDebug(const android::Parcel *input_parcel) {
  int type = input_parcel->readInt32();
  bool enable = (input_parcel->readInt32() > 0);
  DLOGI("type = %d enable = %d", type, enable);

  switch (type) {
  case qService::IQService::DEBUG_ALL:
    HWCDebugHandler::DebugAll(enable);
    break;

  case qService::IQService::DEBUG_MDPCOMP:
    HWCDebugHandler::DebugStrategy(enable);
    HWCDebugHandler::DebugCompManager(enable);
    break;

  case qService::IQService::DEBUG_PIPE_LIFECYCLE:
    HWCDebugHandler::DebugResources(enable);
    break;

  case qService::IQService::DEBUG_DRIVER_CONFIG:
    HWCDebugHandler::DebugDriverConfig(enable);
    break;

  case qService::IQService::DEBUG_ROTATOR:
    HWCDebugHandler::DebugResources(enable);
    HWCDebugHandler::DebugDriverConfig(enable);
    HWCDebugHandler::DebugBufferManager(enable);
    HWCDebugHandler::DebugOfflineCtrl(enable);
    break;

  default:
    DLOGW("type = %d is not supported", type);
  }
}

void* HWCSession::HWCHotPlugThread(void *context) {
  if (context) {
    return reinterpret_cast<HWCSession *>(context)->HWCHotPlugThreadHandler();
  }

  return NULL;
}

void* HWCSession::HWCHotPlugThreadHandler() {
  static char uevent_data[PAGE_SIZE];
  int length = 0;
  prctl(PR_SET_NAME, hotplug_thread_name_, 0, 0, 0);
  setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);
  if (!uevent_init()) {
    DLOGE("Failed to init uevent");
    pthread_exit(0);
    return NULL;
  }

  while (!hotplug_thread_exit_) {
    // keep last 2 zeroes to ensure double 0 termination
    length = uevent_next_event(uevent_data, INT32(sizeof(uevent_data)) - 2);

    if (!strcasestr("change@/devices/virtual/switch/hdmi", uevent_data)) {
      continue;
    }
    DLOGI("Uevent HDMI = %s", uevent_data);
    int connected = GetHDMIConnectedState(uevent_data, length);
    if (connected >= 0) {
      DLOGI("HDMI = %s", connected ? "connected" : "disconnected");
      if (HotPlugHandler(connected) == -1) {
        DLOGE("Failed handling Hotplug = %s", connected ? "connected" : "disconnected");
      }
    }
  }
  pthread_exit(0);

  return NULL;
}

int HWCSession::GetHDMIConnectedState(const char *uevent_data, int length) {
  const char* iterator_str = uevent_data;
  while (((iterator_str - uevent_data) <= length) && (*iterator_str)) {
    char* pstr = strstr(iterator_str, "SWITCH_STATE=");
    if (pstr != NULL) {
      return (atoi(iterator_str + strlen("SWITCH_STATE=")));
    }
    iterator_str += strlen(iterator_str) + 1;
  }
  return -1;
}

int HWCSession::HotPlugHandler(bool connected) {
  if (!hwc_procs_) {
     DLOGW("Ignore hotplug - hwc_proc not registered");
    return -1;
  }

  if (connected) {
    SEQUENCE_WAIT_SCOPE_LOCK(locker_);
    if (display_external_) {
     DLOGE("HDMI already connected");
     return -1;
    }
    // Create hdmi display
    display_external_ = new HWCDisplayExternal(core_intf_, &hwc_procs_);
    if (!display_external_) {
      return -1;
    }
    int status = display_external_->Init();
    if (status) {
      delete display_external_;
      display_external_ = NULL;
      return -1;
    }
  } else {
    SEQUENCE_WAIT_SCOPE_LOCK(locker_);
    if (!display_external_) {
     DLOGE("HDMI not connected");
     return -1;
    }
    display_external_->SetPowerMode(HWC_POWER_MODE_OFF);
    display_external_->Deinit();
    delete display_external_;
    display_external_ = NULL;
  }

  // notify client and trigger a screen refresh
  hwc_procs_->hotplug(hwc_procs_, HWC_DISPLAY_EXTERNAL, connected);
  hwc_procs_->invalidate(hwc_procs_);

  return 0;
}

}  // namespace sde

