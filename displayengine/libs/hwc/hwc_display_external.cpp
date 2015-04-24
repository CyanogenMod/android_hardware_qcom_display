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

#include "hwc_display_external.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCDisplayExternal"

namespace sde {

HWCDisplayExternal::HWCDisplayExternal(CoreInterface *core_intf, hwc_procs_t const **hwc_procs)
  : HWCDisplay(core_intf, hwc_procs, kHDMI, HWC_DISPLAY_EXTERNAL) {
}

int HWCDisplayExternal::Init() {
  int status = 0;

  status = HWCDisplay::Init();
  if (status != 0) {
    return status;
  }

  return status;
}

int HWCDisplayExternal::Prepare(hwc_display_contents_1_t *content_list) {
  int status = 0;

  status = AllocateLayerStack(content_list);
  if (status) {
    return status;
  }

  status = PrepareLayerStack(content_list);
  if (status) {
    return status;
  }

  return 0;
}

int HWCDisplayExternal::Commit(hwc_display_contents_1_t *content_list) {
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

int HWCDisplayExternal::GetDisplayConfigs(uint32_t *configs, size_t *num_configs) {
  uint32_t config_count = 0;
  if (*num_configs <= 0) {
    return -EINVAL;
  }

  display_intf_->GetNumVariableInfoConfigs(&config_count);
  *num_configs = static_cast<size_t>(config_count);
  if (*num_configs <= 0) {
    return -EINVAL;
  }

  for (uint32_t i = 0; i < config_count; i++) {
    configs[i] = i;
  }

  return 0;
}

void HWCDisplayExternal::ApplyScanAdjustment(hwc_rect_t *display_frame) {
  if (display_intf_->IsUnderscanSupported()) {
    return;
  }

  // Read user defined width and height ratio
  char property[PROPERTY_VALUE_MAX];
  property_get("persist.sys.actionsafe.width", property, "0");
  float width_ratio = FLOAT(atoi(property)) / 100.0f;
  property_get("persist.sys.actionsafe.height", property, "0");
  float height_ratio = FLOAT(atoi(property)) / 100.0f;

  if (width_ratio == 0.0f ||  height_ratio == 0.0f) {
    return;
  }

  uint32_t panel_width = 0;
  uint32_t panel_height = 0;
  GetPanelResolution(&panel_width, &panel_height);

  if (panel_width == 0 || panel_height == 0) {
    DLOGV("Invalid panel dimensions (%d, %d)", panel_width, panel_height);
    return;
  }

  int x_offset = INT((FLOAT(panel_width) * width_ratio) / 2.0f);
  int y_offset = INT((FLOAT(panel_height) * height_ratio) / 2.0f);

  display_frame->left = display_frame->left + x_offset;
  display_frame->top = display_frame->top + y_offset;
  display_frame->right = display_frame->right - x_offset;
  display_frame->bottom = display_frame->bottom - y_offset;
}

}  // namespace sde

