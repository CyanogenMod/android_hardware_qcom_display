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

#define __STDC_FORMAT_MACROS
#include <ctype.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <utils/constants.h>
#include <utils/debug.h>

#include "hw_device.h"

#define __CLASS__ "HWDevice"

namespace sdm {

HWDevice::HWDevice(BufferSyncHandler *buffer_sync_handler)
  : fb_node_index_(-1), fb_path_("/sys/devices/virtual/graphics/fb"), hotplug_enabled_(false),
    buffer_sync_handler_(buffer_sync_handler), synchronous_commit_(false) {
#ifndef SDM_VIRTUAL_DRIVER
  // Pointer to actual driver interfaces.
  ioctl_ = ::ioctl;
  open_ = ::open;
  close_ = ::close;
  poll_ = ::poll;
  pread_ = ::pread;
  pwrite_ = ::pwrite;
  fopen_ = ::fopen;
  fclose_ = ::fclose;
  getline_ = ::getline;
#else
  // Point to virtual driver interfaces.
  extern int virtual_ioctl(int fd, int cmd, ...);
  extern int virtual_open(const char *file_name, int access, ...);
  extern int virtual_close(int fd);
  extern int virtual_poll(struct pollfd *fds,  nfds_t num, int timeout);
  extern ssize_t virtual_pread(int fd, void *data, size_t count, off_t offset);
  extern ssize_t virtual_pwrite(int fd, const void *data, size_t count, off_t offset);
  extern FILE* virtual_fopen(const char *fname, const char *mode);
  extern int virtual_fclose(FILE* fileptr);
  extern ssize_t virtual_getline(char **lineptr, size_t *linelen, FILE *stream);

  ioctl_ = virtual_ioctl;
  open_ = virtual_open;
  close_ = virtual_close;
  poll_ = virtual_poll;
  pread_ = virtual_pread;
  pwrite_ = virtual_pwrite;
  fopen_ = virtual_fopen;
  fclose_ = virtual_fclose;
  getline_ = virtual_getline;
#endif
}

DisplayError HWDevice::Init() {
  DisplayError error = kErrorNone;

  // Read the fb node index
  fb_node_index_ = GetFBNodeIndex(device_type_);
  if (fb_node_index_ == -1) {
    DLOGE("%s should be present", device_name_);
    return kErrorHardware;
  }

  // Populate Panel Info (Used for Partial Update)
  PopulateHWPanelInfo();
  // Populate HW Capabilities
  hw_resource_ = HWResourceInfo();
  hw_info_intf_->GetHWResourceInfo(&hw_resource_);

  return error;
}

DisplayError HWDevice::Open(HWEventHandler *eventhandler) {
  DisplayError error = kErrorNone;

  char device_name[64] = {0};

  // Store EventHandlers for two Physical displays, i.e., Primary and HDMI
  // TODO(user): Need to revisit for HDMI as Primary usecase
  event_handler_ = eventhandler;
  snprintf(device_name, sizeof(device_name), "%s%d", "/dev/graphics/fb", fb_node_index_);

  device_fd_ = open_(device_name, O_RDWR);
  if (device_fd_ < 0) {
    DLOGE("open %s failed err = %d errstr = %s", device_name, errno,  strerror(errno));
    return kErrorResources;
  }

  return error;
}

DisplayError HWDevice::Close() {
  if (device_fd_ > 0) {
    close_(device_fd_);
  }

  return kErrorNone;
}

DisplayError HWDevice::GetNumDisplayAttributes(uint32_t *count) {
  *count = 1;
  return kErrorNone;
}

DisplayError HWDevice::GetDisplayAttributes(HWDisplayAttributes *display_attributes,
                                                 uint32_t index) {
  return kErrorNone;
}

DisplayError HWDevice::GetHWPanelInfo(HWPanelInfo *panel_info) {
  *panel_info = hw_panel_info_;
  return kErrorNone;
}

DisplayError HWDevice::SetDisplayAttributes(uint32_t index) {
  return kErrorNone;
}

DisplayError HWDevice::GetConfigIndex(uint32_t mode, uint32_t *index) {
  return kErrorNone;
}


DisplayError HWDevice::PowerOn() {
  DTRACE_SCOPED();

  if (ioctl_(device_fd_, FBIOBLANK, FB_BLANK_UNBLANK) < 0) {
    IOCTL_LOGE(FB_BLANK_UNBLANK, device_type_);
    return kErrorHardware;
  }

  // Need to turn on HPD
  if (!hotplug_enabled_) {
    hotplug_enabled_ = EnableHotPlugDetection(1);
  }

  return kErrorNone;
}

DisplayError HWDevice::PowerOff() {
  return kErrorNone;
}

DisplayError HWDevice::Doze() {
  return kErrorNone;
}

DisplayError HWDevice::DozeSuspend() {
  return kErrorNone;
}

DisplayError HWDevice::Standby() {
  return kErrorNone;
}

DisplayError HWDevice::Validate(HWLayers *hw_layers) {
  DTRACE_SCOPED();

  DisplayError error = kErrorNone;

  HWLayersInfo &hw_layer_info = hw_layers->info;
  LayerStack *stack = hw_layer_info.stack;

  DLOGV_IF(kTagDriverConfig, "************************** %s Validate Input ***********************",
           device_name_);
  DLOGV_IF(kTagDriverConfig, "SDE layer count is %d", hw_layer_info.count);

  mdp_layer_commit_v1 &mdp_commit = mdp_disp_commit_.commit_v1;
  uint32_t &mdp_layer_count = mdp_commit.input_layer_cnt;

  DLOGI_IF(kTagDriverConfig, "left_roi: x = %d, y = %d, w = %d, h = %d", mdp_commit.left_roi.x,
    mdp_commit.left_roi.y, mdp_commit.left_roi.w, mdp_commit.left_roi.h);
  DLOGI_IF(kTagDriverConfig, "right_roi: x = %d, y = %d, w = %d, h = %d", mdp_commit.right_roi.x,
    mdp_commit.right_roi.y, mdp_commit.right_roi.w, mdp_commit.right_roi.h);

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    uint32_t layer_index = hw_layer_info.index[i];
    Layer &layer = stack->layers[layer_index];
    LayerBuffer *input_buffer = layer.input_buffer;
    HWPipeInfo *left_pipe = &hw_layers->config[i].left_pipe;
    HWPipeInfo *right_pipe = &hw_layers->config[i].right_pipe;
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;
    bool is_rotator_used = (hw_rotator_session->hw_block_count != 0);
    mdp_input_layer mdp_layer;

    for (uint32_t count = 0; count < 2; count++) {
      HWPipeInfo *pipe_info = (count == 0) ? left_pipe : right_pipe;
      HWRotateInfo *hw_rotate_info = &hw_rotator_session->hw_rotate_info[count];

      if (hw_rotate_info->valid) {
        input_buffer = &hw_rotator_session->output_buffer;
      }

      if (pipe_info->valid) {
        mdp_input_layer &mdp_layer = mdp_in_layers_[mdp_layer_count];
        mdp_layer_buffer &mdp_buffer = mdp_layer.buffer;

        mdp_buffer.width = input_buffer->width;
        mdp_buffer.height = input_buffer->height;
        mdp_buffer.comp_ratio.denom = 1000;
        mdp_buffer.comp_ratio.numer = UINT32(hw_layers->config[i].compression * 1000);

        error = SetFormat(input_buffer->format, &mdp_buffer.format);
        if (error != kErrorNone) {
          return error;
        }

        mdp_layer.alpha = layer.plane_alpha;
        mdp_layer.z_order = UINT16(pipe_info->z_order);
        mdp_layer.transp_mask = 0xffffffff;
        SetBlending(layer.blending, &mdp_layer.blend_op);
        mdp_layer.pipe_ndx = pipe_info->pipe_id;
        mdp_layer.horz_deci = pipe_info->horizontal_decimation;
        mdp_layer.vert_deci = pipe_info->vertical_decimation;

        SetRect(pipe_info->src_roi, &mdp_layer.src_rect);
        SetRect(pipe_info->dst_roi, &mdp_layer.dst_rect);
        SetMDPFlags(layer, is_rotator_used, &mdp_layer.flags);
        SetColorSpace(layer.color_space, &mdp_layer.color_space);

        if (pipe_info->scale_data.enable_pixel_ext) {
          if ((mdp_layer.flags & MDP_LAYER_DEINTERLACE) && (layer.transform.rotation == 90.0f)) {
            mdp_buffer.width = pipe_info->scale_data.src_width;
          }
          SetHWScaleData(pipe_info->scale_data, mdp_layer_count);
        }

        // Send scale data to MDP driver
        mdp_layer.scale = GetScaleDataRef(mdp_layer_count);
        mdp_layer_count++;

        DLOGV_IF(kTagDriverConfig, "******************* Layer[%d] %s pipe Input ******************",
                 i, count ? "Right" : "Left");
        DLOGV_IF(kTagDriverConfig, "in_w %d, in_h %d, in_f %d", mdp_buffer.width, mdp_buffer.height,
                 mdp_buffer.format);
        DLOGV_IF(kTagDriverConfig, "plane_alpha %d, zorder %d, blending %d, horz_deci %d, "
                 "vert_deci %d, pipe_id = 0x%x, mdp_flags 0x%x", mdp_layer.alpha, mdp_layer.z_order,
                 mdp_layer.blend_op, mdp_layer.horz_deci, mdp_layer.vert_deci, mdp_layer.pipe_ndx,
                 mdp_layer.flags);
        DLOGV_IF(kTagDriverConfig, "src_rect [%d, %d, %d, %d]", mdp_layer.src_rect.x,
                 mdp_layer.src_rect.y, mdp_layer.src_rect.w, mdp_layer.src_rect.h);
        DLOGV_IF(kTagDriverConfig, "dst_rect [%d, %d, %d, %d]", mdp_layer.dst_rect.x,
                 mdp_layer.dst_rect.y, mdp_layer.dst_rect.w, mdp_layer.dst_rect.h);
        for (int j = 0; j < 4; j++) {
          DLOGV_IF(kTagDriverConfig, "Scale Data[%d]: Phase=[%x %x %x %x] Pixel_Ext=[%d %d %d %d]",
                 j, mdp_layer.scale->init_phase_x[j], mdp_layer.scale->phase_step_x[j],
                 mdp_layer.scale->init_phase_y[j], mdp_layer.scale->phase_step_y[j],
                 mdp_layer.scale->num_ext_pxls_left[j], mdp_layer.scale->num_ext_pxls_top[j],
                 mdp_layer.scale->num_ext_pxls_right[j], mdp_layer.scale->num_ext_pxls_btm[j]);
          DLOGV_IF(kTagDriverConfig, "Fetch=[%d %d %d %d]  Repeat=[%d %d %d %d]  roi_width = %d",
                 mdp_layer.scale->left_ftch[j], mdp_layer.scale->top_ftch[j],
                 mdp_layer.scale->right_ftch[j], mdp_layer.scale->btm_ftch[j],
                 mdp_layer.scale->left_rpt[j], mdp_layer.scale->top_rpt[j],
                 mdp_layer.scale->right_rpt[j], mdp_layer.scale->btm_rpt[j],
                 mdp_layer.scale->roi_w[j]);
        }
        DLOGV_IF(kTagDriverConfig, "*************************************************************");
      }
    }
  }

  if (device_type_ == kDeviceVirtual) {
    LayerBuffer *output_buffer = hw_layers->info.stack->output_buffer;
    // TODO(user): Need to assign the writeback id from the resource manager, since the support
    // has not been added hard coding it to 2 for now.
    mdp_out_layer_.writeback_ndx = 2;
    mdp_out_layer_.buffer.width = output_buffer->width;
    mdp_out_layer_.buffer.height = output_buffer->height;
    mdp_out_layer_.buffer.comp_ratio.denom = 1000;
    mdp_out_layer_.buffer.comp_ratio.numer = UINT32(hw_layers->output_compression * 1000);
    SetFormat(output_buffer->format, &mdp_out_layer_.buffer.format);

    DLOGI_IF(kTagDriverConfig, "********************* Output buffer Info ************************");
    DLOGI_IF(kTagDriverConfig, "out_w %d, out_h %d, out_f %d, wb_id %d",
             mdp_out_layer_.buffer.width, mdp_out_layer_.buffer.height,
             mdp_out_layer_.buffer.format, mdp_out_layer_.writeback_ndx);
    DLOGI_IF(kTagDriverConfig, "*****************************************************************");
  }

  mdp_commit.flags |= MDP_VALIDATE_LAYER;
  if (ioctl_(device_fd_, MSMFB_ATOMIC_COMMIT, &mdp_disp_commit_) < 0) {
    IOCTL_LOGE(MSMFB_ATOMIC_COMMIT, device_type_);
    DumpLayerCommit(mdp_disp_commit_);
    return kErrorHardware;
  }

  return kErrorNone;
}

void HWDevice::DumpLayerCommit(const mdp_layer_commit &layer_commit) {
  const mdp_layer_commit_v1 &mdp_commit = layer_commit.commit_v1;
  const mdp_input_layer *mdp_layers = mdp_commit.input_layers;

  DLOGE("mdp_commit: flags = %x, release fence = %x", mdp_commit.flags, mdp_commit.release_fence);
  DLOGE("left_roi: x = %d, y = %d, w = %d, h = %d", mdp_commit.left_roi.x, mdp_commit.left_roi.y,
         mdp_commit.left_roi.w, mdp_commit.left_roi.h);
  DLOGE("right_roi: x = %d, y = %d, w = %d, h = %d", mdp_commit.right_roi.x,
         mdp_commit.right_roi.y, mdp_commit.right_roi.w, mdp_commit.right_roi.h);
  for (uint32_t i = 0; i < mdp_commit.input_layer_cnt; i++) {
    DLOGE("mdp_commit: layer_cnt = %d, pipe_ndx = %x, zorder = %d, flags = %x",
          i, mdp_layers[i].pipe_ndx, mdp_layers[i].z_order, mdp_layers[i].flags);
    const mdp_rect &src_rect = mdp_layers[i].src_rect;
    DLOGE("src rect: x = %d, y = %d, w = %d, h = %d",
          src_rect.x, src_rect.y, src_rect.w, src_rect.h);
    const mdp_rect &dst_rect = mdp_layers[i].dst_rect;
    DLOGE("dst rect: x = %d, y = %d, w = %d, h = %d",
          dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h);
  }
}

DisplayError HWDevice::Commit(HWLayers *hw_layers) {
  DTRACE_SCOPED();

  HWLayersInfo &hw_layer_info = hw_layers->info;
  LayerStack *stack = hw_layer_info.stack;

  DLOGV_IF(kTagDriverConfig, "*************************** %s Commit Input ************************",
           device_name_);
  DLOGV_IF(kTagDriverConfig, "SDE layer count is %d", hw_layer_info.count);

  mdp_layer_commit_v1 &mdp_commit = mdp_disp_commit_.commit_v1;
  uint32_t mdp_layer_index = 0;

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    uint32_t layer_index = hw_layer_info.index[i];
    LayerBuffer *input_buffer = stack->layers[layer_index].input_buffer;
    HWPipeInfo *left_pipe = &hw_layers->config[i].left_pipe;
    HWPipeInfo *right_pipe = &hw_layers->config[i].right_pipe;
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;

    for (uint32_t count = 0; count < 2; count++) {
      HWPipeInfo *pipe_info = (count == 0) ? left_pipe : right_pipe;
      HWRotateInfo *hw_rotate_info = &hw_rotator_session->hw_rotate_info[count];

      if (hw_rotate_info->valid) {
        input_buffer = &hw_rotator_session->output_buffer;
      }

      if (pipe_info->valid) {
        mdp_layer_buffer &mdp_buffer = mdp_in_layers_[mdp_layer_index].buffer;
        mdp_input_layer &mdp_layer = mdp_in_layers_[mdp_layer_index];
        if (input_buffer->planes[0].fd >= 0) {
          mdp_buffer.plane_count = 1;
          mdp_buffer.planes[0].fd = input_buffer->planes[0].fd;
          mdp_buffer.planes[0].offset = input_buffer->planes[0].offset;
          SetStride(device_type_, input_buffer->format, input_buffer->planes[0].stride,
                    &mdp_buffer.planes[0].stride);
        } else {
          DLOGW("Invalid buffer fd, setting plane count to 0");
          mdp_buffer.plane_count = 0;
        }

        mdp_buffer.fence = input_buffer->acquire_fence_fd;
        mdp_layer_index++;

        DLOGV_IF(kTagDriverConfig, "****************** Layer[%d] %s pipe Input *******************",
                 i, count ? "Right" : "Left");
        DLOGI_IF(kTagDriverConfig, "in_w %d, in_h %d, in_f %d, horz_deci %d, vert_deci %d",
                 mdp_buffer.width, mdp_buffer.height, mdp_buffer.format, mdp_layer.horz_deci,
                 mdp_layer.vert_deci);
        DLOGI_IF(kTagDriverConfig, "in_buf_fd %d, in_buf_offset %d, in_buf_stride %d, " \
                 "in_plane_count %d, in_fence %d, layer count %d", mdp_buffer.planes[0].fd,
                 mdp_buffer.planes[0].offset, mdp_buffer.planes[0].stride, mdp_buffer.plane_count,
                 mdp_buffer.fence, mdp_commit.input_layer_cnt);
        DLOGV_IF(kTagDriverConfig, "*************************************************************");
      }
    }
  }
  if (device_type_ == kDeviceVirtual) {
    LayerBuffer *output_buffer = hw_layers->info.stack->output_buffer;

    if (output_buffer->planes[0].fd >= 0) {
      mdp_out_layer_.buffer.planes[0].fd = output_buffer->planes[0].fd;
      mdp_out_layer_.buffer.planes[0].offset = output_buffer->planes[0].offset;
      SetStride(device_type_, output_buffer->format, output_buffer->planes[0].stride,
                &mdp_out_layer_.buffer.planes[0].stride);
      mdp_out_layer_.buffer.plane_count = 1;
    } else {
      DLOGW("Invalid output buffer fd, setting plane count to 0");
      mdp_out_layer_.buffer.plane_count = 0;
    }

    mdp_out_layer_.buffer.fence = output_buffer->acquire_fence_fd;

    DLOGI_IF(kTagDriverConfig, "********************** Output buffer Info ***********************");
    DLOGI_IF(kTagDriverConfig, "out_fd %d, out_offset %d, out_stride %d, acquire_fence %d",
             mdp_out_layer_.buffer.planes[0].fd, mdp_out_layer_.buffer.planes[0].offset,
             mdp_out_layer_.buffer.planes[0].stride,  mdp_out_layer_.buffer.fence);
    DLOGI_IF(kTagDriverConfig, "*****************************************************************");
  }

  mdp_commit.release_fence = -1;
  mdp_commit.flags &= ~MDP_VALIDATE_LAYER;
  if (synchronous_commit_) {
    mdp_commit.flags |= MDP_COMMIT_WAIT_FOR_FINISH;
  }
  if (ioctl_(device_fd_, MSMFB_ATOMIC_COMMIT, &mdp_disp_commit_) < 0) {
    IOCTL_LOGE(MSMFB_ATOMIC_COMMIT, device_type_);
    DumpLayerCommit(mdp_disp_commit_);
    synchronous_commit_ = false;
    return kErrorHardware;
  }

  stack->retire_fence_fd = mdp_commit.retire_fence;

  // MDP returns only one release fence for the entire layer stack. Duplicate this fence into all
  // layers being composed by MDP.
  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    uint32_t layer_index = hw_layer_info.index[i];
    LayerBuffer *input_buffer = stack->layers[layer_index].input_buffer;
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;

    if (hw_rotator_session->hw_block_count) {
      input_buffer = &hw_rotator_session->output_buffer;
    }

    input_buffer->release_fence_fd = dup(mdp_commit.release_fence);
  }
  DLOGI_IF(kTagDriverConfig, "*************************** %s Commit Input ************************",
           device_name_);
  DLOGI_IF(kTagDriverConfig, "retire_fence_fd %d", stack->retire_fence_fd);
  DLOGI_IF(kTagDriverConfig, "*******************************************************************");

  close_(mdp_commit.release_fence);

  if (synchronous_commit_) {
    // A synchronous commit can be requested when changing the display mode so we need to update
    // panel info.
    PopulateHWPanelInfo();
    synchronous_commit_ = false;
  }

  return kErrorNone;
}

DisplayError HWDevice::Flush() {
  ResetDisplayParams();
  mdp_layer_commit_v1 &mdp_commit = mdp_disp_commit_.commit_v1;
  mdp_commit.input_layer_cnt = 0;
  mdp_commit.output_layer = NULL;

  mdp_commit.flags &= ~MDP_VALIDATE_LAYER;
  if (ioctl_(device_fd_, MSMFB_ATOMIC_COMMIT, &mdp_disp_commit_) < 0) {
    IOCTL_LOGE(MSMFB_ATOMIC_COMMIT, device_type_);
    DumpLayerCommit(mdp_disp_commit_);
    return kErrorHardware;
  }
  return kErrorNone;
}

DisplayError HWDevice::SetFormat(const LayerBufferFormat &source, uint32_t *target) {
  switch (source) {
  case kFormatARGB8888:                 *target = MDP_ARGB_8888;         break;
  case kFormatRGBA8888:                 *target = MDP_RGBA_8888;         break;
  case kFormatBGRA8888:                 *target = MDP_BGRA_8888;         break;
  case kFormatRGBX8888:                 *target = MDP_RGBX_8888;         break;
  case kFormatBGRX8888:                 *target = MDP_BGRX_8888;         break;
  case kFormatRGBA5551:                 *target = MDP_RGBA_5551;         break;
  case kFormatRGBA4444:                 *target = MDP_RGBA_4444;         break;
  case kFormatRGB888:                   *target = MDP_RGB_888;           break;
  case kFormatBGR888:                   *target = MDP_BGR_888;           break;
  case kFormatRGB565:                   *target = MDP_RGB_565;           break;
  case kFormatYCbCr420Planar:           *target = MDP_Y_CB_CR_H2V2;      break;
  case kFormatYCrCb420Planar:           *target = MDP_Y_CR_CB_H2V2;      break;
  case kFormatYCbCr420SemiPlanar:       *target = MDP_Y_CBCR_H2V2;       break;
  case kFormatYCrCb420SemiPlanar:       *target = MDP_Y_CRCB_H2V2;       break;
  case kFormatYCbCr422H1V2SemiPlanar:   *target = MDP_Y_CBCR_H1V2;       break;
  case kFormatYCrCb422H1V2SemiPlanar:   *target = MDP_Y_CRCB_H1V2;       break;
  case kFormatYCbCr422H2V1SemiPlanar:   *target = MDP_Y_CBCR_H2V1;       break;
  case kFormatYCrCb422H2V1SemiPlanar:   *target = MDP_Y_CRCB_H2V1;       break;
  case kFormatYCbCr422H2V1Packed:       *target = MDP_YCBYCR_H2V1;       break;
  case kFormatYCbCr420SemiPlanarVenus:  *target = MDP_Y_CBCR_H2V2_VENUS; break;
  case kFormatRGBA8888Ubwc:             *target = MDP_RGBA_8888_UBWC;    break;
  case kFormatRGBX8888Ubwc:             *target = MDP_RGBX_8888_UBWC;    break;
  case kFormatRGB565Ubwc:               *target = MDP_RGB_565_UBWC;      break;
  case kFormatYCbCr420SPVenusUbwc:      *target = MDP_Y_CBCR_H2V2_UBWC;  break;
  default:
    DLOGE("Unsupported format type %d", source);
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError HWDevice::SetStride(HWDeviceType device_type, LayerBufferFormat format,
                                      uint32_t width, uint32_t *target) {
  // TODO(user): This SetStride function is a workaround to satisfy the driver expectation for
  // rotator and virtual devices. Eventually this will be taken care in the driver.
  if (device_type != kDeviceRotator && device_type != kDeviceVirtual) {
    *target = width;
    return kErrorNone;
  }

  switch (format) {
  case kFormatARGB8888:
  case kFormatRGBA8888:
  case kFormatBGRA8888:
  case kFormatRGBX8888:
  case kFormatBGRX8888:
  case kFormatRGBA8888Ubwc:
  case kFormatRGBX8888Ubwc:
    *target = width * 4;
    break;
  case kFormatRGB888:
  case kFormatBGR888:
    *target = width * 3;
    break;
  case kFormatRGB565:
  case kFormatRGB565Ubwc:
    *target = width * 2;
    break;
  case kFormatYCbCr420SemiPlanarVenus:
  case kFormatYCbCr420SPVenusUbwc:
  case kFormatYCbCr420Planar:
  case kFormatYCrCb420Planar:
  case kFormatYCbCr420SemiPlanar:
  case kFormatYCrCb420SemiPlanar:
    *target = width;
    break;
  case kFormatYCbCr422H2V1Packed:
  case kFormatYCrCb422H2V1SemiPlanar:
  case kFormatYCrCb422H1V2SemiPlanar:
  case kFormatYCbCr422H2V1SemiPlanar:
  case kFormatYCbCr422H1V2SemiPlanar:
  case kFormatRGBA5551:
  case kFormatRGBA4444:
    *target = width * 2;
    break;
  default:
    DLOGE("Unsupported format type %d", format);
    return kErrorParameters;
  }

  return kErrorNone;
}

void HWDevice::SetBlending(const LayerBlending &source, mdss_mdp_blend_op *target) {
  switch (source) {
  case kBlendingPremultiplied:  *target = BLEND_OP_PREMULTIPLIED;   break;
  case kBlendingCoverage:       *target = BLEND_OP_COVERAGE;        break;
  default:                      *target = BLEND_OP_NOT_DEFINED;     break;
  }
}

void HWDevice::SetRect(const LayerRect &source, mdp_rect *target) {
  target->x = UINT32(source.left);
  target->y = UINT32(source.top);
  target->w = UINT32(source.right) - target->x;
  target->h = UINT32(source.bottom) - target->y;
}

void HWDevice::SetMDPFlags(const Layer &layer, const bool &is_rotator_used,
                           uint32_t *mdp_flags) {
  LayerBuffer *input_buffer = layer.input_buffer;

  // Flips will be taken care by rotator, if layer uses rotator for downscale/rotation. So ignore
  // flip flags for MDP.
  if (!is_rotator_used) {
    if (layer.transform.flip_vertical) {
      *mdp_flags |= MDP_LAYER_FLIP_UD;
    }

    if (layer.transform.flip_horizontal) {
      *mdp_flags |= MDP_LAYER_FLIP_LR;
    }
  }

  if (input_buffer->flags.interlace) {
    *mdp_flags |= MDP_LAYER_DEINTERLACE;
  }

  if (input_buffer->flags.secure) {
    *mdp_flags |= MDP_LAYER_SECURE_SESSION;
  }

  if (input_buffer->flags.secure_display) {
    *mdp_flags |= MDP_SECURE_DISPLAY_OVERLAY_SESSION;
  }
}

void HWDevice::SyncMerge(const int &fd1, const int &fd2, int *target) {
  if (fd1 >= 0 && fd2 >= 0) {
    buffer_sync_handler_->SyncMerge(fd1, fd2, target);
  } else if (fd1 >= 0) {
    *target = fd1;
  } else if (fd2 >= 0) {
    *target = fd2;
  }
}

int HWDevice::GetFBNodeIndex(HWDeviceType device_type) {
  int fb_node_index = -1;
  for (int i = 0; i <= kDeviceVirtual; i++) {
    HWPanelInfo *panel_info = new HWPanelInfo();
    GetHWPanelInfoByNode(i, panel_info);
    switch (device_type) {
    case kDevicePrimary:
      if (panel_info->is_primary_panel) {
        fb_node_index = i;
      }
      break;
    case kDeviceHDMI:
      if (panel_info->port == kPortDTv) {
        fb_node_index = i;
      }
      break;
    case kDeviceVirtual:
      if (panel_info->port == kPortWriteBack) {
        fb_node_index = i;
      }
      break;
    default:
      break;
    }
  }
  return fb_node_index;
}

void HWDevice::PopulateHWPanelInfo() {
  hw_panel_info_ = HWPanelInfo();
  GetHWPanelInfoByNode(fb_node_index_, &hw_panel_info_);
  DLOGI("Device type = %d, Display Port = %d, Display Mode = %d, Device Node = %d, Is Primary = %d",
        device_type_, hw_panel_info_.port, hw_panel_info_.mode, fb_node_index_,
        hw_panel_info_.is_primary_panel);
  DLOGI("Partial Update = %d, Dynamic FPS = %d",
        hw_panel_info_.partial_update, hw_panel_info_.dynamic_fps);
  DLOGI("Align: left = %d, width = %d, top = %d, height = %d",
        hw_panel_info_.left_align, hw_panel_info_.width_align,
        hw_panel_info_.top_align, hw_panel_info_.height_align);
  DLOGI("ROI: min_width = %d, min_height = %d, need_merge = %d",
        hw_panel_info_.min_roi_width, hw_panel_info_.min_roi_height,
        hw_panel_info_.needs_roi_merge);
  DLOGI("FPS: min = %d, max =%d", hw_panel_info_.min_fps, hw_panel_info_.max_fps);
  DLOGI("Left Split = %d, Right Split = %d", hw_panel_info_.split_info.left_split,
        hw_panel_info_.split_info.right_split);
  DLOGI("Source Split Always = %d", hw_panel_info_.split_info.always_src_split);
}

void HWDevice::GetHWPanelInfoByNode(int device_node, HWPanelInfo *panel_info) {
  if (!panel_info) {
    DLOGE("PanelInfo pointer in invalid.");
    return;
  }
  char stringbuffer[kMaxStringLength];
  FILE *fileptr = NULL;
  snprintf(stringbuffer, sizeof(stringbuffer), "%s%d/msm_fb_panel_info", fb_path_, device_node);
  fileptr = fopen(stringbuffer, "r");
  if (!fileptr) {
    DLOGW("Failed to open msm_fb_panel_info node device node %d", device_node);
    return;
  }

  size_t len = kMaxStringLength;
  ssize_t read;
  char *line = NULL;
  while ((read = getline(&line, &len, fileptr)) != -1) {
    uint32_t token_count = 0;
    const uint32_t max_count = 10;
    char *tokens[max_count] = { NULL };
    if (!ParseLine(line, tokens, max_count, &token_count)) {
      if (!strncmp(tokens[0], "pu_en", strlen("pu_en"))) {
        panel_info->partial_update = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "xstart", strlen("xstart"))) {
        panel_info->left_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "walign", strlen("walign"))) {
        panel_info->width_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "ystart", strlen("ystart"))) {
        panel_info->top_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "halign", strlen("halign"))) {
        panel_info->height_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "min_w", strlen("min_w"))) {
        panel_info->min_roi_width = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "min_h", strlen("min_h"))) {
        panel_info->min_roi_height = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "roi_merge", strlen("roi_merge"))) {
        panel_info->needs_roi_merge = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "dyn_fps_en", strlen("dyn_fps_en"))) {
        panel_info->dynamic_fps = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "min_fps", strlen("min_fps"))) {
        panel_info->min_fps = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_fps", strlen("max_fps"))) {
        panel_info->max_fps = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "primary_panel", strlen("primary_panel"))) {
        panel_info->is_primary_panel = atoi(tokens[1]);
      }
    }
  }
  fclose(fileptr);
  free(line);
  panel_info->port = GetHWDisplayPort(device_node);
  panel_info->mode = GetHWDisplayMode(device_node);
  GetSplitInfo(device_node, panel_info);
}

HWDisplayPort HWDevice::GetHWDisplayPort(int device_node) {
  char stringbuffer[kMaxStringLength];
  DisplayError error = kErrorNone;
  char *line = NULL;
  size_t len = kMaxStringLength;
  ssize_t read;
  HWDisplayPort port = kPortDefault;

  snprintf(stringbuffer, sizeof(stringbuffer), "%s%d/msm_fb_type", fb_path_, device_node);
  FILE *fileptr = fopen(stringbuffer, "r");
  if (!fileptr) {
    DLOGW("File not found %s", stringbuffer);
    return port;
  }
  read = getline(&line, &len, fileptr);
  if (read == -1) {
    fclose(fileptr);
    return port;
  }
  if ((strncmp(line, "mipi dsi cmd panel", strlen("mipi dsi cmd panel")) == 0)) {
    port = kPortDSI;
  } else if ((strncmp(line, "mipi dsi video panel", strlen("mipi dsi video panel")) == 0)) {
    port = kPortDSI;
  } else if ((strncmp(line, "lvds panel", strlen("lvds panel")) == 0)) {
    port = kPortLVDS;
  } else if ((strncmp(line, "edp panel", strlen("edp panel")) == 0)) {
    port = kPortEDP;
  } else if ((strncmp(line, "dtv panel", strlen("dtv panel")) == 0)) {
    port = kPortDTv;
  } else if ((strncmp(line, "writeback panel", strlen("writeback panel")) == 0)) {
    port = kPortWriteBack;
  } else {
    port = kPortDefault;
  }
  fclose(fileptr);
  free(line);
  return port;
}

HWDisplayMode HWDevice::GetHWDisplayMode(int device_node) {
  char stringbuffer[kMaxStringLength];
  DisplayError error = kErrorNone;
  char *line = NULL;
  size_t len = kMaxStringLength;
  ssize_t read;
  HWDisplayMode mode = kModeDefault;

  snprintf(stringbuffer, sizeof(stringbuffer), "%s%d/msm_fb_type", fb_path_, device_node);
  FILE *fileptr = fopen(stringbuffer, "r");
  if (!fileptr) {
    DLOGW("File not found %s", stringbuffer);
    return mode;
  }
  read = getline(&line, &len, fileptr);
  if (read == -1) {
    fclose(fileptr);
    return mode;
  }
  if ((strncmp(line, "mipi dsi cmd panel", strlen("mipi dsi cmd panel")) == 0)) {
    mode = kModeCommand;
  } else if ((strncmp(line, "mipi dsi video panel", strlen("mipi dsi video panel")) == 0)) {
    mode = kModeVideo;
  } else {
    mode = kModeDefault;
  }
  fclose(fileptr);
  free(line);
  return mode;
}

void HWDevice::GetSplitInfo(int device_node, HWPanelInfo *panel_info) {
  char stringbuffer[kMaxStringLength];
  FILE *fileptr = NULL;
  size_t len = kMaxStringLength;
  ssize_t read;
  char *line = NULL;
  uint32_t token_count = 0;
  const uint32_t max_count = 10;
  char *tokens[max_count] = { NULL };

  // Split info - for MDSS Version 5 - No need to check version here
  snprintf(stringbuffer , sizeof(stringbuffer), "%s%d/msm_fb_split", fb_path_, device_node);
  fileptr = fopen(stringbuffer, "r");
  if (!fileptr) {
    DLOGW("File not found %s", stringbuffer);
    return;
  }

  // Format "left right" space as delimiter
  read = getline(&line, &len, fileptr);
  if (read != -1) {
    if (!ParseLine(line, tokens, max_count, &token_count)) {
      panel_info->split_info.left_split = atoi(tokens[0]);
      panel_info->split_info.right_split = atoi(tokens[1]);
    }
  }
  fclose(fileptr);

  // SourceSplit enabled - Get More information
  snprintf(stringbuffer , sizeof(stringbuffer), "%s%d/msm_fb_src_split_info", fb_path_,
           device_node);
  fileptr = fopen(stringbuffer, "r");
  if (!fileptr) {
    DLOGW("File not found %s", stringbuffer);
    return;
  }

  read = getline(&line, &len, fileptr);
  if (read != -1) {
    if (!strncmp(line, "src_split_always", strlen("src_split_always"))) {
      panel_info->split_info.always_src_split = true;
    }
  }
  fclose(fileptr);
  free(line);
}

int HWDevice::ParseLine(char *input, char *tokens[], const uint32_t max_token, uint32_t *count) {
  char *tmp_token = NULL;
  char *temp_ptr;
  uint32_t index = 0;
  const char *delim = ", =\n";
  if (!input) {
    return -1;
  }
  tmp_token = strtok_r(input, delim, &temp_ptr);
  while (tmp_token && index < max_token) {
    tokens[index++] = tmp_token;
    tmp_token = strtok_r(NULL, delim, &temp_ptr);
  }
  *count = index;

  return 0;
}

bool HWDevice::EnableHotPlugDetection(int enable) {
  bool ret_value = true;
  char hpdpath[kMaxStringLength];
  int hdmi_node_index = GetFBNodeIndex(kDeviceHDMI);
  snprintf(hpdpath , sizeof(hpdpath), "%s%d/hpd", fb_path_, hdmi_node_index);
  int hpdfd = open_(hpdpath, O_RDWR, 0);
  if (hpdfd < 0) {
    DLOGE("Open failed = %s", hpdpath);
    return kErrorHardware;
  }
  char value = enable ? '1' : '0';
  ssize_t length = pwrite_(hpdfd, &value, 1, 0);
  if (length <= 0) {
    DLOGE("Write failed 'hpd' = %d", enable);
    ret_value = false;
  }
  close_(hpdfd);

  return ret_value;
}

void HWDevice::ResetDisplayParams() {
  memset(&mdp_disp_commit_, 0, sizeof(mdp_disp_commit_));
  memset(&mdp_in_layers_, 0, sizeof(mdp_in_layers_));
  memset(&mdp_out_layer_, 0, sizeof(mdp_out_layer_));
  memset(&scale_data_, 0, sizeof(scale_data_));

  for (uint32_t i = 0; i < kMaxSDELayers * 2; i++) {
    mdp_in_layers_[i].buffer.fence = -1;
  }

  mdp_disp_commit_.version = MDP_COMMIT_VERSION_1_0;
  mdp_disp_commit_.commit_v1.input_layers = mdp_in_layers_;
  mdp_disp_commit_.commit_v1.output_layer = &mdp_out_layer_;
  mdp_disp_commit_.commit_v1.release_fence = -1;
  mdp_disp_commit_.commit_v1.retire_fence = -1;
}

void HWDevice::SetHWScaleData(const ScaleData &scale, uint32_t index) {
  mdp_scale_data *mdp_scale = GetScaleDataRef(index);
  mdp_scale->enable_pxl_ext = scale.enable_pixel_ext;

  for (int i = 0; i < 4; i++) {
    const HWPlane &plane = scale.plane[i];
    mdp_scale->init_phase_x[i] = plane.init_phase_x;
    mdp_scale->phase_step_x[i] = plane.phase_step_x;
    mdp_scale->init_phase_y[i] = plane.init_phase_y;
    mdp_scale->phase_step_y[i] = plane.phase_step_y;

    mdp_scale->num_ext_pxls_left[i] = plane.left.extension;
    mdp_scale->left_ftch[i] = plane.left.overfetch;
    mdp_scale->left_rpt[i] = plane.left.repeat;

    mdp_scale->num_ext_pxls_top[i] = plane.top.extension;
    mdp_scale->top_ftch[i] = plane.top.overfetch;
    mdp_scale->top_rpt[i] = plane.top.repeat;

    mdp_scale->num_ext_pxls_right[i] = plane.right.extension;
    mdp_scale->right_ftch[i] = plane.right.overfetch;
    mdp_scale->right_rpt[i] = plane.right.repeat;

    mdp_scale->num_ext_pxls_btm[i] = plane.bottom.extension;
    mdp_scale->btm_ftch[i] = plane.bottom.overfetch;
    mdp_scale->btm_rpt[i] = plane.bottom.repeat;

    mdp_scale->roi_w[i] = plane.roi_width;
  }
}

void HWDevice::SetColorSpace(LayerColorSpace source, mdp_color_space *color_space) {
  switch (source) {
  case kLimitedRange601:    *color_space = MDP_CSC_ITU_R_601;      break;
  case kFullRange601:       *color_space = MDP_CSC_ITU_R_601_FR;   break;
  case kLimitedRange709:    *color_space = MDP_CSC_ITU_R_709;      break;
  }
}

}  // namespace sdm

