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
#include <algorithm>

#include "hwc_display_external.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCDisplayExternal"

namespace sdm {

int HWCDisplayExternal::Create(CoreInterface *core_intf, HWCCallbacks *callbacks,
                               qService::QService *qservice, HWCDisplay **hwc_display) {
  return Create(core_intf, callbacks, 0, 0, qservice, false, hwc_display);
}

int HWCDisplayExternal::Create(CoreInterface *core_intf, HWCCallbacks *callbacks,
                               uint32_t primary_width, uint32_t primary_height,
                               qService::QService *qservice, bool use_primary_res,
                               HWCDisplay **hwc_display) {
  uint32_t external_width = 0;
  uint32_t external_height = 0;
  DisplayError error = kErrorNone;

  HWCDisplay *hwc_display_external = new HWCDisplayExternal(core_intf, callbacks, qservice);
  int status = hwc_display_external->Init();
  if (status) {
    delete hwc_display_external;
    return status;
  }

  error = hwc_display_external->GetMixerResolution(&external_width, &external_height);
  if (error != kErrorNone) {
    return -EINVAL;
  }

  if (primary_width && primary_height) {
    // use_primary_res means HWCDisplayExternal should directly set framebuffer resolution to the
    // provided primary_width and primary_height
    if (use_primary_res) {
      external_width = primary_width;
      external_height = primary_height;
    } else {
      int downscale_enabled = 0;
      HWCDebugHandler::Get()->GetProperty("sdm.debug.downscale_external", &downscale_enabled);
      if (downscale_enabled) {
        GetDownscaleResolution(primary_width, primary_height, &external_width, &external_height);
      }
    }
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

HWCDisplayExternal::HWCDisplayExternal(CoreInterface *core_intf, HWCCallbacks *callbacks,
                                       qService::QService *qservice)
    : HWCDisplay(core_intf, callbacks, kHDMI, HWC_DISPLAY_EXTERNAL, false, qservice,
                 DISPLAY_CLASS_EXTERNAL) {
}

HWC2::Error HWCDisplayExternal::Validate(uint32_t *out_num_types, uint32_t *out_num_requests) {
  auto status = HWC2::Error::None;

  if (secure_display_active_) {
    MarkLayersForGPUBypass();
    return status;
  }

  BuildLayerStack();

  if (layer_set_.empty()) {
    flush_ = true;
    return status;
  }

  status = PrepareLayerStack(out_num_types, out_num_requests);
  return status;
}

HWC2::Error HWCDisplayExternal::Present(int32_t *out_retire_fence) {
  auto status = HWC2::Error::None;

  if (!secure_display_active_) {
    status = HWCDisplay::CommitLayerStack();
    if (status == HWC2::Error::None) {
      status = HWCDisplay::PostCommitLayerStack(out_retire_fence);
    }
  }
  CloseAcquireFds();
  return status;
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

  if (width_ratio == 0.0f || height_ratio == 0.0f) {
    return;
  }

  uint32_t mixer_width = 0;
  uint32_t mixer_height = 0;
  GetMixerResolution(&mixer_width, &mixer_height);

  if (mixer_width == 0 || mixer_height == 0) {
    DLOGV("Invalid mixer dimensions (%d, %d)", mixer_width, mixer_height);
    return;
  }

  uint32_t new_mixer_width = UINT32(mixer_width * FLOAT(1.0f - width_ratio));
  uint32_t new_mixer_height = UINT32(mixer_height * FLOAT(1.0f - height_ratio));

  int x_offset = INT((FLOAT(mixer_width) * width_ratio) / 2.0f);
  int y_offset = INT((FLOAT(mixer_height) * height_ratio) / 2.0f);

  display_frame->left = (display_frame->left * INT32(new_mixer_width) / INT32(mixer_width))
                        + x_offset;
  display_frame->top = (display_frame->top * INT32(new_mixer_height) / INT32(mixer_height)) +
                       y_offset;
  display_frame->right = ((display_frame->right * INT32(new_mixer_width)) / INT32(mixer_width)) +
                         x_offset;
  display_frame->bottom = ((display_frame->bottom * INT32(new_mixer_height)) / INT32(mixer_height))
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
                                                uint32_t *non_primary_width,
                                                uint32_t *non_primary_height) {
  uint32_t primary_area = primary_width * primary_height;
  uint32_t non_primary_area = (*non_primary_width) * (*non_primary_height);

  if (primary_area > non_primary_area) {
    if (primary_height > primary_width) {
      std::swap(primary_height, primary_width);
    }
    AdjustSourceResolution(primary_width, primary_height, non_primary_width, non_primary_height);
  }
}

}  // namespace sdm
