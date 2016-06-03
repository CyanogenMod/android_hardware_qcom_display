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

#include <stdlib.h>
#include <utils/debug.h>
#include <utils/constants.h>

namespace sdm {

Debug Debug::debug_;

Debug::Debug() : debug_handler_(&default_debug_handler_) {
}

int Debug::GetSimulationFlag() {
  int value = 0;
  debug_.debug_handler_->GetProperty("sdm.composition_simulation", &value);

  return value;
}

int Debug::GetHDMIResolution() {
  int value = 0;
  debug_.debug_handler_->GetProperty("hw.hdmi.resolution", &value);

  return value;
}

uint32_t Debug::GetIdleTimeoutMs() {
  int value = IDLE_TIMEOUT_DEFAULT_MS;
  debug_.debug_handler_->GetProperty("sdm.idle_time", &value);

  return UINT32(value);
}

int Debug::GetBootAnimLayerCount() {
  int value = 0;
  debug_.debug_handler_->GetProperty("sdm.boot_anim_layer_count", &value);

  return value;
}

bool Debug::IsRotatorDownScaleDisabled() {
  int value = 0;
  debug_.debug_handler_->GetProperty("sdm.debug.rotator_downscale", &value);

  return (value == 1);
}

bool Debug::IsDecimationDisabled() {
  int value = 0;
  debug_.debug_handler_->GetProperty("sdm.disable_decimation", &value);

  return (value == 1);
}

int Debug::GetMaxPipesPerMixer(DisplayType display_type) {
  int value = -1;
  switch (display_type) {
  case kPrimary:
    debug_.debug_handler_->GetProperty("sdm.primary.mixer_stages", &value);
    break;
  case kHDMI:
    debug_.debug_handler_->GetProperty("sdm.external.mixer_stages", &value);
    break;
  case kVirtual:
    debug_.debug_handler_->GetProperty("sdm.virtual.mixer_stages", &value);
    break;
  default:
    break;
  }

  return value;
}

bool Debug::IsVideoModeEnabled() {
  int value = 0;
  debug_.debug_handler_->GetProperty("sdm.video_mode_panel", &value);

  return (value == 1);
}

bool Debug::IsRotatorUbwcDisabled() {
  int value = 0;
  debug_.debug_handler_->GetProperty("sdm.debug.rotator_disable_ubwc", &value);

  return (value == 1);
}

bool Debug::IsRotatorSplitDisabled() {
  int value = 0;
  debug_.debug_handler_->GetProperty("sdm.debug.disable_rotator_split", &value);

  return (value == 1);
}

bool Debug::IsScalarDisabled() {
  int value = 0;
  debug_.debug_handler_->GetProperty("sdm.debug.disable_scalar", &value);

  return (value == 1);
}

bool Debug::IsUbwcTiledFrameBuffer() {
  int ubwc_disabled = 0;
  int ubwc_framebuffer = 0;

  debug_.debug_handler_->GetProperty("debug.gralloc.gfx_ubwc_disable", &ubwc_disabled);

  if (!ubwc_disabled) {
    debug_.debug_handler_->GetProperty("debug.gralloc.enable_fb_ubwc", &ubwc_framebuffer);
  }

  return (ubwc_framebuffer == 1);
}

bool Debug::GetProperty(const char* property_name, char* value) {
  if (debug_.debug_handler_->GetProperty(property_name, value) != kErrorNone) {
    return false;
  }

  return true;
}

bool Debug::SetProperty(const char* property_name, const char* value) {
  if (debug_.debug_handler_->SetProperty(property_name, value) != kErrorNone) {
    return false;
  }

  return true;
}

}  // namespace sdm

