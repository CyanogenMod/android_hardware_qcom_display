/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

#include <utils/debug.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "hw_rotator.h"

#define __CLASS__ "HWRotator"

namespace sde {

DisplayError HWRotatorInterface::Create(HWRotatorInterface **intf,
                                        BufferSyncHandler *buffer_sync_handler) {
  DisplayError error = kErrorNone;
  HWRotator *hw_rotator = NULL;

  hw_rotator = new HWRotator(buffer_sync_handler);
  if (!hw_rotator) {
    error = kErrorMemory;
  } else {
    *intf = hw_rotator;
  }
  return error;
}

DisplayError HWRotatorInterface::Destroy(HWRotatorInterface *intf) {
  delete intf;
  intf = NULL;

  return kErrorNone;
}

HWRotator::HWRotator(BufferSyncHandler *buffer_sync_handler) : HWDevice(buffer_sync_handler) {
  HWDevice::device_type_ = kDeviceRotator;
  HWDevice::device_name_ = "Rotator Device";
}

DisplayError HWRotator::Open() {
  DisplayError error = kErrorNone;

  char device_name[64] = {0};
  snprintf(device_name, sizeof(device_name), "%s", "/dev/mdss_rotator");

  HWDevice::device_fd_ = open_(device_name, O_RDWR);
  if (HWDevice::device_fd_ < 0) {
    DLOGE("open %s failed err = %d errstr = %s", device_name, errno,  strerror(errno));
    return kErrorResources;
  }

  return error;
}

DisplayError HWRotator::Close() {
  if (HWDevice::device_fd_ > 0) {
    close_(HWDevice::device_fd_);
  }

  return kErrorNone;
}

DisplayError HWRotator::OpenSession(HWRotateInfo *rotate_info) {
  LayerBuffer *input_buffer = rotate_info->input_buffer;
  HWBufferInfo *rot_buf_info = &rotate_info->hw_buffer_info;

  ResetRotatorParams();

  STRUCT_VAR(mdp_rotation_config, mdp_rot_config);
  mdp_rot_config.version = MDP_ROTATION_REQUEST_VERSION_1_0;
  mdp_rot_config.input.width = input_buffer->width;
  mdp_rot_config.input.height = input_buffer->height;
  HWDevice::SetFormat(input_buffer->format, &mdp_rot_config.input.format);
  mdp_rot_config.output.width = rot_buf_info->output_buffer.width;
  mdp_rot_config.output.height = rot_buf_info->output_buffer.height;
  HWDevice::SetFormat(rot_buf_info->output_buffer.format, &mdp_rot_config.output.format);
  mdp_rot_config.frame_rate = rotate_info->frame_rate;

  if (ioctl_(HWDevice::device_fd_, MDSS_ROTATION_OPEN, &mdp_rot_config) < 0) {
    IOCTL_LOGE(MDSS_ROTATION_OPEN, HWDevice::device_type_);
    return kErrorHardware;
  }

  rot_buf_info->session_id = mdp_rot_config.session_id;

  DLOGV_IF(kTagDriverConfig, "session_id %d", rot_buf_info->session_id);

  return kErrorNone;
}

DisplayError HWRotator::CloseSession(int32_t session_id) {
  if (ioctl_(HWDevice::device_fd_, MDSS_ROTATION_CLOSE, (uint32_t)session_id) < 0) {
    IOCTL_LOGE(MDSS_ROTATION_CLOSE, HWDevice::device_type_);
    return kErrorHardware;
  }

  DLOGV_IF(kTagDriverConfig, "session_id %d", session_id);

  return kErrorNone;
}

void HWRotator::SetRotatorCtrlParams(HWLayers *hw_layers) {
  DLOGV_IF(kTagDriverConfig, "************************* %s Validate Input ************************",
           HWDevice::device_name_);

  ResetRotatorParams();

  HWLayersInfo &hw_layer_info = hw_layers->info;

  uint32_t &rot_count = mdp_rot_request_.count;
  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer& layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];

    for (uint32_t count = 0; count < 2; count++) {
      HWRotateInfo *rotate_info = &hw_layers->config[i].rotates[count];

      if (rotate_info->valid) {
        HWBufferInfo *rot_buf_info = &rotate_info->hw_buffer_info;
        mdp_rotation_item *mdp_rot_item = &mdp_rot_request_.list[rot_count];
        bool rot90 = (layer.transform.rotation == 90.0f);

        if (rot90) {
          mdp_rot_item->flags |= MDP_ROTATION_90;
        }

        if (layer.transform.flip_horizontal) {
          mdp_rot_item->flags |= MDP_ROTATION_FLIP_LR;
        }

        if (layer.transform.flip_vertical) {
          mdp_rot_item->flags |= MDP_ROTATION_FLIP_UD;
        }

        HWDevice::SetRect(rotate_info->src_roi, &mdp_rot_item->src_rect);
        HWDevice::SetRect(rotate_info->dst_roi, &mdp_rot_item->dst_rect);

        // TODO(user): Need to assign the writeback id and pipe id  returned from resource manager.
        mdp_rot_item->pipe_idx = 0;
        mdp_rot_item->wb_idx = 0;

        mdp_rot_item->input.width = layer.input_buffer->width;
        mdp_rot_item->input.height = layer.input_buffer->height;
        HWDevice::SetFormat(layer.input_buffer->format, &mdp_rot_item->input.format);

        mdp_rot_item->output.width = rot_buf_info->output_buffer.width;
        mdp_rot_item->output.height = rot_buf_info->output_buffer.height;
        HWDevice::SetFormat(rot_buf_info->output_buffer.format, &mdp_rot_item->output.format);

        rot_count++;

        DLOGV_IF(kTagDriverConfig, "******************** Layer[%d] %s rotate ********************",
                 i, count ? "Right" : "Left");
        DLOGV_IF(kTagDriverConfig, "in_w %d, in_h %d, in_f %d,\t out_w %d, out_h %d, out_f %d",
                 mdp_rot_item->input.width, mdp_rot_item->input.height, mdp_rot_item->input.format,
                 mdp_rot_item->output.width, mdp_rot_item->output.height,
                 mdp_rot_item->output.format);
        DLOGV_IF(kTagDriverConfig, "pipe_id %d, wb_id %d, rot_flag %d", mdp_rot_item->pipe_idx,
                 mdp_rot_item->wb_idx, mdp_rot_item->flags);
        DLOGV_IF(kTagDriverConfig, "src_rect [%d, %d, %d, %d]", mdp_rot_item->src_rect.x,
                 mdp_rot_item->src_rect.y, mdp_rot_item->src_rect.w, mdp_rot_item->src_rect.h);
        DLOGV_IF(kTagDriverConfig, "dst_rect [%d, %d, %d, %d]", mdp_rot_item->dst_rect.x,
                 mdp_rot_item->dst_rect.y, mdp_rot_item->dst_rect.w, mdp_rot_item->dst_rect.h);
        DLOGV_IF(kTagDriverConfig, "*************************************************************");
      }
    }
  }
}

void HWRotator::SetRotatorBufferParams(HWLayers *hw_layers) {
  HWLayersInfo &hw_layer_info = hw_layers->info;
  uint32_t rot_count = 0;

  DLOGV_IF(kTagDriverConfig, "************************* %s Commit Input **************************",
           HWDevice::device_name_);
  DLOGV_IF(kTagDriverConfig, "Rotate layer count is %d", mdp_rot_request_.count);

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer& layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];

    for (uint32_t count = 0; count < 2; count++) {
      HWRotateInfo *rotate_info = &hw_layers->config[i].rotates[count];

      if (rotate_info->valid) {
        HWBufferInfo *rot_buf_info = &rotate_info->hw_buffer_info;
        mdp_rotation_item *mdp_rot_item = &mdp_rot_request_.list[rot_count];

        mdp_rot_item->input.planes[0].fd = layer.input_buffer->planes[0].fd;
        mdp_rot_item->input.planes[0].offset = layer.input_buffer->planes[0].offset;
        HWDevice::SetStride(HWDevice::device_type_, layer.input_buffer->format,
                            layer.input_buffer->width, &mdp_rot_item->input.planes[0].stride);
        mdp_rot_item->input.plane_count = 1;
        mdp_rot_item->input.fence = layer.input_buffer->acquire_fence_fd;

        mdp_rot_item->output.planes[0].fd = rot_buf_info->output_buffer.planes[0].fd;
        mdp_rot_item->output.planes[0].offset = rot_buf_info->output_buffer.planes[0].offset;
        HWDevice::SetStride(HWDevice::device_type_, rot_buf_info->output_buffer.format,
                            rot_buf_info->output_buffer.planes[0].stride,
                            &mdp_rot_item->output.planes[0].stride);
        mdp_rot_item->output.plane_count = 1;
        mdp_rot_item->output.fence = -1;

        rot_count++;

        DLOGV_IF(kTagDriverConfig, "******************** Layer[%d] %s rotate ********************",
                 i, count ? "Right" : "Left");
        DLOGV_IF(kTagDriverConfig, "in_buf_fd %d, in_buf_offset %d, in_stride %d, " \
                 "in_plane_count %d, in_fence %d", mdp_rot_item->input.planes[0].fd,
                 mdp_rot_item->input.planes[0].offset, mdp_rot_item->input.planes[0].stride,
                 mdp_rot_item->input.plane_count, mdp_rot_item->input.fence);
        DLOGV_IF(kTagDriverConfig, "out_fd %d, out_offset %d, out_stride %d, out_plane_count %d, " \
                 "out_fence %d", mdp_rot_item->output.planes[0].fd,
                 mdp_rot_item->output.planes[0].offset, mdp_rot_item->output.planes[0].stride,
                 mdp_rot_item->output.plane_count, mdp_rot_item->output.fence);
        DLOGV_IF(kTagDriverConfig, "*************************************************************");
      }
    }
  }
}

DisplayError HWRotator::Validate(HWLayers *hw_layers) {
  SetRotatorCtrlParams(hw_layers);

  mdp_rot_request_.flags = MDSS_ROTATION_REQUEST_VALIDATE;
  if (ioctl_(HWDevice::device_fd_, MDSS_ROTATION_REQUEST, &mdp_rot_request_) < 0) {
    IOCTL_LOGE(MDSS_ROTATION_REQUEST, HWDevice::device_type_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWRotator::Commit(HWLayers *hw_layers) {
  HWLayersInfo &hw_layer_info = hw_layers->info;
  uint32_t rot_count = 0;

  SetRotatorCtrlParams(hw_layers);

  SetRotatorBufferParams(hw_layers);

  mdp_rot_request_.flags &= ~MDSS_ROTATION_REQUEST_VALIDATE;
  if (ioctl_(HWDevice::device_fd_, MDSS_ROTATION_REQUEST, &mdp_rot_request_) < 0) {
    IOCTL_LOGE(MDSS_ROTATION_REQUEST, HWDevice::device_type_);
    return kErrorHardware;
  }

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer& layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];

    layer.input_buffer->release_fence_fd = -1;

    for (uint32_t count = 0; count < 2; count++) {
      HWRotateInfo *rotate_info = &hw_layers->config[i].rotates[count];

      if (rotate_info->valid) {
        HWBufferInfo *rot_buf_info = &rotate_info->hw_buffer_info;
        mdp_rotation_item *mdp_rot_item = &mdp_rot_request_.list[rot_count];

        HWDevice::SyncMerge(layer.input_buffer->release_fence_fd, dup(mdp_rot_item->output.fence),
                            &layer.input_buffer->release_fence_fd);

        rot_buf_info->output_buffer.acquire_fence_fd = dup(mdp_rot_item->output.fence);

        close_(mdp_rot_item->output.fence);
        rot_count++;
      }
    }
  }

  return kErrorNone;
}

void HWRotator::ResetRotatorParams() {
  memset(&mdp_rot_request_, 0, sizeof(mdp_rot_request_));
  memset(&mdp_rot_layers_, 0, sizeof(mdp_rot_layers_));

  for (uint32_t i = 0; i < kMaxSDELayers * 2; i++) {
    mdp_rot_layers_[i].input.fence = -1;
    mdp_rot_layers_[i].output.fence = -1;
  }

  mdp_rot_request_.version = MDP_ROTATION_REQUEST_VERSION_1_0;
  mdp_rot_request_.list = mdp_rot_layers_;
}

}  // namespace sde

