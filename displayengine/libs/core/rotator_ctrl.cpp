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
#include <utils/rect.h>
#include <core/buffer_allocator.h>
#include <core/buffer_sync_handler.h>

#include "rotator_ctrl.h"
#include "hw_rotator_interface.h"
#include "hw_interface.h"
#include "session_manager.h"

#define __CLASS__ "RotatorCtrl"

namespace sde {

RotatorCtrl::RotatorCtrl() : hw_rotator_intf_(NULL) {
}

DisplayError RotatorCtrl::Init(BufferAllocator *buffer_allocator,
                               BufferSyncHandler *buffer_sync_handler) {
  DisplayError error = kErrorNone;

  error = HWRotatorInterface::Create(buffer_sync_handler, &hw_rotator_intf_);
  if (error != kErrorNone) {
    return error;
  }

  error = hw_rotator_intf_->Open();
  if (error != kErrorNone) {
    DLOGE("Failed to open rotator device");
    return error;
  }

  session_manager_ = new SessionManager(hw_rotator_intf_, buffer_allocator, buffer_sync_handler);
  if (session_manager_ == NULL) {
    HWRotatorInterface::Destroy(hw_rotator_intf_);
    return kErrorMemory;
  }

  return kErrorNone;
}

DisplayError RotatorCtrl::Deinit() {
  DisplayError error = kErrorNone;

  error = hw_rotator_intf_->Close();
  if (error != kErrorNone) {
    DLOGW("Failed to close rotator device");
    return error;
  }

  if (session_manager_) {
    delete session_manager_;
    session_manager_ = NULL;
  }

  HWRotatorInterface::Destroy(hw_rotator_intf_);

  return kErrorNone;
}

DisplayError RotatorCtrl::RegisterDisplay(DisplayType type, Handle *display_ctx) {
  DisplayRotatorContext *disp_rotator_ctx = new DisplayRotatorContext();
  if (disp_rotator_ctx == NULL) {
    return kErrorMemory;
  }

  disp_rotator_ctx->display_type = type;
  *display_ctx = disp_rotator_ctx;

  return kErrorNone;
}

void RotatorCtrl::UnregisterDisplay(Handle display_ctx) {
  DisplayRotatorContext *disp_rotator_ctx = reinterpret_cast<DisplayRotatorContext *>(display_ctx);

  delete disp_rotator_ctx;
  disp_rotator_ctx = NULL;
}


DisplayError RotatorCtrl::Prepare(Handle display_ctx, HWLayers *hw_layers) {
  DisplayError error = kErrorNone;

  DisplayRotatorContext *disp_rotator_ctx = reinterpret_cast<DisplayRotatorContext *>(display_ctx);

  error = PrepareSessions(disp_rotator_ctx, hw_layers);
  if (error != kErrorNone) {
    DLOGE("Prepare rotator session failed for display %d", disp_rotator_ctx->display_type);
    return error;
  }

  error = hw_rotator_intf_->Validate(hw_layers);
  if (error != kErrorNone) {
    DLOGE("Rotator validation failed for display %d", disp_rotator_ctx->display_type);
    return error;
  }

  return kErrorNone;
}

DisplayError RotatorCtrl::Commit(Handle display_ctx, HWLayers *hw_layers) {
  DisplayError error = kErrorNone;

  DisplayRotatorContext *disp_rotator_ctx = reinterpret_cast<DisplayRotatorContext *>(display_ctx);

  error = GetOutputBuffers(disp_rotator_ctx, hw_layers);
  if (error != kErrorNone) {
    return error;
  }

  error = hw_rotator_intf_->Commit(hw_layers);
  if (error != kErrorNone) {
    DLOGE("Rotator commit failed for display %d", disp_rotator_ctx->display_type);
    return error;
  }

  return kErrorNone;
}

DisplayError RotatorCtrl::PostCommit(Handle display_ctx, HWLayers *hw_layers) {
  HWLayersInfo &hw_layer_info = hw_layers->info;
  DisplayError error = kErrorNone;
  DisplayRotatorContext *disp_rotator_ctx = reinterpret_cast<DisplayRotatorContext *>(display_ctx);
  int client_id = INT(disp_rotator_ctx->display_type);

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer& layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;

    if (!hw_rotator_session->hw_block_count) {
      continue;
    }

    error = session_manager_->SetReleaseFd(client_id, hw_rotator_session);
    if (error != kErrorNone) {
      DLOGE("Rotator Post commit failed for display %d", disp_rotator_ctx->display_type);
      return error;
    }
  }

  return kErrorNone;
}

DisplayError RotatorCtrl::Purge(Handle display_ctx, HWLayers *hw_layers) {
  DisplayRotatorContext *disp_rotator_ctx = reinterpret_cast<DisplayRotatorContext *>(display_ctx);
  int client_id = INT(disp_rotator_ctx->display_type);

  session_manager_->Start(client_id);

  return session_manager_->Stop(client_id);
}

DisplayError RotatorCtrl::PrepareSessions(DisplayRotatorContext *disp_rotator_ctx,
                                          HWLayers *hw_layers) {
  HWLayersInfo &hw_layer_info = hw_layers->info;
  DisplayError error = kErrorNone;
  int client_id = INT(disp_rotator_ctx->display_type);

  session_manager_->Start(client_id);

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer& layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;
    HWSessionConfig &hw_session_config = hw_rotator_session->hw_session_config;
    HWRotateInfo *left_rotate = &hw_rotator_session->hw_rotate_info[0];
    HWRotateInfo *right_rotate = &hw_rotator_session->hw_rotate_info[1];

    if (!hw_rotator_session->hw_block_count) {
      continue;
    }

    hw_session_config.src_width = UINT32(layer.src_rect.right - layer.src_rect.left);
    hw_session_config.src_height = UINT32(layer.src_rect.bottom - layer.src_rect.top);
    hw_session_config.src_format = layer.input_buffer->format;

    LayerRect dst_rect = Union(left_rotate->dst_roi, right_rotate->dst_roi);

    hw_session_config.dst_width = UINT32(dst_rect.right - dst_rect.left);
    hw_session_config.dst_height = UINT32(dst_rect.bottom - dst_rect.top);
    hw_session_config.dst_format = hw_rotator_session->output_buffer.format;

    // Allocate two rotator output buffers by default for double buffering.
    hw_session_config.buffer_count = kDoubleBuffering;
    hw_session_config.secure = layer.input_buffer->flags.secure;
    hw_session_config.frame_rate = layer.frame_rate;

    error = session_manager_->OpenSession(client_id, hw_rotator_session);
    if (error != kErrorNone) {
      return error;
    }
  }

  error = session_manager_->Stop(client_id);
  if (error != kErrorNone) {
    return error;
  }

  return kErrorNone;
}

DisplayError RotatorCtrl::GetOutputBuffers(DisplayRotatorContext *disp_rotator_ctx,
                                           HWLayers *hw_layers) {
  HWLayersInfo &hw_layer_info = hw_layers->info;
  DisplayError error = kErrorNone;
  int client_id = INT(disp_rotator_ctx->display_type);

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer& layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;

    if (!hw_rotator_session->hw_block_count) {
      continue;
    }

    error = session_manager_->GetNextBuffer(client_id, hw_rotator_session);
    if (error != kErrorNone) {
      return error;
    }
  }

  return kErrorNone;
}

}  // namespace sde

