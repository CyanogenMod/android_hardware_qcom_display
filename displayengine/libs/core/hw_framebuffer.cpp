/*
* Copyright (c) 2014, The Linux Foundation. All rights reserved.
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

// SDE_LOG_TAG definition must precede debug.h include.
#define SDE_LOG_TAG kTagCore
#define SDE_MODULE_NAME "HWFrameBuffer"
#include <utils/debug.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <utils/constants.h>

#include "hw_framebuffer.h"

#define IOCTL_LOGE(ioctl) DLOGE("ioctl %s, errno = %d, desc = %s", #ioctl, errno, strerror(errno))

#ifdef DISPLAY_CORE_VIRTUAL_DRIVER
extern int virtual_ioctl(int fd, int cmd, ...);
extern int virtual_open(const char *file_name, int access, ...);
extern int virtual_close(int fd);
#endif

namespace sde {

HWFrameBuffer::HWFrameBuffer() {
  // Point to actual driver interfaces.
  ioctl_ = ::ioctl;
  open_ = ::open;
  close_ = ::close;

#ifdef DISPLAY_CORE_VIRTUAL_DRIVER
  // If debug property to use virtual driver is set, point to virtual driver interfaces.
  if (Debug::IsVirtualDriver()) {
    ioctl_ = virtual_ioctl;
    open_ = virtual_open;
    close_ = virtual_close;
  }
#endif
}

DisplayError HWFrameBuffer::Init() {
  return kErrorNone;
}

DisplayError HWFrameBuffer::Deinit() {
  return kErrorNone;
}

DisplayError HWFrameBuffer::GetHWCapabilities(HWResourceInfo *hw_res_info) {
  // Hardcode for 8084 for now.
  hw_res_info->mdp_version = 500;
  hw_res_info->hw_revision = 0x10030001;
  hw_res_info->num_dma_pipe = 2;
  hw_res_info->num_vig_pipe = 4;
  hw_res_info->num_rgb_pipe = 4;
  hw_res_info->num_rotator = 2;
  hw_res_info->num_control = 5;
  hw_res_info->num_mixer_to_disp = 4;
  hw_res_info->max_scale_up = 20;
  hw_res_info->max_scale_down = 4;
  hw_res_info->has_non_scalar_rgb = true;
  hw_res_info->is_src_split = true;

  return kErrorNone;
}

DisplayError HWFrameBuffer::Open(HWBlockType type, Handle *device) {
  DisplayError error = kErrorNone;

  HWContext *hw_context = new HWContext();
  if (UNLIKELY(!hw_context)) {
    return kErrorMemory;
  }

  int device_id = 0;
  switch (type) {
  case kHWPrimary:
    device_id = 0;
    break;
  default:
    break;
  }

  char device_name[64] = {0};
  snprintf(device_name, sizeof(device_name), "%s%d", "/dev/graphics/fb", device_id);

  hw_context->device_fd = open_(device_name, O_RDWR);
  if (UNLIKELY(hw_context->device_fd < 0)) {
    DLOGE("open %s failed.", device_name);
    error = kErrorResources;
    delete hw_context;
  }

  *device = hw_context;
  return error;
}

DisplayError HWFrameBuffer::Close(Handle device) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  close_(hw_context->device_fd);
  delete hw_context;

  return kErrorNone;
}

DisplayError HWFrameBuffer::GetNumDeviceAttributes(Handle device, uint32_t *count) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  // TODO(user): Query modes
  *count = 1;

  return kErrorNone;
}

DisplayError HWFrameBuffer::GetDeviceAttributes(Handle device,
                                                HWDeviceAttributes *device_attributes,
                                                uint32_t mode) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  int &device_fd = hw_context->device_fd;

  // TODO(user): Query for respective mode index.

  // Variable screen info
  STRUCT_VAR(fb_var_screeninfo, var_screeninfo);
  if (UNLIKELY(ioctl_(device_fd, FBIOGET_VSCREENINFO, &var_screeninfo) == -1)) {
    IOCTL_LOGE(FBIOGET_VSCREENINFO);
    return kErrorHardware;
  }

  // Frame rate
  STRUCT_VAR(msmfb_metadata, meta_data);
  meta_data.op = metadata_op_frame_rate;
  if (UNLIKELY(ioctl_(device_fd, MSMFB_METADATA_GET, &meta_data) == -1)) {
    IOCTL_LOGE(MSMFB_METADATA_GET);
    return kErrorHardware;
  }

  // If driver doesn't return width/height information, default to 160 dpi
  if (INT(var_screeninfo.width) <= 0 || INT(var_screeninfo.height) <= 0) {
    var_screeninfo.width  = INT((FLOAT(var_screeninfo.xres) * 25.4f)/160.0f + 0.5f);
    var_screeninfo.height = INT((FLOAT(var_screeninfo.yres) * 25.4f)/160.0f + 0.5f);
  }

  device_attributes->x_pixels = var_screeninfo.xres;
  device_attributes->y_pixels = var_screeninfo.yres;
  device_attributes->x_dpi = (FLOAT(var_screeninfo.xres) * 25.4f) / FLOAT(var_screeninfo.width);
  device_attributes->y_dpi = (FLOAT(var_screeninfo.yres) * 25.4f) / FLOAT(var_screeninfo.height);
  device_attributes->vsync_period_ns = UINT32(1000000000L / FLOAT(meta_data.data.panel_frame_rate));

  // TODO(user): set panel information from sysfs
  device_attributes->is_device_split = true;
  device_attributes->split_left = device_attributes->x_pixels / 2;

  return kErrorNone;
}

DisplayError HWFrameBuffer::PowerOn(Handle device) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  if (UNLIKELY(ioctl_(hw_context->device_fd, FBIOBLANK, FB_BLANK_UNBLANK) == -1)) {
    IOCTL_LOGE(FB_BLANK_UNBLANK);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::PowerOff(Handle device) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  if (UNLIKELY(ioctl_(hw_context->device_fd, FBIOBLANK, FB_BLANK_POWERDOWN) == -1)) {
    IOCTL_LOGE(FB_BLANK_POWERDOWN);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::Doze(Handle device) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  return kErrorNone;
}

DisplayError HWFrameBuffer::Standby(Handle device) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  return kErrorNone;
}

DisplayError HWFrameBuffer::Validate(Handle device, HWLayers *hw_layers) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  return kErrorNone;
}

DisplayError HWFrameBuffer::Commit(Handle device, HWLayers *hw_layers) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  HWLayersInfo &hw_layer_info = hw_layers->info;

  // Assuming left & right both pipe are required, maximum possible number of overlays.
  uint32_t max_overlay_count = hw_layer_info.count * 2;

  int acquire_fences[hw_layer_info.count];  // NOLINT
  int release_fence = -1;
  int retire_fence = -1;
  uint32_t acquire_fence_count = 0;
  STRUCT_VAR_ARRAY(mdp_overlay, overlay_array, max_overlay_count);
  STRUCT_VAR_ARRAY(msmfb_overlay_data, data_array, max_overlay_count);

  LayerStack *stack = hw_layer_info.stack;
  uint32_t num_overlays = 0;
  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    uint32_t layer_index = hw_layer_info.index[i];
    Layer &layer = stack->layers[layer_index];
    LayerBuffer *input_buffer = layer.input_buffer;
    HWLayerConfig &config = hw_layers->config[i];
    HWPipeInfo &left_pipe = config.left_pipe;

    // Configure left pipe
    mdp_overlay &left_overlay = overlay_array[num_overlays];
    msmfb_overlay_data &left_data = data_array[num_overlays];

    left_overlay.id = left_pipe.pipe_id;
    left_overlay.flags |= MDP_BLEND_FG_PREMULT;
    left_overlay.transp_mask = 0xffffffff;
    left_overlay.z_order = i;
    left_overlay.alpha = layer.plane_alpha;
    left_overlay.src.width = input_buffer->planes[0].stride;
    left_overlay.src.height = input_buffer->height;
    SetBlending(&left_overlay.blend_op, layer.blending);
    SetFormat(&left_overlay.src.format, layer.input_buffer->format);
    SetRect(&left_overlay.src_rect, left_pipe.src_roi);
    SetRect(&left_overlay.dst_rect, left_pipe.dst_roi);
    left_data.id = left_pipe.pipe_id;
    left_data.data.memory_id = input_buffer->planes[0].fd;
    left_data.data.offset = input_buffer->planes[0].offset;

    num_overlays++;

    // Configure right pipe
    if (config.is_right_pipe) {
      HWPipeInfo &right_pipe = config.right_pipe;
      mdp_overlay &right_overlay = overlay_array[num_overlays];
      msmfb_overlay_data &right_data = data_array[num_overlays];

      right_overlay = left_overlay;
      right_data = left_data;
      right_overlay.id = right_pipe.pipe_id;
      right_data.id = right_pipe.pipe_id;
      SetRect(&right_overlay.src_rect, right_pipe.src_roi);
      SetRect(&right_overlay.dst_rect, right_pipe.dst_roi);

      num_overlays++;
    }

    if (input_buffer->acquire_fence_fd >= 0) {
      acquire_fences[acquire_fence_count] = input_buffer->acquire_fence_fd;
      acquire_fence_count++;
    }
  }

  mdp_overlay *overlay_list[num_overlays];
  msmfb_overlay_data *data_list[num_overlays];
  for (uint32_t i = 0; i < num_overlays; i++) {
    overlay_list[i] = &overlay_array[i];
    data_list[i] = &data_array[i];
  }

  // TODO(user): Replace with Atomic commit call.
  STRUCT_VAR(mdp_atomic_commit, atomic_commit);
  atomic_commit.overlay_list = overlay_list;
  atomic_commit.data_list = data_list;
  atomic_commit.num_overlays = num_overlays;
  atomic_commit.buf_sync.acq_fen_fd = acquire_fences;
  atomic_commit.buf_sync.acq_fen_fd_cnt = acquire_fence_count;
  atomic_commit.buf_sync.rel_fen_fd = &release_fence;
  atomic_commit.buf_sync.retire_fen_fd = &retire_fence;
  atomic_commit.buf_sync.flags = MDP_BUF_SYNC_FLAG_RETIRE_FENCE;

  if (UNLIKELY(ioctl_(hw_context->device_fd, MSMFB_ATOMIC_COMMIT, &atomic_commit) == -1)) {
    IOCTL_LOGE(MSMFB_ATOMIC_COMMIT);
    return kErrorHardware;
  }

  // MDP returns only one release fence for the entire layer stack. Duplicate this fence into all
  // layers being composed by MDP.
  stack->retire_fence_fd = retire_fence;
  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    uint32_t layer_index = hw_layer_info.index[i];
    Layer &layer = stack->layers[layer_index];
    LayerBuffer *input_buffer = layer.input_buffer;
    input_buffer->release_fence_fd = dup(release_fence);
  }
  close(release_fence);

  return kErrorNone;
}

void HWFrameBuffer::SetFormat(uint32_t *target, const LayerBufferFormat &source) {
  switch (source) {
  default:
    *target = MDP_RGBA_8888;
    break;
  }
}

void HWFrameBuffer::SetBlending(uint32_t *target, const LayerBlending &source) {
  switch (source) {
  case kBlendingPremultiplied:
    *target = BLEND_OP_PREMULTIPLIED;
    break;

  case kBlendingCoverage:
    *target = BLEND_OP_COVERAGE;
    break;

  default:
    *target = BLEND_OP_NOT_DEFINED;
    break;
  }
}

void HWFrameBuffer::SetRect(mdp_rect *target, const LayerRect &source) {
  target->x = INT(ceilf(source.left));
  target->y = INT(ceilf(source.top));
  target->w = INT(floorf(source.right)) - target->x;
  target->h = INT(floorf(source.bottom)) - target->y;
}

}  // namespace sde

