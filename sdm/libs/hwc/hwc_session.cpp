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

namespace sdm {

Locker HWCSession::locker_;
bool HWCSession::reset_panel_ = false;

HWCSession::HWCSession(const hw_module_t *module) : core_intf_(NULL), hwc_procs_(NULL),
            uevent_thread_exit_(false), uevent_thread_name_("HWC_UeventThread") {
  memset(&hwc_display_, 0, sizeof(hwc_display_));
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
    DLOGE("Failed to start = %s, error = %s", uevent_thread_name_);
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

  for (ssize_t dpy = (num_displays - 1); dpy >= 0; dpy--) {
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
    return -1;
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

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;
  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->SetActiveConfig(index);
  }

  return status;
}

int HWCSession::HandleVirtualDisplayLifeCycle(hwc_display_contents_1_t *content_list) {
  int status = 0;

  if (HWCDisplayVirtual::IsValidContentList(content_list)) {
    if (!hwc_display_[HWC_DISPLAY_VIRTUAL]) {
      // Create virtual display device
      status = HWCDisplayVirtual::Create(core_intf_, &hwc_procs_, content_list,
                                         &hwc_display_[HWC_DISPLAY_VIRTUAL]);
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

  case qService::IQService::QDCM_SVC_CMDS:
    status = QdcmCMDHandler(*input_parcel, output_parcel);
    break;

  default:
    DLOGW("QService command = %d is not supported", command);
    return -EINVAL;
  }

  return status;
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
    HWCDebugHandler::DebugRotator(enable);
    break;

  case qService::IQService::DEBUG_QDCM:
    HWCDebugHandler::DebugQdcm(enable);
    break;

  default:
    DLOGW("type = %d is not supported", type);
  }
}

android::status_t HWCSession::QdcmCMDHandler(const android::Parcel &in, android::Parcel *out) {
  int ret = 0;
  uint32_t display_id(0);
  PPPendingParams pending_action;
  PPDisplayAPIPayload resp_payload, req_payload;

  // Read display_id, payload_size and payload from in_parcel.
  ret = HWCColorManager::CreatePayloadFromParcel(in, &display_id, &req_payload);
  if (!ret) {
    if (HWC_DISPLAY_PRIMARY == display_id && hwc_display_[HWC_DISPLAY_PRIMARY])
      ret = hwc_display_[HWC_DISPLAY_PRIMARY]->ColorSVCRequestRoute(req_payload,
                                                                  &resp_payload, &pending_action);

    if (HWC_DISPLAY_EXTERNAL == display_id && hwc_display_[HWC_DISPLAY_EXTERNAL])
      ret = hwc_display_[HWC_DISPLAY_EXTERNAL]->ColorSVCRequestRoute(req_payload, &resp_payload,
                                                                  &pending_action);
  }

  if (ret) {
    out->writeInt32(ret);  // first field in out parcel indicates return code.
    req_payload.DestroyPayload();
    resp_payload.DestroyPayload();
    return ret;
  }

  switch (pending_action.action) {
    case kInvalidating:
      hwc_procs_->invalidate(hwc_procs_);
      break;
    case kEnterQDCMMode:
      ret = color_mgr_->EnableQDCMMode(true);
      break;
    case kExitQDCMMode:
      ret = color_mgr_->EnableQDCMMode(false);
      break;
    case kApplySolidFill:
    case kDisableSolidFill:
      break;
    case kNoAction:
      break;
    default:
      DLOGW("Invalid pending action = %d!", pending_action.action);
      break;
  }

  // for display API getter case, marshall returned params into out_parcel.
  out->writeInt32(ret);
  HWCColorManager::MarshallStructIntoParcel(resp_payload, *out);
  req_payload.DestroyPayload();
  resp_payload.DestroyPayload();

  return (ret? -EINVAL : 0);
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

}  // namespace sdm

