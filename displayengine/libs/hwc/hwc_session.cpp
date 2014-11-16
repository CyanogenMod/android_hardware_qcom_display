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

#include <core/dump_interface.h>
#include <utils/constants.h>

// HWC_MODULE_NAME definition must precede hwc_logger.h include.
#define HWC_MODULE_NAME "HWCSession"
#include "hwc_logger.h"

#include "hwc_session.h"

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

HWCSession::HWCSession(const hw_module_t *module) : core_intf_(NULL), hwc_procs_(NULL) {
  hwc_composer_device_1_t::common.tag = HARDWARE_DEVICE_TAG;
  hwc_composer_device_1_t::common.version = HWC_DEVICE_API_VERSION_1_3;
  hwc_composer_device_1_t::common.module = const_cast<hw_module_t*>(module);
  hwc_composer_device_1_t::common.close = Close;
  hwc_composer_device_1_t::prepare = Prepare;
  hwc_composer_device_1_t::set = Set;
  hwc_composer_device_1_t::eventControl = EventControl;
  hwc_composer_device_1_t::blank = Blank;
  hwc_composer_device_1_t::query = Query;
  hwc_composer_device_1_t::registerProcs = RegisterProcs;
  hwc_composer_device_1_t::dump = Dump;
  hwc_composer_device_1_t::getDisplayConfigs = GetDisplayConfigs;
  hwc_composer_device_1_t::getDisplayAttributes = GetDisplayAttributes;
}

int HWCSession::Init() {
  DisplayError error = CoreInterface::CreateCore(this, &core_intf_);
  if (UNLIKELY(error != kErrorNone)) {
    DLOGE("Display core initialization failed. Error = %d", error);
    return -EINVAL;
  }

  int status = -EINVAL;

  // Create and power on primary display
  display_primary_ = new HWCDisplayPrimary(core_intf_, &hwc_procs_);
  if (UNLIKELY(!display_primary_)) {
    CoreInterface::DestroyCore();
    return -ENOMEM;
  }

  status = display_primary_->Init();
  if (UNLIKELY(status)) {
    CoreInterface::DestroyCore();
    delete display_primary_;
    return status;
  }

  status = display_primary_->PowerOn();
  if (UNLIKELY(status)) {
    CoreInterface::DestroyCore();
    display_primary_->Deinit();
    delete display_primary_;
    return status;
  }

  return 0;
}

int HWCSession::Deinit() {
  display_primary_->PowerOff();
  display_primary_->Deinit();
  delete display_primary_;

  DisplayError error = CoreInterface::DestroyCore();
  if (error != kErrorNone) {
    DLOGE("Display core de-initialization failed. Error = %d", error);
  }

  return 0;
}

int HWCSession::Open(const hw_module_t *module, const char *name, hw_device_t **device) {
  if (UNLIKELY(!module || !name || !device)) {
    DLOGE("::%s Invalid parameters.", __FUNCTION__);
    return -EINVAL;
  }

  if (LIKELY(!strcmp(name, HWC_HARDWARE_COMPOSER))) {
    HWCSession *hwc_session = new HWCSession(module);
    if (UNLIKELY(!hwc_session)) {
      return -ENOMEM;
    }

    int status = hwc_session->Init();
    if (UNLIKELY(status != 0)) {
      delete hwc_session;
      return status;
    }

    hwc_composer_device_1_t *composer_device = hwc_session;
    *device = reinterpret_cast<hw_device_t *>(composer_device);
  }

  return 0;
}

int HWCSession::Close(hw_device_t *device) {
  if (UNLIKELY(!device)) {
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
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!device || !displays)) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  for (size_t i = 0; i < num_displays; i++) {
    hwc_display_contents_1_t *content_list = displays[i];
    if (UNLIKELY(!content_list || !content_list->numHwLayers)) {
      DLOGE("::%s Invalid content list.", __FUNCTION__);
      return -EINVAL;
    }

    switch (i) {
    case HWC_DISPLAY_PRIMARY:
      status = hwc_session->display_primary_->Prepare(content_list);
      break;
    default:
      status = -EINVAL;
    }

    if (UNLIKELY(!status)) {
      break;
    }
  }

  return status;
}

int HWCSession::Set(hwc_composer_device_1 *device, size_t num_displays,
                    hwc_display_contents_1_t **displays) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!device || !displays)) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  for (size_t i = 0; i < num_displays; i++) {
    hwc_display_contents_1_t *content_list = displays[i];
    if (UNLIKELY(!content_list || !content_list->numHwLayers)) {
      DLOGE("::%s Invalid content list.", __FUNCTION__);
      return -EINVAL;
    }

    switch (i) {
    case HWC_DISPLAY_PRIMARY:
      status = hwc_session->display_primary_->Commit(content_list);
      break;
    default:
      status = -EINVAL;
    }

    if (UNLIKELY(!status)) {
      break;
    }
  }

  return status;
}

int HWCSession::EventControl(hwc_composer_device_1 *device, int disp, int event, int enable) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!device)) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  switch (disp) {
  case HWC_DISPLAY_PRIMARY:
    status = hwc_session->display_primary_->EventControl(event, enable);
    break;
  default:
    status = -EINVAL;
  }

  return 0;
}

int HWCSession::Blank(hwc_composer_device_1 *device, int disp, int blank) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!device)) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  switch (disp) {
  case HWC_DISPLAY_PRIMARY:
    status = hwc_session->display_primary_->Blank(blank);
    break;
  default:
    status = -EINVAL;
  }

  return status;
}

int HWCSession::Query(hwc_composer_device_1 *device, int param, int *value) {
  if (UNLIKELY(!device || !value)) {
    return -EINVAL;
  }

  return -EINVAL;
}

void HWCSession::RegisterProcs(hwc_composer_device_1 *device, hwc_procs_t const *procs) {
  if (UNLIKELY(!device || !procs)) {
    return;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  hwc_session->hwc_procs_ = procs;
}

void HWCSession::Dump(hwc_composer_device_1 *device, char *buffer, int length) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!device || !buffer || !length)) {
    return;
  }

  DumpInterface::GetDump(buffer, length);
}

int HWCSession::GetDisplayConfigs(hwc_composer_device_1 *device, int disp, uint32_t *configs,
                                  size_t *num_configs) {
  if (UNLIKELY(!device || !configs || !num_configs)) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  switch (disp) {
  case HWC_DISPLAY_PRIMARY:
    status = hwc_session->display_primary_->GetDisplayConfigs(configs, num_configs);
    break;
  default:
    status = -EINVAL;
  }

  return status;
}

int HWCSession::GetDisplayAttributes(hwc_composer_device_1 *device, int disp, uint32_t config,
                                     const uint32_t *attributes, int32_t *values) {
  if (UNLIKELY(!device || !attributes || !values)) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  switch (disp) {
  case HWC_DISPLAY_PRIMARY:
    status = hwc_session->display_primary_->GetDisplayAttributes(config, attributes, values);
    break;
  default:
    status = -EINVAL;
  }

  return status;
}

DisplayError HWCSession::Hotplug(const CoreEventHotplug &hotplug) {
  return kErrorNone;
}

}  // namespace sde

