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
#include <core/buffer_allocator.h>
#include <private/color_params.h>
#include <utils/constants.h>
#include <utils/String16.h>
#include <cutils/properties.h>
#include <hardware_legacy/uevent.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <binder/Parcel.h>
#include <QService.h>
#include <gr.h>
#include <gralloc_priv.h>
#include <display_config.h>

#include "hwc_buffer_allocator.h"
#include "hwc_buffer_sync_handler.h"
#include "hwc_session.h"
#include "hwc_debugger.h"
#include "hwc_display_primary.h"
#include "hwc_display_virtual.h"

#define __CLASS__ "HWCSession"

#define HWC_UEVENT_SWITCH_HDMI "change@/devices/virtual/switch/hdmi"
#define HWC_UEVENT_GRAPHICS_FB0 "change@/devices/virtual/graphics/fb0"

static sdm::HWCSession::HWCModuleMethods g_hwc_module_methods;

hwc_module_t HAL_MODULE_INFO_SYM = {
  .common = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 2,
    .version_minor = 0,
    .id = HWC_HARDWARE_MODULE_ID,
    .name = "QTI Hardware Composer Module",
    .author = "CodeAurora Forum",
    .methods = &g_hwc_module_methods,
    .dso = 0,
    .reserved = {0},
  }
};

namespace sdm {

Locker HWCSession::locker_;
bool HWCSession::reset_panel_ = false;

static void Invalidate(const struct hwc_procs *procs) {
}

static void VSync(const struct hwc_procs* procs, int disp, int64_t timestamp) {
}

static void Hotplug(const struct hwc_procs* procs, int disp, int connected) {
}

HWCSession::HWCSession(const hw_module_t *module) {
  // By default, drop any events. Calls will be routed to SurfaceFlinger after registerProcs.
  hwc_procs_default_.invalidate = Invalidate;
  hwc_procs_default_.vsync = VSync;
  hwc_procs_default_.hotplug = Hotplug;

  hwc_composer_device_1_t::common.tag = HARDWARE_DEVICE_TAG;
  hwc_composer_device_1_t::common.version = HWC_DEVICE_API_VERSION_1_5;
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
  hwc_composer_device_1_t::setCursorPositionAsync = SetCursorPositionAsync;
}

int HWCSession::Init() {
  int status = -EINVAL;
  const char *qservice_name = "display.qservice";

  // Start QService and connect to it.
  qService::QService::init();
  android::sp<qService::IQService> qservice = android::interface_cast<qService::IQService>(
                android::defaultServiceManager()->getService(android::String16(qservice_name)));

  if (qservice.get()) {
    qservice->connect(android::sp<qClient::IQClient>(this));
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

  DisplayError error = CoreInterface::CreateCore(HWCDebugHandler::Get(), buffer_allocator_,
                                                 buffer_sync_handler_, &core_intf_);
  if (error != kErrorNone) {
    DLOGE("Display core initialization failed. Error = %d", error);
    return -EINVAL;
  }

  // Create and power on primary display
  status = HWCDisplayPrimary::Create(core_intf_, &hwc_procs_,
                                     &hwc_display_[HWC_DISPLAY_PRIMARY]);
  if (status) {
    CoreInterface::DestroyCore();
    return status;
  }

  color_mgr_ = HWCColorManager::CreateColorManager();
  if (!color_mgr_) {
    DLOGW("Failed to load HWCColorManager.");
  }

  if (pthread_create(&uevent_thread_, NULL, &HWCUeventThread, this) < 0) {
    DLOGE("Failed to start = %s, error = %s", uevent_thread_name_, strerror(errno));
    HWCDisplayPrimary::Destroy(hwc_display_[HWC_DISPLAY_PRIMARY]);
    hwc_display_[HWC_DISPLAY_PRIMARY] = 0;
    CoreInterface::DestroyCore();
    return -errno;
  }

  return 0;
}

int HWCSession::Deinit() {
  HWCDisplayPrimary::Destroy(hwc_display_[HWC_DISPLAY_PRIMARY]);
  hwc_display_[HWC_DISPLAY_PRIMARY] = 0;
  if (color_mgr_) {
    color_mgr_->DestroyColorManager();
  }
  uevent_thread_exit_ = true;
  pthread_join(uevent_thread_, NULL);

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

  if (reset_panel_) {
    DLOGW("panel is in bad state, resetting the panel");
    hwc_session->ResetPanel();
  }

  hwc_session->HandleSecureDisplaySession(displays);

  if (hwc_session->color_mgr_) {
    HWCDisplay *primary_display = hwc_session->hwc_display_[HWC_DISPLAY_PRIMARY];
    if (primary_display) {
      int ret = hwc_session->color_mgr_->SolidFillLayersPrepare(displays, primary_display);
      if (ret)
        return 0;
    }
  }

  for (ssize_t dpy = static_cast<ssize_t>(num_displays - 1); dpy >= 0; dpy--) {
    hwc_display_contents_1_t *content_list = displays[dpy];
    if (dpy == HWC_DISPLAY_VIRTUAL) {
      if (hwc_session->hwc_display_[HWC_DISPLAY_EXTERNAL]) {
        continue;
      }
      hwc_session->HandleVirtualDisplayLifeCycle(content_list);
    }

    if (hwc_session->hwc_display_[dpy]) {
      hwc_session->hwc_display_[dpy]->Prepare(content_list);
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

  if (hwc_session->color_mgr_) {
    HWCDisplay *primary_display = hwc_session->hwc_display_[HWC_DISPLAY_PRIMARY];
    if (primary_display) {
      int ret = hwc_session->color_mgr_->SolidFillLayersSet(displays, primary_display);
      if (ret)
        return 0;
    }
  }

  for (size_t dpy = 0; dpy < num_displays; dpy++) {
    hwc_display_contents_1_t *content_list = displays[dpy];
    if (dpy == HWC_DISPLAY_VIRTUAL) {
      if (hwc_session->hwc_display_[HWC_DISPLAY_EXTERNAL]) {
        if (content_list) {
          for (size_t i = 0; i < content_list->numHwLayers; i++) {
            if (content_list->hwLayers[i].acquireFenceFd >= 0) {
              close(content_list->hwLayers[i].acquireFenceFd);
              content_list->hwLayers[i].acquireFenceFd = -1;
            }
          }
          if (content_list->outbufAcquireFenceFd >= 0) {
            close(content_list->outbufAcquireFenceFd);
            content_list->outbufAcquireFenceFd = -1;
          }
          content_list->retireFenceFd = -1;
        }
      }
    }

    if (hwc_session->hwc_display_[dpy]) {
      hwc_session->hwc_display_[dpy]->Commit(content_list);
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
  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->EventControl(event, enable);
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
  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->SetPowerMode(mode);
  }
  if (disp == HWC_DISPLAY_PRIMARY && hwc_session->hwc_display_[HWC_DISPLAY_VIRTUAL]) {
    // Set the power mode for virtual display while setting power mode for primary, as SF
    // does not invoke SetPowerMode() for virtual display.
    status = hwc_session->hwc_display_[HWC_DISPLAY_VIRTUAL]->SetPowerMode(mode);
  }

  return status;
}

int HWCSession::Query(hwc_composer_device_1 *device, int param, int *value) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device || !value) {
    return -EINVAL;
  }

  int status = 0;

  switch (param) {
  case HWC_BACKGROUND_LAYER_SUPPORTED:
    value[0] = 1;
    break;

  default:
    status = -EINVAL;
  }

  return status;
}

void HWCSession::RegisterProcs(hwc_composer_device_1 *device, hwc_procs_t const *procs) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

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

  if (disp < HWC_DISPLAY_PRIMARY || disp >  HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;
  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->GetDisplayConfigs(configs, num_configs);
  }

  return status;
}

int HWCSession::GetDisplayAttributes(hwc_composer_device_1 *device, int disp, uint32_t config,
                                     const uint32_t *attributes, int32_t *values) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device || !attributes || !values) {
    return -EINVAL;
  }

  if (disp < HWC_DISPLAY_PRIMARY || disp >  HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;
  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->GetDisplayAttributes(config, attributes, values);
  }

  return status;
}

int HWCSession::GetActiveConfig(hwc_composer_device_1 *device, int disp) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device) {
    return -EINVAL;
  }

  if (disp < HWC_DISPLAY_PRIMARY || disp >  HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int active_config = -1;
  if (hwc_session->hwc_display_[disp]) {
    active_config = hwc_session->hwc_display_[disp]->GetActiveConfig();
  }

  return active_config;
}

int HWCSession::SetActiveConfig(hwc_composer_device_1 *device, int disp, int index) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device) {
    return -EINVAL;
  }

  if (disp < HWC_DISPLAY_PRIMARY || disp >  HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->SetActiveConfig(index);
  }

  return status;
}

int HWCSession::SetCursorPositionAsync(hwc_composer_device_1 *device, int disp, int x, int y) {
  DTRACE_SCOPED();

  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device || (disp < HWC_DISPLAY_PRIMARY) || (disp > HWC_DISPLAY_VIRTUAL)) {
    return -EINVAL;
  }

  int status = -EINVAL;
  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->SetCursorPosition(x, y);
  }

  return status;
}

int HWCSession::HandleVirtualDisplayLifeCycle(hwc_display_contents_1_t *content_list) {
  int status = 0;

  if (HWCDisplayVirtual::IsValidContentList(content_list)) {
    if (!hwc_display_[HWC_DISPLAY_VIRTUAL]) {
      uint32_t primary_width = 0;
      uint32_t primary_height = 0;
      hwc_display_[HWC_DISPLAY_PRIMARY]->GetFrameBufferResolution(&primary_width, &primary_height);
      // Create virtual display device
      status = HWCDisplayVirtual::Create(core_intf_, &hwc_procs_, primary_width, primary_height,
                                         content_list, &hwc_display_[HWC_DISPLAY_VIRTUAL]);
      if (hwc_display_[HWC_DISPLAY_VIRTUAL]) {
        hwc_display_[HWC_DISPLAY_VIRTUAL]->SetSecureDisplay(secure_display_active_);
      }
    }
  } else {
    if (hwc_display_[HWC_DISPLAY_VIRTUAL]) {
      HWCDisplayVirtual::Destroy(hwc_display_[HWC_DISPLAY_VIRTUAL]);
      hwc_display_[HWC_DISPLAY_VIRTUAL] = 0;
      // Signal the HotPlug thread to continue with the external display connection
      locker_.Signal();
    }
  }

  return status;
}

android::status_t HWCSession::notifyCallback(uint32_t command, const android::Parcel *input_parcel,
                                             android::Parcel *output_parcel) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  android::status_t status = 0;

  switch (command) {
  case qService::IQService::DYNAMIC_DEBUG:
    DynamicDebug(input_parcel);
    break;

  case qService::IQService::SCREEN_REFRESH:
    hwc_procs_->invalidate(hwc_procs_);
    break;

  case qService::IQService::SET_IDLE_TIMEOUT:
    if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
      uint32_t timeout = UINT32(input_parcel->readInt32());
      hwc_display_[HWC_DISPLAY_PRIMARY]->SetIdleTimeoutMs(timeout);
    }
    break;

  case qService::IQService::SET_FRAME_DUMP_CONFIG:
    SetFrameDumpConfig(input_parcel);
    break;

  case qService::IQService::SET_MAX_PIPES_PER_MIXER:
    status = SetMaxMixerStages(input_parcel);
    break;

  case qService::IQService::SET_DISPLAY_MODE:
    status = SetDisplayMode(input_parcel);
    break;
  case qService::IQService::SET_SECONDARY_DISPLAY_STATUS:
    status = SetSecondaryDisplayStatus(input_parcel);
    break;

  case qService::IQService::CONFIGURE_DYN_REFRESH_RATE:
    status = ConfigureRefreshRate(input_parcel);
    break;

  case qService::IQService::SET_VIEW_FRAME:
    break;

  case qService::IQService::CONTROL_BACKLIGHT:
    status = ControlBackLight(input_parcel);
    break;

  case qService::IQService::QDCM_SVC_CMDS:
    status = QdcmCMDHandler(input_parcel, output_parcel);
    break;

  case qService::IQService::MIN_HDCP_ENCRYPTION_LEVEL_CHANGED:
    status = OnMinHdcpEncryptionLevelChange(input_parcel, output_parcel);
    break;

  case qService::IQService::CONTROL_PARTIAL_UPDATE:
    status = ControlPartialUpdate(input_parcel, output_parcel);
    break;

  case qService::IQService::SET_ACTIVE_CONFIG:
    status = HandleSetActiveDisplayConfig(input_parcel, output_parcel);
    break;

  case qService::IQService::GET_ACTIVE_CONFIG:
    status = HandleGetActiveDisplayConfig(input_parcel, output_parcel);
    break;

  case qService::IQService::GET_CONFIG_COUNT:
    status = HandleGetDisplayConfigCount(input_parcel, output_parcel);
    break;

  case qService::IQService::GET_DISPLAY_ATTRIBUTES_FOR_CONFIG:
    status = HandleGetDisplayAttributesForConfig(input_parcel, output_parcel);
    break;

  default:
    DLOGW("QService command = %d is not supported", command);
    return -EINVAL;
  }

  return status;
}

android::status_t HWCSession::ControlBackLight(const android::Parcel *input_parcel) {
  uint32_t display_status = UINT32(input_parcel->readInt32());
  HWCDisplay *display = hwc_display_[HWC_DISPLAY_PRIMARY];

  DLOGI("Primary Display display_status = %d", display_status);

  int fd = open("/sys/class/leds/lcd-backlight/brightness", O_RDWR);
  const char *bl_brightness = "0";

  if (fd < 0) {
    DLOGE("unable to open brightness node err = %d errstr = %s", errno, strerror(errno));
    return -1;
  }

  if (display_status == 0) {
    // Read backlight and store it internally. Set backlight to 0 on primary.
    if (read(fd, brightness_, sizeof(brightness_)) > 0) {
      DLOGI("backlight brightness is %s", brightness_);
      ssize_t ret = write(fd, bl_brightness, strlen(bl_brightness));
      if (ret < 0) {
        DLOGE("Failed to write backlight node err = %d errstr = %s", errno, strerror(errno));
        close(fd);
        return -1;
      }
    }
  } else {
    // Restore backlight to original value.
    ssize_t ret = write(fd, brightness_, sizeof(brightness_));
    if (ret < 0) {
      DLOGE("Failed to write backlight node err = %d errstr = %s", errno, strerror(errno));
      close(fd);
      return -1;
    }
  }
  close(fd);

  return display->SetDisplayStatus(display_status);
}

android::status_t HWCSession::ControlPartialUpdate(const android::Parcel *input_parcel,
                                                   android::Parcel *out) {
  DisplayError error = kErrorNone;
  int ret = 0;
  uint32_t disp_id = UINT32(input_parcel->readInt32());
  uint32_t enable = UINT32(input_parcel->readInt32());

  if (disp_id != HWC_DISPLAY_PRIMARY) {
    DLOGW("CONTROL_PARTIAL_UPDATE is not applicable for display = %d", disp_id);
    ret = -EINVAL;
    out->writeInt32(ret);
    return ret;
  }

  if (!hwc_display_[HWC_DISPLAY_PRIMARY]) {
    DLOGE("primary display object is not instantiated");
    ret = -EINVAL;
    out->writeInt32(ret);
    return ret;
  }

  uint32_t pending = 0;
  error = hwc_display_[HWC_DISPLAY_PRIMARY]->ControlPartialUpdate(enable, &pending);

  if (error == kErrorNone) {
    if (!pending) {
      out->writeInt32(ret);
      return ret;
    }
  } else if (error == kErrorNotSupported) {
    out->writeInt32(ret);
    return ret;
  } else {
    ret = -EINVAL;
    out->writeInt32(ret);
    return ret;
  }

  hwc_procs_->invalidate(hwc_procs_);

  // Wait until partial update control is complete
  ret = locker_.WaitFinite(kPartialUpdateControlTimeoutMs);
  locker_.Unlock();

  out->writeInt32(ret);

  return ret;
}

android::status_t HWCSession::HandleSetActiveDisplayConfig(const android::Parcel *input_parcel,
                                                     android::Parcel *output_parcel) {
  int config = input_parcel->readInt32();
  int dpy = input_parcel->readInt32();
  int error = android::BAD_VALUE;

  if (dpy > HWC_DISPLAY_VIRTUAL) {
    return android::BAD_VALUE;
  }

  if (hwc_display_[dpy]) {
    error = hwc_display_[dpy]->SetActiveDisplayConfig(config);
    if (error == 0) {
      hwc_procs_->invalidate(hwc_procs_);
    }
  }

  return error;
}

android::status_t HWCSession::HandleGetActiveDisplayConfig(const android::Parcel *input_parcel,
                                                           android::Parcel *output_parcel) {
  int dpy = input_parcel->readInt32();
  int error = android::BAD_VALUE;

  if (dpy > HWC_DISPLAY_VIRTUAL) {
    return android::BAD_VALUE;
  }

  if (hwc_display_[dpy]) {
    uint32_t config = 0;
    error = hwc_display_[dpy]->GetActiveDisplayConfig(&config);
    if (error == 0) {
      output_parcel->writeInt32(config);
    }
  }

  return error;
}

android::status_t HWCSession::HandleGetDisplayConfigCount(const android::Parcel *input_parcel,
                                                          android::Parcel *output_parcel) {
  int dpy = input_parcel->readInt32();
  int error = android::BAD_VALUE;

  if (dpy > HWC_DISPLAY_VIRTUAL) {
    return android::BAD_VALUE;
  }

  uint32_t count = 0;
  if (hwc_display_[dpy]) {
    error = hwc_display_[dpy]->GetDisplayConfigCount(&count);
    if (error == 0) {
      output_parcel->writeInt32(count);
    }
  }

  return error;
}

android::status_t HWCSession::HandleGetDisplayAttributesForConfig(const android::Parcel
                                                                  *input_parcel,
                                                                  android::Parcel *output_parcel) {
  int config = input_parcel->readInt32();
  int dpy = input_parcel->readInt32();
  int error = android::BAD_VALUE;
  DisplayConfigVariableInfo attributes;

  if (dpy > HWC_DISPLAY_VIRTUAL) {
    return android::BAD_VALUE;
  }

  if (hwc_display_[dpy]) {
    error = hwc_display_[dpy]->GetDisplayAttributesForConfig(config, &attributes);
    if (error == 0) {
      output_parcel->writeInt32(attributes.vsync_period_ns);
      output_parcel->writeInt32(attributes.x_pixels);
      output_parcel->writeInt32(attributes.y_pixels);
      output_parcel->writeFloat(attributes.x_dpi);
      output_parcel->writeFloat(attributes.y_dpi);
      output_parcel->writeInt32(0);  // Panel type, unsupported.
    }
  }

  return error;
}

android::status_t HWCSession::SetSecondaryDisplayStatus(const android::Parcel *input_parcel) {
  uint32_t display_id = UINT32(input_parcel->readInt32());
  uint32_t display_status = UINT32(input_parcel->readInt32());

  DLOGI("Display %d Status %d", display_id, display_status);
  if (display_id < HWC_DISPLAY_EXTERNAL || display_id > HWC_DISPLAY_VIRTUAL) {
    DLOGW("Not supported for display %d", display_id);
    return -EINVAL;
  }

  return hwc_display_[display_id]->SetDisplayStatus(display_status);
}

android::status_t HWCSession::ConfigureRefreshRate(const android::Parcel *input_parcel) {
  uint32_t operation = UINT32(input_parcel->readInt32());
  switch (operation) {
    case qdutils::DISABLE_METADATA_DYN_REFRESH_RATE:
      return hwc_display_[HWC_DISPLAY_PRIMARY]->Perform(
          HWCDisplayPrimary::SET_METADATA_DYN_REFRESH_RATE, false);
    case qdutils::ENABLE_METADATA_DYN_REFRESH_RATE:
      return hwc_display_[HWC_DISPLAY_PRIMARY]->Perform(
          HWCDisplayPrimary::SET_METADATA_DYN_REFRESH_RATE, true);
    case qdutils::SET_BINDER_DYN_REFRESH_RATE:
      {
        uint32_t refresh_rate = UINT32(input_parcel->readInt32());
        return hwc_display_[HWC_DISPLAY_PRIMARY]->Perform(
            HWCDisplayPrimary::SET_BINDER_DYN_REFRESH_RATE,
            refresh_rate);
      }
    default:
      DLOGW("Invalid operation %d", operation);
      return -EINVAL;
  }

  return 0;
}

android::status_t HWCSession::SetDisplayMode(const android::Parcel *input_parcel) {
  uint32_t mode = UINT32(input_parcel->readInt32());
  return hwc_display_[HWC_DISPLAY_PRIMARY]->Perform(HWCDisplayPrimary::SET_DISPLAY_MODE, mode);
}

android::status_t HWCSession::SetMaxMixerStages(const android::Parcel *input_parcel) {
  DisplayError error = kErrorNone;
  uint32_t bit_mask_display_type = UINT32(input_parcel->readInt32());
  uint32_t max_mixer_stages = UINT32(input_parcel->readInt32());

  if (IS_BIT_SET(bit_mask_display_type, HWC_DISPLAY_PRIMARY)) {
    if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
      error = hwc_display_[HWC_DISPLAY_PRIMARY]->SetMaxMixerStages(max_mixer_stages);
      if (error != kErrorNone) {
        return -EINVAL;
      }
    }
  }

  if (IS_BIT_SET(bit_mask_display_type, HWC_DISPLAY_EXTERNAL)) {
    if (hwc_display_[HWC_DISPLAY_EXTERNAL]) {
      error = hwc_display_[HWC_DISPLAY_EXTERNAL]->SetMaxMixerStages(max_mixer_stages);
      if (error != kErrorNone) {
        return -EINVAL;
      }
    }
  }

  if (IS_BIT_SET(bit_mask_display_type, HWC_DISPLAY_VIRTUAL)) {
    if (hwc_display_[HWC_DISPLAY_VIRTUAL]) {
      error = hwc_display_[HWC_DISPLAY_VIRTUAL]->SetMaxMixerStages(max_mixer_stages);
      if (error != kErrorNone) {
        return -EINVAL;
      }
    }
  }

  return 0;
}

void HWCSession::SetFrameDumpConfig(const android::Parcel *input_parcel) {
  uint32_t frame_dump_count = UINT32(input_parcel->readInt32());
  uint32_t bit_mask_display_type = UINT32(input_parcel->readInt32());
  uint32_t bit_mask_layer_type = UINT32(input_parcel->readInt32());

  if (IS_BIT_SET(bit_mask_display_type, HWC_DISPLAY_PRIMARY)) {
    if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
      hwc_display_[HWC_DISPLAY_PRIMARY]->SetFrameDumpConfig(frame_dump_count, bit_mask_layer_type);
    }
  }

  if (IS_BIT_SET(bit_mask_display_type, HWC_DISPLAY_EXTERNAL)) {
    if (hwc_display_[HWC_DISPLAY_EXTERNAL]) {
      hwc_display_[HWC_DISPLAY_EXTERNAL]->SetFrameDumpConfig(frame_dump_count, bit_mask_layer_type);
    }
  }

  if (IS_BIT_SET(bit_mask_display_type, HWC_DISPLAY_VIRTUAL)) {
    if (hwc_display_[HWC_DISPLAY_VIRTUAL]) {
      hwc_display_[HWC_DISPLAY_VIRTUAL]->SetFrameDumpConfig(frame_dump_count, bit_mask_layer_type);
    }
  }
}

void HWCSession::DynamicDebug(const android::Parcel *input_parcel) {
  int type = input_parcel->readInt32();
  bool enable = (input_parcel->readInt32() > 0);
  DLOGI("type = %d enable = %d", type, enable);
  int verbose_level = input_parcel->readInt32();

  switch (type) {
  case qService::IQService::DEBUG_ALL:
    HWCDebugHandler::DebugAll(enable, verbose_level);
    break;

  case qService::IQService::DEBUG_MDPCOMP:
    HWCDebugHandler::DebugStrategy(enable, verbose_level);
    HWCDebugHandler::DebugCompManager(enable, verbose_level);
    break;

  case qService::IQService::DEBUG_PIPE_LIFECYCLE:
    HWCDebugHandler::DebugResources(enable, verbose_level);
    break;

  case qService::IQService::DEBUG_DRIVER_CONFIG:
    HWCDebugHandler::DebugDriverConfig(enable, verbose_level);
    break;

  case qService::IQService::DEBUG_ROTATOR:
    HWCDebugHandler::DebugResources(enable, verbose_level);
    HWCDebugHandler::DebugDriverConfig(enable, verbose_level);
    HWCDebugHandler::DebugRotator(enable, verbose_level);
    break;

  case qService::IQService::DEBUG_QDCM:
    HWCDebugHandler::DebugQdcm(enable, verbose_level);
    break;

  default:
    DLOGW("type = %d is not supported", type);
  }
}

android::status_t HWCSession::QdcmCMDHandler(const android::Parcel *input_parcel,
                                             android::Parcel *output_parcel) {
  int ret = 0;
  int32_t *brightness_value = NULL;
  uint32_t display_id(0);
  PPPendingParams pending_action;
  PPDisplayAPIPayload resp_payload, req_payload;

  if (!color_mgr_) {
    return -1;
  }

  // Read display_id, payload_size and payload from in_parcel.
  ret = HWCColorManager::CreatePayloadFromParcel(*input_parcel, &display_id, &req_payload);
  if (!ret) {
    if (HWC_DISPLAY_PRIMARY == display_id && hwc_display_[HWC_DISPLAY_PRIMARY])
      ret = hwc_display_[HWC_DISPLAY_PRIMARY]->ColorSVCRequestRoute(req_payload,
                                                                  &resp_payload, &pending_action);

    if (HWC_DISPLAY_EXTERNAL == display_id && hwc_display_[HWC_DISPLAY_EXTERNAL])
      ret = hwc_display_[HWC_DISPLAY_EXTERNAL]->ColorSVCRequestRoute(req_payload, &resp_payload,
                                                                  &pending_action);
  }

  if (ret) {
    output_parcel->writeInt32(ret);  // first field in out parcel indicates return code.
    req_payload.DestroyPayload();
    resp_payload.DestroyPayload();
    return ret;
  }

  switch (pending_action.action) {
    case kInvalidating:
      hwc_procs_->invalidate(hwc_procs_);
      break;
    case kEnterQDCMMode:
      ret = color_mgr_->EnableQDCMMode(true, hwc_display_[HWC_DISPLAY_PRIMARY]);
      break;
    case kExitQDCMMode:
      ret = color_mgr_->EnableQDCMMode(false, hwc_display_[HWC_DISPLAY_PRIMARY]);
      break;
    case kApplySolidFill:
      ret = color_mgr_->SetSolidFill(pending_action.params,
                                     true, hwc_display_[HWC_DISPLAY_PRIMARY]);
      hwc_procs_->invalidate(hwc_procs_);
      break;
    case kDisableSolidFill:
      ret = color_mgr_->SetSolidFill(pending_action.params,
                                     false, hwc_display_[HWC_DISPLAY_PRIMARY]);
      hwc_procs_->invalidate(hwc_procs_);
      break;
    case kSetPanelBrightness:
      brightness_value = reinterpret_cast<int32_t*>(resp_payload.payload);
      if (brightness_value == NULL) {
        DLOGE("Brightness value is Null");
        return -EINVAL;
      }
      if (HWC_DISPLAY_PRIMARY == display_id)
        ret = hwc_display_[HWC_DISPLAY_PRIMARY]->SetPanelBrightness(*brightness_value);
      break;
    case kNoAction:
      break;
    default:
      DLOGW("Invalid pending action = %d!", pending_action.action);
      break;
  }

  // for display API getter case, marshall returned params into out_parcel.
  output_parcel->writeInt32(ret);
  HWCColorManager::MarshallStructIntoParcel(resp_payload, output_parcel);
  req_payload.DestroyPayload();
  resp_payload.DestroyPayload();

  return (ret? -EINVAL : 0);
}

android::status_t HWCSession::OnMinHdcpEncryptionLevelChange(const android::Parcel *input_parcel,
                                                             android::Parcel *output_parcel) {
  int ret = -EINVAL;
  uint32_t display_id = UINT32(input_parcel->readInt32());

  DLOGI("Display %d", display_id);
  if (display_id != HWC_DISPLAY_EXTERNAL) {
    DLOGW("Not supported for display %d", display_id);
  } else if (!hwc_display_[display_id]) {
    DLOGE("Display %d is not connected", display_id);
  } else {
    ret = hwc_display_[display_id]->OnMinHdcpEncryptionLevelChange();
  }

  output_parcel->writeInt32(ret);

  return ret;
}

void* HWCSession::HWCUeventThread(void *context) {
  if (context) {
    return reinterpret_cast<HWCSession *>(context)->HWCUeventThreadHandler();
  }

  return NULL;
}

void* HWCSession::HWCUeventThreadHandler() {
  static char uevent_data[PAGE_SIZE];
  int length = 0;
  prctl(PR_SET_NAME, uevent_thread_name_, 0, 0, 0);
  setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);
  if (!uevent_init()) {
    DLOGE("Failed to init uevent");
    pthread_exit(0);
    return NULL;
  }

  while (!uevent_thread_exit_) {
    // keep last 2 zeroes to ensure double 0 termination
    length = uevent_next_event(uevent_data, INT32(sizeof(uevent_data)) - 2);

    if (strcasestr(HWC_UEVENT_SWITCH_HDMI, uevent_data)) {
      DLOGI("Uevent HDMI = %s", uevent_data);
      int connected = GetEventValue(uevent_data, length, "SWITCH_STATE=");
      if (connected >= 0) {
        DLOGI("HDMI = %s", connected ? "connected" : "disconnected");
        if (HotPlugHandler(connected) == -1) {
          DLOGE("Failed handling Hotplug = %s", connected ? "connected" : "disconnected");
        }
      }
    } else if (strcasestr(HWC_UEVENT_GRAPHICS_FB0, uevent_data)) {
      DLOGI("Uevent FB0 = %s", uevent_data);
      int panel_reset = GetEventValue(uevent_data, length, "PANEL_ALIVE=");
      if (panel_reset == 0) {
        if (hwc_procs_) {
          reset_panel_ = true;
          hwc_procs_->invalidate(hwc_procs_);
        } else {
          DLOGW("Ignore resetpanel - hwc_proc not registered");
        }
      }
    }
  }
  pthread_exit(0);

  return NULL;
}

int HWCSession::GetEventValue(const char *uevent_data, int length, const char *event_info) {
  const char *iterator_str = uevent_data;
  while (((iterator_str - uevent_data) <= length) && (*iterator_str)) {
    char *pstr = strstr(iterator_str, event_info);
    if (pstr != NULL) {
      return (atoi(iterator_str + strlen(event_info)));
    }
    iterator_str += strlen(iterator_str) + 1;
  }

  return -1;
}

void HWCSession::ResetPanel() {
  int status = -EINVAL;

  DLOGI("Powering off primary");
  status = hwc_display_[HWC_DISPLAY_PRIMARY]->SetPowerMode(HWC_POWER_MODE_OFF);
  if (status) {
    DLOGE("power-off on primary failed with error = %d", status);
  }

  DLOGI("Restoring power mode on primary");
  uint32_t mode = hwc_display_[HWC_DISPLAY_PRIMARY]->GetLastPowerMode();
  status = hwc_display_[HWC_DISPLAY_PRIMARY]->SetPowerMode(mode);
  if (status) {
    DLOGE("Setting power mode = %d on primary failed with error = %d", mode, status);
  }

  status = hwc_display_[HWC_DISPLAY_PRIMARY]->EventControl(HWC_EVENT_VSYNC, 1);
  if (status) {
    DLOGE("enabling vsync failed for primary with error = %d", status);
  }

  reset_panel_ = false;
}

int HWCSession::HotPlugHandler(bool connected) {
  if (!hwc_procs_) {
     DLOGW("Ignore hotplug - hwc_proc not registered");
    return -1;
  }

  if (connected) {
    SEQUENCE_WAIT_SCOPE_LOCK(locker_);
    if (hwc_display_[HWC_DISPLAY_VIRTUAL]) {
      // Wait for the virtual display to tear down
      int status = locker_.WaitFinite(kExternalConnectionTimeoutMs);
      if (status != 0) {
        DLOGE("Timed out while waiting for virtual display to tear down.");
        return -1;
      }
    }
    if (hwc_display_[HWC_DISPLAY_EXTERNAL]) {
     DLOGE("HDMI already connected");
     return -1;
    }

    uint32_t primary_width = 0;
    uint32_t primary_height = 0;
    hwc_display_[HWC_DISPLAY_PRIMARY]->GetFrameBufferResolution(&primary_width, &primary_height);
    // Create hdmi display
    int status = HWCDisplayExternal::Create(core_intf_, &hwc_procs_, primary_width,
                                            primary_height, &hwc_display_[HWC_DISPLAY_EXTERNAL]);
    if (status) {
      return status;
    }
    if (hwc_display_[HWC_DISPLAY_EXTERNAL]) {
      hwc_display_[HWC_DISPLAY_EXTERNAL]->SetSecureDisplay(secure_display_active_);
    }
  } else {
    SEQUENCE_WAIT_SCOPE_LOCK(locker_);
    if (!hwc_display_[HWC_DISPLAY_EXTERNAL]) {
     DLOGE("HDMI not connected");
     return -1;
    }
    HWCDisplayExternal::Destroy(hwc_display_[HWC_DISPLAY_EXTERNAL]);
    hwc_display_[HWC_DISPLAY_EXTERNAL] = 0;
  }

  // notify client and trigger a screen refresh
  hwc_procs_->hotplug(hwc_procs_, HWC_DISPLAY_EXTERNAL, connected);
  hwc_procs_->invalidate(hwc_procs_);

  return 0;
}

void HWCSession::HandleSecureDisplaySession(hwc_display_contents_1_t **displays) {
  secure_display_active_ = false;
  if (!*displays) {
    DLOGW("Invalid display contents");
    return;
  }

  hwc_display_contents_1_t *content_list = displays[HWC_DISPLAY_PRIMARY];
  if (!content_list) {
    DLOGW("Invalid primary content list");
    return;
  }
  size_t num_hw_layers = content_list->numHwLayers;

  for (size_t i = 0; i < num_hw_layers - 1; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer.handle);
    if (pvt_handle && pvt_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_DISPLAY) {
      secure_display_active_ = true;
    }
  }

  for (ssize_t dpy = static_cast<ssize_t>(HWC_NUM_DISPLAY_TYPES - 1); dpy >= 0; dpy--) {
    if (hwc_display_[dpy]) {
      hwc_display_[dpy]->SetSecureDisplay(secure_display_active_);
    }
  }
}

}  // namespace sdm

