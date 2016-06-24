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

int HWCDisplayExternal::Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                               qService::QService *qservice, HWCDisplay **hwc_display) {
  return Create(core_intf, hwc_procs, 0, 0, qservice, false, hwc_display);
}

int HWCDisplayExternal::Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                               uint32_t primary_width, uint32_t primary_height,
                               qService::QService *qservice, bool use_primary_res,
                               HWCDisplay **hwc_display) {
  uint32_t external_width = 0;
  uint32_t external_height = 0;
  int drc_enabled = 0;
  DisplayError error = kErrorNone;

  HWCDisplay *hwc_display_external = new HWCDisplayExternal(core_intf, hwc_procs, qservice);
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

  HWCDebugHandler::Get()->GetProperty("sdm.hdmi.drc_enabled", &(drc_enabled));
  reinterpret_cast<HWCDisplayExternal *>(hwc_display_external)->drc_enabled_ = drc_enabled;

  *hwc_display = hwc_display_external;

  return status;
}

void HWCDisplayExternal::Destroy(HWCDisplay *hwc_display) {
  hwc_display->Deinit();
  delete hwc_display;
}

HWCDisplayExternal::HWCDisplayExternal(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                                       qService::QService *qservice)
  : HWCDisplay(core_intf, hwc_procs, kHDMI, HWC_DISPLAY_EXTERNAL, false, qservice,
               DISPLAY_CLASS_EXTERNAL) {
}

int HWCDisplayExternal::Prepare(hwc_display_contents_1_t *content_list) {
  int status = 0;
  DisplayError error = kErrorNone;

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

  bool one_video_updating_layer = SingleVideoLayerUpdating(UINT32(content_list->numHwLayers - 1));

  if (current_refresh_rate_ != metadata_refresh_rate_ && one_video_updating_layer && drc_enabled_) {
    error = display_intf_->SetRefreshRate(metadata_refresh_rate_);
  }

  if (error == kErrorNone) {
    // On success, set current refresh rate to new refresh rate
    current_refresh_rate_ = metadata_refresh_rate_;
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
                                        uint32_t *non_primary_width, uint32_t *non_primary_height) {
  uint32_t primary_area = primary_width * primary_height;
  uint32_t non_primary_area = (*non_primary_width) * (*non_primary_height);

  if (primary_area > non_primary_area) {
    if (primary_height > primary_width) {
      std::swap(primary_height, primary_width);
    }
    AdjustSourceResolution(primary_width, primary_height, non_primary_width, non_primary_height);
  }
}

uint32_t HWCDisplayExternal::RoundToStandardFPS(float fps) {
  static const uint32_t standard_fps[] = {23976, 24000, 25000, 29970, 30000, 50000, 59940, 60000};
  static const uint32_t mapping_fps[] = {59940, 60000, 60000, 59940, 60000, 50000, 59940, 60000};
  uint32_t frame_rate = (uint32_t)(fps * 1000);

  int count = INT(sizeof(standard_fps) / sizeof(standard_fps[0]));
  for (int i = 0; i < count; i++) {
    // Most likely used for video, the fps for frames should be stable from video side.
    if (standard_fps[i] > frame_rate) {
      if (i > 0) {
        if ((standard_fps[i] - frame_rate) > (frame_rate - standard_fps[i-1])) {
          return mapping_fps[i-1];
        } else {
          return mapping_fps[i];
        }
      } else {
        return mapping_fps[i];
      }
    }
  }

  return standard_fps[count - 1];
}

void HWCDisplayExternal::PrepareDynamicRefreshRate(Layer *layer) {
  if (layer->input_buffer->flags.video) {
    metadata_refresh_rate_ = SanitizeRefreshRate(layer->frame_rate);
    layer->frame_rate = current_refresh_rate_;
  }
}

}  // namespace sdm

