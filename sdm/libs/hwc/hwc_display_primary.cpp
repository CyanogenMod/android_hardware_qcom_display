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

#include <cutils/properties.h>
#include <utils/constants.h>
#include <stdarg.h>
#include <cutils/properties.h>
#include "hwc_display_primary.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCDisplayPrimary"

namespace sdm {

int HWCDisplayPrimary::Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                              HWCDisplay **hwc_display) {
  int status = 0;
  uint32_t primary_width = 0;
  uint32_t primary_height = 0;
  char property[PROPERTY_VALUE_MAX];

  HWCDisplay *hwc_display_primary = new HWCDisplayPrimary(core_intf, hwc_procs);
  status = hwc_display_primary->Init();
  if (status) {
    delete hwc_display_primary;
    return status;
  }

  status = hwc_display_primary->SetPowerMode(HWC_POWER_MODE_NORMAL);
  if (status) {
    Destroy(hwc_display_primary);
    return status;
  }

  hwc_display_primary->GetPanelResolution(&primary_width, &primary_height);
  int width = 0, height = 0;
  HWCDebugHandler::Get()->GetProperty("sdm.fb_size_width", &width);
  HWCDebugHandler::Get()->GetProperty("sdm.fb_size_height", &height);
  if (width > 0 && height > 0) {
    primary_width = width;
    primary_height = height;
  }

  status = hwc_display_primary->SetFrameBufferResolution(primary_width, primary_height);
  if (status) {
    Destroy(hwc_display_primary);
    return status;
  }

  *hwc_display = hwc_display_primary;

  return status;
}

void HWCDisplayPrimary::Destroy(HWCDisplay *hwc_display) {
  hwc_display->Deinit();
  delete hwc_display;
}

HWCDisplayPrimary::HWCDisplayPrimary(CoreInterface *core_intf, hwc_procs_t const **hwc_procs)
  : HWCDisplay(core_intf, hwc_procs, kPrimary, HWC_DISPLAY_PRIMARY, true) {
}

void HWCDisplayPrimary::ProcessBootAnimCompleted() {
  char value[PROPERTY_VALUE_MAX];

  // Applying default mode after bootanimation is finished
  property_get("init.svc.bootanim", value, "running");
  if (!strncmp(value,"stopped",strlen("stopped"))) {
    boot_animation_completed_ = true;

    // one-shot action check if bootanimation completed then apply default display mode.
    if (display_intf_)
      display_intf_->ApplyDefaultDisplayMode();
  }
}

int HWCDisplayPrimary::Prepare(hwc_display_contents_1_t *content_list) {
  int status = 0;

  if (!boot_animation_completed_)
    ProcessBootAnimCompleted();

  status = AllocateLayerStack(content_list);
  if (status) {
    return status;
  }

  status = PrepareLayerStack(content_list);
  if (status) {
    return status;
  }

  if (use_metadata_refresh_rate_) {
    SetRefreshRate(metadata_refresh_rate_);
  }

  return 0;
}

int HWCDisplayPrimary::Commit(hwc_display_contents_1_t *content_list) {
  int status = 0;

  status = HWCDisplay::CommitLayerStack(content_list);
  if (status) {
    return status;
  }

  status = HWCDisplay::PostCommitLayerStack(content_list);
  if (status) {
    return status;
  }

  return 0;
}

int HWCDisplayPrimary::SetActiveConfig(uint32_t index) {
  DisplayError error = kErrorNone;

  if (display_intf_) {
    error = display_intf_->SetActiveConfig(index);
  }

  return error;
}

int HWCDisplayPrimary::SetRefreshRate(uint32_t refresh_rate) {
  DisplayError error = kErrorNone;

  if (display_intf_) {
    error = display_intf_->SetRefreshRate(refresh_rate);
  }

  return error;
}

int HWCDisplayPrimary::Perform(uint32_t operation, ...) {
  va_list args;
  va_start(args, operation);
  int val = va_arg(args, uint32_t);
  va_end(args);
  switch (operation) {
    case SET_METADATA_DYN_REFRESH_RATE:
      SetMetaDataRefreshRateFlag(val);
      break;
    case SET_BINDER_DYN_REFRESH_RATE:
      SetRefreshRate(val);
      break;
    case SET_DISPLAY_MODE:
      SetDisplayMode(val);
      break;
    default:
      DLOGW("Invalid operation %d", operation);
      return -EINVAL;
  }

  return 0;
}

DisplayError HWCDisplayPrimary::SetDisplayMode(uint32_t mode) {
  DisplayError error = kErrorNone;

  if (display_intf_) {
    error = display_intf_->SetDisplayMode(mode);
  }

  return error;
}

void HWCDisplayPrimary::SetMetaDataRefreshRateFlag(bool enable) {
  use_metadata_refresh_rate_ = enable;
}

}  // namespace sdm

