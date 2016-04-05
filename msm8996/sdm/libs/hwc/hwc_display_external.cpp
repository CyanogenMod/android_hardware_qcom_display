/*
* Copyright (c) 2014 - 2016, The Linux Foundation. All rights reserved.
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
#include <utils/debug.h>

#include "hwc_display_external.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCDisplayExternal"

namespace sdm {

int HWCDisplayExternal::Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                               uint32_t primary_width, uint32_t primary_height,
                               HWCDisplay **hwc_display) {
  uint32_t external_width = 0;
  uint32_t external_height = 0;

  HWCDisplay *hwc_display_external = new HWCDisplayExternal(core_intf, hwc_procs);
  int status = hwc_display_external->Init();
  if (status) {
    delete hwc_display_external;
    return status;
  }

  hwc_display_external->GetPanelResolution(&external_width, &external_height);

  int downscale_enabled = 0;
  HWCDebugHandler::Get()->GetProperty("sdm.debug.downscale_external", &downscale_enabled);
  if (downscale_enabled) {
    GetDownscaleResolution(primary_width, primary_height, &external_width, &external_height);
  }

  status = hwc_display_external->SetFrameBufferResolution(external_width, external_height);
  if (status) {
    Destroy(hwc_display_external);
    return status;
  }

  *hwc_display = hwc_display_external;

  return status;
}

void HWCDisplayExternal::Destroy(HWCDisplay *hwc_display) {
  hwc_display->Deinit();
  delete hwc_display;
}

HWCDisplayExternal::HWCDisplayExternal(CoreInterface *core_intf, hwc_procs_t const **hwc_procs)
  : HWCDisplay(core_intf, hwc_procs, kHDMI, HWC_DISPLAY_EXTERNAL, false) {
}

int HWCDisplayExternal::Prepare(hwc_display_contents_1_t *content_list) {
  int status = 0;

  if (secure_display_active_) {
    MarkLayersForGPUBypass(content_list);
    return status;
  }

  status = AllocateLayerStack(content_list);
  if (status) {
    return status;
  }

  status = PrePrepareLayerStack(content_list);
  if (status) {
    return status;
  }

  if (content_list->numHwLayers <= 1) {
    flush_ = true;
    return 0;
  }

  status = PrepareLayerStack(content_list);
  if (status) {
    return status;
  }

  return 0;
}

int HWCDisplayExternal::Commit(hwc_display_contents_1_t *content_list) {
  int status = 0;

  if (secure_display_active_) {
    return status;
  }

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

void HWCDisplayExternal::ApplyScanAdjustment(hwc_rect_t *display_frame) {
  if (display_intf_->IsUnderscanSupported()) {
    return;
  }

  // Read user defined width and height ratio
  int width = 0, height = 0;
  HWCDebugHandler::Get()->GetProperty("sdm.external_action_safe_width", &width);
  float width_ratio = FLOAT(width) / 100.0f;
  HWCDebugHandler::Get()->GetProperty("sdm.external_action_safe_height", &height);
  float height_ratio = FLOAT(height) / 100.0f;

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

  uint32_t new_panel_width = panel_width * UINT32(1.0f - width_ratio);
  uint32_t new_panel_height = panel_height * UINT32(1.0f - height_ratio);

  int x_offset = INT((FLOAT(panel_width) * width_ratio) / 2.0f);
  int y_offset = INT((FLOAT(panel_height) * height_ratio) / 2.0f);

  display_frame->left = (display_frame->left * INT32(new_panel_width) / INT32(panel_width))
                        + x_offset;
  display_frame->top = (display_frame->top * INT32(new_panel_height) / INT32(panel_height)) +
                       y_offset;
  display_frame->right = ((display_frame->right * INT32(new_panel_width)) / INT32(panel_width)) +
                         x_offset;
  display_frame->bottom = ((display_frame->bottom * INT32(new_panel_height)) / INT32(panel_height))
                          + y_offset;
}

void HWCDisplayExternal::SetSecureDisplay(bool secure_display_active) {
  if (secure_display_active_ != secure_display_active) {
    secure_display_active_ = secure_display_active;

    if (secure_display_active_) {
      DisplayError error = display_intf_->Flush();
      if (error != kErrorNone) {
        DLOGE("Flush failed. Error = %d", error);
      }
    }
  }
  return;
}

static void AdjustSourceResolution(uint32_t dst_width, uint32_t dst_height, uint32_t *src_width,
                                   uint32_t *src_height) {
  *src_height = (dst_width * (*src_height)) / (*src_width);
  *src_width = dst_width;
}

void HWCDisplayExternal::GetDownscaleResolution(uint32_t primary_width, uint32_t primary_height,
                                        uint32_t *non_primary_width, uint32_t *non_primary_height) {
  uint32_t primary_area = primary_width * primary_height;
  uint32_t non_primary_area = (*non_primary_width) * (*non_primary_height);

  if (primary_area > non_primary_area) {
    if (primary_height > primary_width) {
      Swap(primary_height, primary_width);
    }
    AdjustSourceResolution(primary_width, primary_height, non_primary_width, non_primary_height);
  }
}

}  // namespace sdm

