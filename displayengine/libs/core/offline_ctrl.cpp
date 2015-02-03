/*
* Copyright (c) 2014 - 2015, The Linux Foundation. All rights reserved.
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

#include <utils/constants.h>
#include <utils/debug.h>

#include "offline_ctrl.h"

#define __CLASS__ "OfflineCtrl"

namespace sde {

// TODO(user): Move this offline controller under composition manager like other modules
// [resource manager]. Implement session management and buffer management in offline controller.
OfflineCtrl::OfflineCtrl() : hw_intf_(NULL), hw_rotator_device_(NULL) {
}

DisplayError OfflineCtrl::Init(HWInterface *hw_intf, HWResourceInfo hw_res_info) {
  hw_intf_ = hw_intf;
  DisplayError error = kErrorNone;

  error = hw_intf_->Open(kDeviceRotator, &hw_rotator_device_, NULL);
  if (error != kErrorNone) {
    DLOGW("Failed to open rotator device");
  }

  return kErrorNone;
}

DisplayError OfflineCtrl::Deinit() {
  DisplayError error = kErrorNone;

  error = hw_intf_->Close(hw_rotator_device_);
  if (error != kErrorNone) {
    DLOGW("Failed to close rotator device");
    return error;
  }

  return kErrorNone;
}

DisplayError OfflineCtrl::RegisterDisplay(DisplayType type, Handle *display_ctx) {
  DisplayOfflineContext *disp_offline_ctx = new DisplayOfflineContext();
  if (disp_offline_ctx == NULL) {
    return kErrorMemory;
  }

  disp_offline_ctx->display_type = type;
  *display_ctx = disp_offline_ctx;

  return kErrorNone;
}

void OfflineCtrl::UnregisterDisplay(Handle display_ctx) {
  DisplayOfflineContext *disp_offline_ctx = reinterpret_cast<DisplayOfflineContext *>(display_ctx);

  delete disp_offline_ctx;
  disp_offline_ctx = NULL;
}


DisplayError OfflineCtrl::Prepare(Handle display_ctx, HWLayers *hw_layers) {
  DisplayError error = kErrorNone;

  DisplayOfflineContext *disp_offline_ctx = reinterpret_cast<DisplayOfflineContext *>(display_ctx);

  if (!hw_rotator_device_ && IsRotationRequired(hw_layers)) {
    DLOGV_IF(kTagOfflineCtrl, "No Rotator device found");
    return kErrorHardware;
  }

  disp_offline_ctx->pending_rot_commit = false;

  uint32_t i = 0;
  while (hw_layers->closed_session_ids[i] >= 0) {
    error = hw_intf_->CloseRotatorSession(hw_rotator_device_, hw_layers->closed_session_ids[i]);
    if (LIKELY(error != kErrorNone)) {
      DLOGE("Rotator close session failed");
      return error;
    }
    hw_layers->closed_session_ids[i++] = -1;
  }


  if (IsRotationRequired(hw_layers)) {
    error = hw_intf_->OpenRotatorSession(hw_rotator_device_, hw_layers);
    if (LIKELY(error != kErrorNone)) {
      DLOGE("Rotator open session failed");
      return error;
    }

    error = hw_intf_->Validate(hw_rotator_device_, hw_layers);
    if (LIKELY(error != kErrorNone)) {
      DLOGE("Rotator validation failed");
      return error;
    }
    disp_offline_ctx->pending_rot_commit = true;
  }

  return kErrorNone;
}

DisplayError OfflineCtrl::Commit(Handle display_ctx, HWLayers *hw_layers) {
  DisplayError error = kErrorNone;

  DisplayOfflineContext *disp_offline_ctx = reinterpret_cast<DisplayOfflineContext *>(display_ctx);

  if (disp_offline_ctx->pending_rot_commit) {
    error = hw_intf_->Commit(hw_rotator_device_, hw_layers);
    if (error != kErrorNone) {
      DLOGE("Rotator commit failed");
      return error;
    }
    disp_offline_ctx->pending_rot_commit = false;
  }

  return kErrorNone;
}

bool OfflineCtrl::IsRotationRequired(HWLayers *hw_layers) {
  HWLayersInfo &layer_info = hw_layers->info;

  for (uint32_t i = 0; i < layer_info.count; i++) {
    Layer& layer = layer_info.stack->layers[layer_info.index[i]];

    HWRotateInfo *rotate = &hw_layers->config[i].rotates[0];
    if (rotate->valid) {
      return true;
    }

    rotate = &hw_layers->config[i].rotates[1];
    if (rotate->valid) {
      return true;
    }
  }
  return false;
}

}  // namespace sde

