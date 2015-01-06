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

#define __STDC_FORMAT_MACROS
#include <ctype.h>
#include <math.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <pthread.h>
#include <utils/constants.h>
#include <utils/debug.h>

#include "hw_framebuffer.h"

#define __CLASS__ "HWFrameBuffer"

#define IOCTL_LOGE(ioctl, type) DLOGE("ioctl %s, display = %d errno = %d, desc = %s", #ioctl, \
                                      type, errno, strerror(errno))

#ifdef DISPLAY_CORE_VIRTUAL_DRIVER
extern int virtual_ioctl(int fd, int cmd, ...);
extern int virtual_open(const char *file_name, int access, ...);
extern int virtual_close(int fd);
extern int virtual_poll(struct pollfd *fds,  nfds_t num, int timeout);
extern ssize_t virtual_pread(int fd, void *data, size_t count, off_t offset);
extern FILE* virtual_fopen(const char *fname, const char *mode);
extern int virtual_fclose(FILE* fileptr);
extern ssize_t virtual_getline(char **lineptr, size_t *linelen, FILE *stream);
extern ssize_t virtual_read(int fd, void *buf, size_t count);
extern ssize_t virtual_write(int fd, const void *buf, size_t count);

#endif

namespace sde {

HWFrameBuffer::HWFrameBuffer() : event_thread_name_("SDE_EventThread"), fake_vsync_(false),
                                 exit_threads_(false), fb_path_("/sys/devices/virtual/graphics/fb"),
                                 hotplug_enabled_(false) {
  // Pointer to actual driver interfaces.
  ioctl_ = ::ioctl;
  open_ = ::open;
  close_ = ::close;
  poll_ = ::poll;
  pread_ = ::pread;
  fopen_ = ::fopen;
  fclose_ = ::fclose;
  getline_ = ::getline;
  read_ = ::read;
  write_ = ::write;

#ifdef DISPLAY_CORE_VIRTUAL_DRIVER
  // If debug property to use virtual driver is set, point to virtual driver interfaces.
  if (Debug::IsVirtualDriver()) {
    ioctl_ = virtual_ioctl;
    open_ = virtual_open;
    close_ = virtual_close;
    poll_ = virtual_poll;
    pread_ = virtual_pread;
    fopen_ = virtual_fopen;
    fclose_ = virtual_fclose;
    getline_ = virtual_getline;
    read_ = virtual_read;
    write_ = virtual_write;
  }
#endif
  for (int i = 0; i < kDeviceMax; i++) {
    fb_node_index_[i] = -1;
  }
}

DisplayError HWFrameBuffer::Init() {
  DisplayError error = kErrorNone;
  char node_path[kMaxStringLength] = {0};
  char data[kMaxStringLength] = {0};
  const char* event_name[kNumDisplayEvents] = {"vsync_event", "show_blank_event"};

  // Read the fb node index
  PopulateFBNodeIndex();
  if (fb_node_index_[kHWPrimary] == -1) {
    DLOGE("HW Display Device Primary should be present");
    error = kErrorHardware;
    goto CleanupOnError;
  }

  // Populate Primary Panel Info(Used for Partial Update)
  PopulatePanelInfo(fb_node_index_[kHWPrimary]);
  // Populate HW Capabilities
  error = PopulateHWCapabilities();
  if (error != kErrorNone) {
    goto CleanupOnError;
  }

  // Open nodes for polling
  for (int display = 0; display < kNumPhysicalDisplays; display++) {
    for (int event = 0; event < kNumDisplayEvents; event++) {
      poll_fds_[display][event].fd = -1;
    }
  }

  if (!fake_vsync_) {
    for (int display = 0; display < kNumPhysicalDisplays; display++) {
      for (int event = 0; event < kNumDisplayEvents; event++) {
        pollfd &poll_fd = poll_fds_[display][event];

        snprintf(node_path, sizeof(node_path), "%s%d/%s", fb_path_, fb_node_index_[display],
                 event_name[event]);

        poll_fd.fd = open_(node_path, O_RDONLY);
        if (poll_fd.fd < 0) {
          DLOGE("open failed for display=%d event=%d, error=%s", display, event, strerror(errno));
          error = kErrorHardware;
          goto CleanupOnError;
        }

        // Read once on all fds to clear data on all fds.
        pread_(poll_fd.fd, data , kMaxStringLength, 0);
        poll_fd.events = POLLPRI | POLLERR;
      }
    }
  }

  // Mode look-up table for HDMI
  supported_video_modes_ = new msm_hdmi_mode_timing_info[HDMI_VFRMT_MAX];
  if (!supported_video_modes_) {
    error = kErrorMemory;
    goto CleanupOnError;
  }
  // Populate the mode table for supported modes
  MSM_HDMI_MODES_INIT_TIMINGS(supported_video_modes_);
  MSM_HDMI_MODES_SET_SUPP_TIMINGS(supported_video_modes_, MSM_HDMI_MODES_ALL);

  // Start the Event thread
  if (pthread_create(&event_thread_, NULL, &DisplayEventThread, this) < 0) {
    DLOGE("Failed to start %s, error = %s", event_thread_name_);
    error = kErrorResources;
    goto CleanupOnError;
  }

  return kErrorNone;

CleanupOnError:
  // Close all poll fds
  for (int display = 0; display < kNumPhysicalDisplays; display++) {
    for (int event = 0; event < kNumDisplayEvents; event++) {
      int &fd = poll_fds_[display][event].fd;
      if (fd >= 0) {
        close_(fd);
      }
    }
  }
  if (supported_video_modes_) {
    delete supported_video_modes_;
  }

  return error;
}

DisplayError HWFrameBuffer::Deinit() {
  exit_threads_ = true;
  pthread_join(event_thread_, NULL);

  for (int display = 0; display < kNumPhysicalDisplays; display++) {
    for (int event = 0; event < kNumDisplayEvents; event++) {
      close(poll_fds_[display][event].fd);
    }
  }
  if (supported_video_modes_) {
    delete supported_video_modes_;
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::GetHWCapabilities(HWResourceInfo *hw_res_info) {
  *hw_res_info = hw_resource_;

  return kErrorNone;
}

DisplayError HWFrameBuffer::Open(HWDeviceType type, Handle *device, HWEventHandler* eventhandler) {
  DisplayError error = kErrorNone;

  HWContext *hw_context = new HWContext();
  if (!hw_context) {
    return kErrorMemory;
  }

  char device_name[64] = {0};

  switch (type) {
  case kDevicePrimary:
  case kDeviceHDMI:
    // Store EventHandlers for two Physical displays, i.e., Primary and HDMI
    // TODO(user): Need to revisit for HDMI as Primary usecase
    event_handler_[type] = eventhandler;
  case kDeviceVirtual:
    snprintf(device_name, sizeof(device_name), "%s%d", "/dev/graphics/fb", fb_node_index_[type]);
    break;
  default:
    break;
  }

  hw_context->device_fd = open_(device_name, O_RDWR);
  if (hw_context->device_fd < 0) {
    DLOGE("open %s failed.", device_name);
    error = kErrorResources;
    delete hw_context;
  }
  hw_context->type = type;

  *device = hw_context;

  return error;
}

DisplayError HWFrameBuffer::Close(Handle device) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  switch (hw_context->type) {
  case kDevicePrimary:
    break;
  case kDeviceHDMI:
    hdmi_mode_count_ = 0;
    break;
  default:
    break;
  }

  if (hw_context->device_fd > 0) {
    close_(hw_context->device_fd);
  }
  delete hw_context;

  return kErrorNone;
}

DisplayError HWFrameBuffer::GetNumDisplayAttributes(Handle device, uint32_t *count) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  switch (hw_context->type) {
  case kDevicePrimary:
    *count = 1;
    break;
  case kDeviceHDMI:
    *count = GetHDMIModeCount();
    if (*count <= 0) {
      return kErrorHardware;
    }
    break;
  default:
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::GetDisplayAttributes(Handle device,
                                                 HWDisplayAttributes *display_attributes,
                                                 uint32_t index) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  int &device_fd = hw_context->device_fd;
  // Variable screen info
  STRUCT_VAR(fb_var_screeninfo, var_screeninfo);

  switch (hw_context->type) {
  case kDevicePrimary:
    {
      if (ioctl_(device_fd, FBIOGET_VSCREENINFO, &var_screeninfo) == -1) {
        IOCTL_LOGE(FBIOGET_VSCREENINFO, hw_context->type);
        return kErrorHardware;
      }

      // Frame rate
      STRUCT_VAR(msmfb_metadata, meta_data);
      meta_data.op = metadata_op_frame_rate;
      if (ioctl_(device_fd, MSMFB_METADATA_GET, &meta_data) == -1) {
        IOCTL_LOGE(MSMFB_METADATA_GET, hw_context->type);
        return kErrorHardware;
      }

      // If driver doesn't return width/height information, default to 160 dpi
      if (INT(var_screeninfo.width) <= 0 || INT(var_screeninfo.height) <= 0) {
        var_screeninfo.width  = INT(((FLOAT(var_screeninfo.xres) * 25.4f)/160.0f) + 0.5f);
        var_screeninfo.height = INT(((FLOAT(var_screeninfo.yres) * 25.4f)/160.0f) + 0.5f);
      }

      display_attributes->x_pixels = var_screeninfo.xres;
      display_attributes->y_pixels = var_screeninfo.yres;
      display_attributes->v_total = var_screeninfo.yres + var_screeninfo.lower_margin +
          var_screeninfo.upper_margin + var_screeninfo.vsync_len;
      display_attributes->x_dpi =
          (FLOAT(var_screeninfo.xres) * 25.4f) / FLOAT(var_screeninfo.width);
      display_attributes->y_dpi =
          (FLOAT(var_screeninfo.yres) * 25.4f) / FLOAT(var_screeninfo.height);
      display_attributes->fps = FLOAT(meta_data.data.panel_frame_rate);
      display_attributes->vsync_period_ns = UINT32(1000000000L / display_attributes->fps);
      display_attributes->is_device_split = (hw_resource_.split_info.left_split ||
          (var_screeninfo.xres > hw_resource_.max_mixer_width)) ? true : false;
      display_attributes->split_left = hw_resource_.split_info.left_split ?
          hw_resource_.split_info.left_split : display_attributes->x_pixels / 2;
    }
    break;

  case kDeviceHDMI:
    {
      // Get the resolution info from the look up table
      msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[0];
      for (int i = 0; i < HDMI_VFRMT_MAX; i++) {
        msm_hdmi_mode_timing_info *cur = &supported_video_modes_[i];
        if (cur->video_format == hdmi_modes_[index]) {
          timing_mode = cur;
          break;
        }
      }
      display_attributes->x_pixels = timing_mode->active_h;
      display_attributes->y_pixels = timing_mode->active_v;
      display_attributes->v_total = timing_mode->active_v + timing_mode->front_porch_v +
          timing_mode->back_porch_v + timing_mode->pulse_width_v;
      display_attributes->x_dpi = 0;
      display_attributes->y_dpi = 0;
      display_attributes->fps = FLOAT(timing_mode->refresh_rate) / 1000.0f;
      display_attributes->vsync_period_ns = UINT32(1000000000L / display_attributes->fps);
      display_attributes->split_left = display_attributes->x_pixels;
      if (display_attributes->x_pixels > hw_resource_.max_mixer_width) {
        display_attributes->is_device_split = true;
        display_attributes->split_left = display_attributes->x_pixels / 2;
      }
    }
    break;

  default:
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::SetDisplayAttributes(Handle device, uint32_t index) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  DisplayError error = kErrorNone;

  switch (hw_context->type) {
  case kDevicePrimary:
    break;

  case kDeviceHDMI:
    {
      // Variable screen info
      STRUCT_VAR(fb_var_screeninfo, vscreeninfo);
      if (ioctl_(hw_context->device_fd, FBIOGET_VSCREENINFO, &vscreeninfo) == -1) {
        IOCTL_LOGE(FBIOGET_VSCREENINFO, hw_context->type);
        return kErrorHardware;
      }

      DLOGI("GetInfo<Mode=%d %dx%d (%d,%d,%d),(%d,%d,%d) %dMHz>", vscreeninfo.reserved[3],
            vscreeninfo.xres, vscreeninfo.yres, vscreeninfo.right_margin, vscreeninfo.hsync_len,
            vscreeninfo.left_margin, vscreeninfo.lower_margin, vscreeninfo.vsync_len,
            vscreeninfo.upper_margin, vscreeninfo.pixclock/1000000);

      msm_hdmi_mode_timing_info *timing_mode = &supported_video_modes_[0];
      for (int i = 0; i < HDMI_VFRMT_MAX; i++) {
        msm_hdmi_mode_timing_info *cur = &supported_video_modes_[i];
        if (cur->video_format == hdmi_modes_[index]) {
          timing_mode = cur;
          break;
        }
      }
      if (MapHDMIDisplayTiming(timing_mode, &vscreeninfo) == false) {
        return kErrorParameters;
      }
      STRUCT_VAR(msmfb_metadata, metadata);
      memset(&metadata, 0 , sizeof(metadata));
      metadata.op = metadata_op_vic;
      metadata.data.video_info_code = timing_mode->video_format;
      if (ioctl(hw_context->device_fd, MSMFB_METADATA_SET, &metadata) == -1) {
        IOCTL_LOGE(MSMFB_METADATA_SET, hw_context->type);
        return kErrorHardware;
      }
      DLOGI("SetInfo<Mode=%d %dx%d (%d,%d,%d),(%d,%d,%d) %dMHz>", vscreeninfo.reserved[3] & 0xFF00,
            vscreeninfo.xres, vscreeninfo.yres, vscreeninfo.right_margin, vscreeninfo.hsync_len,
            vscreeninfo.left_margin, vscreeninfo.lower_margin, vscreeninfo.vsync_len,
            vscreeninfo.upper_margin, vscreeninfo.pixclock/1000000);

      vscreeninfo.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_ALL | FB_ACTIVATE_FORCE;
      if (ioctl_(hw_context->device_fd, FBIOPUT_VSCREENINFO, &vscreeninfo) == -1) {
        IOCTL_LOGE(FBIOGET_VSCREENINFO, hw_context->type);
        return kErrorHardware;
      }
    }
    break;

  default:
    return kErrorParameters;
  }

  return error;
}

DisplayError HWFrameBuffer::PowerOn(Handle device) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  if (ioctl_(hw_context->device_fd, FBIOBLANK, FB_BLANK_UNBLANK) == -1) {
    IOCTL_LOGE(FB_BLANK_UNBLANK, hw_context->type);
    return kErrorHardware;
  }

  // Need to turn on HPD
  if (!hotplug_enabled_) {
    hotplug_enabled_ = EnableHotPlugDetection(1);
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::PowerOff(Handle device) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  if (ioctl_(hw_context->device_fd, FBIOBLANK, FB_BLANK_POWERDOWN) == -1) {
    IOCTL_LOGE(FB_BLANK_POWERDOWN, hw_context->type);
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

DisplayError HWFrameBuffer::SetVSyncState(Handle device, bool enable) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  int vsync_on = enable ? 1 : 0;
  if (ioctl_(hw_context->device_fd, MSMFB_OVERLAY_VSYNC_CTRL, &vsync_on) == -1) {
    IOCTL_LOGE(MSMFB_OVERLAY_VSYNC_CTRL, hw_context->type);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::Validate(Handle device, HWLayers *hw_layers) {
  DisplayError error = kErrorNone;
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  hw_context->ResetMDPCommit();

  HWLayersInfo &hw_layer_info = hw_layers->info;
  LayerStack *stack = hw_layer_info.stack;

  mdp_layer_commit_v1 &mdp_commit = hw_context->mdp_commit.commit_v1;
  mdp_input_layer *mdp_layers = hw_context->mdp_layers;
  uint32_t &mdp_layer_count = mdp_commit.input_layer_cnt;

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    uint32_t layer_index = hw_layer_info.index[i];
    Layer &layer = stack->layers[layer_index];
    LayerBuffer *input_buffer = layer.input_buffer;
    HWLayerConfig &config = hw_layers->config[i];

    uint32_t split_count = hw_layers->config[i].is_right_pipe ? 2 : 1;
    for (uint32_t j = 0; j < split_count; j++) {
      HWPipeInfo &pipe = (j == 0) ? config.left_pipe : config.right_pipe;
      mdp_input_layer &mdp_layer = mdp_layers[mdp_layer_count];
      mdp_layer.alpha = layer.plane_alpha;
      mdp_layer.z_order = static_cast<uint16_t>(i);
      mdp_layer.transp_mask = 0xffffffff;
      SetBlending(layer.blending, &mdp_layer.blend_op);
      SetRect(pipe.src_roi, &mdp_layer.src_rect);
      SetRect(pipe.dst_roi, &mdp_layer.dst_rect);

      mdp_layer.pipe_ndx = pipe.pipe_id;

      mdp_layer_buffer &mdp_buffer_left = mdp_layer.buffer;
      mdp_buffer_left.width = input_buffer->width;
      mdp_buffer_left.height = input_buffer->height;

      error = SetFormat(layer.input_buffer->format, &mdp_buffer_left.format);
      if (error != kErrorNone) {
        return error;
      }
      mdp_layer_count++;
    }
  }

  mdp_commit.flags |= MDP_VALIDATE_LAYER;
  if (ioctl_(hw_context->device_fd, MSMFB_ATOMIC_COMMIT, &hw_context->mdp_commit) == -1) {
    IOCTL_LOGE(MSMFB_ATOMIC_COMMIT, hw_context->type);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::Commit(Handle device, HWLayers *hw_layers) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  HWLayersInfo &hw_layer_info = hw_layers->info;
  LayerStack *stack = hw_layer_info.stack;

  mdp_layer_commit_v1 &mdp_commit = hw_context->mdp_commit.commit_v1;
  mdp_input_layer *mdp_layers = hw_context->mdp_layers;
  uint32_t mdp_layer_index = 0;

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    uint32_t layer_index = hw_layer_info.index[i];
    LayerBuffer *input_buffer = stack->layers[layer_index].input_buffer;

    uint32_t split_count = hw_layers->config[i].is_right_pipe ? 2 : 1;
    for (uint32_t j = 0; j < split_count; j++) {
      mdp_layer_buffer &mdp_buffer = mdp_layers[mdp_layer_index].buffer;

      if (input_buffer->planes[0].fd >= 0) {
        mdp_buffer.plane_count = 1;
        mdp_buffer.planes[0].fd = input_buffer->planes[0].fd;
        mdp_buffer.planes[0].offset = input_buffer->planes[0].offset;
        mdp_buffer.planes[0].stride = input_buffer->planes[0].stride;
      } else {
        DLOGW("Invalid buffer fd, setting plane count to 0");
        mdp_buffer.plane_count = 0;
      }

      mdp_buffer.fence = input_buffer->acquire_fence_fd;
      mdp_layer_index++;
    }
  }

  mdp_commit.flags |= MDP_COMMIT_RETIRE_FENCE;
  mdp_commit.flags &= ~MDP_VALIDATE_LAYER;
  if (ioctl_(hw_context->device_fd, MSMFB_ATOMIC_COMMIT, &hw_context->mdp_commit) == -1) {
    IOCTL_LOGE(MSMFB_ATOMIC_COMMIT, hw_context->type);
    return kErrorHardware;
  }

  // MDP returns only one release fence for the entire layer stack. Duplicate this fence into all
  // layers being composed by MDP.
  stack->retire_fence_fd = mdp_commit.retire_fence;
  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    uint32_t layer_index = hw_layer_info.index[i];
    LayerBuffer *input_buffer = stack->layers[layer_index].input_buffer;

    input_buffer->release_fence_fd = dup(mdp_commit.release_fence);
  }
  close(mdp_commit.release_fence);

  return kErrorNone;
}

DisplayError HWFrameBuffer::SetFormat(const LayerBufferFormat &source, uint32_t *target) {
  switch (source) {
  case kFormatARGB8888:                 *target = MDP_ARGB_8888;         break;
  case kFormatRGBA8888:                 *target = MDP_RGBA_8888;         break;
  case kFormatBGRA8888:                 *target = MDP_BGRA_8888;         break;
  case kFormatRGBX8888:                 *target = MDP_RGBX_8888;         break;
  case kFormatBGRX8888:                 *target = MDP_BGRX_8888;         break;
  case kFormatRGB888:                   *target = MDP_RGB_888;           break;
  case kFormatRGB565:                   *target = MDP_RGB_565;           break;
  case kFormatYCbCr420Planar:           *target = MDP_Y_CB_CR_H2V2;      break;
  case kFormatYCrCb420Planar:           *target = MDP_Y_CR_CB_H2V2;      break;
  case kFormatYCbCr420SemiPlanar:       *target = MDP_Y_CBCR_H2V2;       break;
  case kFormatYCrCb420SemiPlanar:       *target = MDP_Y_CRCB_H2V2;       break;
  case kFormatYCbCr422Packed:           *target = MDP_YCBYCR_H2V1;       break;
  case kFormatYCbCr420SemiPlanarVenus:  *target = MDP_Y_CBCR_H2V2_VENUS; break;
  default:
    DLOGE("Unsupported format type %d", source);
    return kErrorParameters;
  }

  return kErrorNone;
}

void HWFrameBuffer::SetBlending(const LayerBlending &source, mdss_mdp_blend_op *target) {
  switch (source) {
  case kBlendingPremultiplied:  *target = BLEND_OP_PREMULTIPLIED;   break;
  case kBlendingCoverage:       *target = BLEND_OP_COVERAGE;        break;
  default:                      *target = BLEND_OP_NOT_DEFINED;     break;
  }
}

void HWFrameBuffer::SetRect(const LayerRect &source, mdp_rect *target) {
  target->x = INT(ceilf(source.left));
  target->y = INT(ceilf(source.top));
  target->w = INT(floorf(source.right)) - target->x;
  target->h = INT(floorf(source.bottom)) - target->y;
}

void* HWFrameBuffer::DisplayEventThread(void *context) {
  if (context) {
    return reinterpret_cast<HWFrameBuffer *>(context)->DisplayEventThreadHandler();
  }

  return NULL;
}

void* HWFrameBuffer::DisplayEventThreadHandler() {
  char data[kMaxStringLength] = {0};

  prctl(PR_SET_NAME, event_thread_name_, 0, 0, 0);
  setpriority(PRIO_PROCESS, 0, kThreadPriorityUrgent);

  if (fake_vsync_) {
    while (!exit_threads_) {
      // Fake vsync is used only when set explicitly through a property(todo) or when
      // the vsync timestamp node cannot be opened at bootup. There is no
      // fallback to fake vsync from the true vsync loop, ever, as the
      // condition can easily escape detection.
      // Also, fake vsync is delivered only for the primary display.
      usleep(16666);
      STRUCT_VAR(timeval, time_now);
      gettimeofday(&time_now, NULL);
      uint64_t ts = uint64_t(time_now.tv_sec)*1000000000LL +uint64_t(time_now.tv_usec)*1000LL;

      // Send Vsync event for primary display(0)
      event_handler_[kDevicePrimary]->VSync(ts);
    }

    pthread_exit(0);
  }

  typedef void (HWFrameBuffer::*EventHandler)(int, char *);
  EventHandler event_handler[kNumDisplayEvents] = { &HWFrameBuffer::HandleVSync,
                                                    &HWFrameBuffer::HandleBlank };

  while (!exit_threads_) {
    int error = poll_(poll_fds_[0], kNumPhysicalDisplays * kNumDisplayEvents, -1);
    if (error < 0) {
      DLOGW("poll failed. error = %s", strerror(errno));
      continue;
    }

    for (int display = 0; display < kNumPhysicalDisplays; display++) {
      for (int event = 0; event < kNumDisplayEvents; event++) {
        pollfd &poll_fd = poll_fds_[display][event];

        if (poll_fd.revents & POLLPRI) {
          ssize_t length = pread_(poll_fd.fd, data, kMaxStringLength, 0);
          if (length < 0) {
            // If the read was interrupted - it is not a fatal error, just continue.
            DLOGW("pread failed. event = %d, display = %d, error = %s",
                                                      event, display, strerror(errno));
            continue;
          }

          (this->*event_handler[event])(display, data);
        }
      }
    }
  }

  pthread_exit(0);

  return NULL;
}

void HWFrameBuffer::HandleVSync(int display_id, char *data) {
  int64_t timestamp = 0;
  if (!strncmp(data, "VSYNC=", strlen("VSYNC="))) {
    timestamp = strtoull(data + strlen("VSYNC="), NULL, 0);
  }
  event_handler_[display_id]->VSync(timestamp);
}

void HWFrameBuffer::HandleBlank(int display_id, char *data) {
  // TODO(user): Need to send blank Event
}

void HWFrameBuffer::PopulateFBNodeIndex() {
  char stringbuffer[kMaxStringLength];
  DisplayError error = kErrorNone;
  char *line = stringbuffer;
  size_t len = kMaxStringLength;
  ssize_t read;


  for (int i = 0; i < kDeviceMax; i++) {
    snprintf(stringbuffer, sizeof(stringbuffer), "%s%d/msm_fb_type", fb_path_, i);
    FILE* fileptr = fopen_(stringbuffer, "r");
    if (fileptr == NULL) {
      DLOGW("File not found %s", stringbuffer);
      continue;
    }
    read = getline_(&line, &len, fileptr);
    if (read ==-1) {
      fclose_(fileptr);
      continue;
    }
    // TODO(user): For now, assume primary to be cmd/video/lvds/edp mode panel only
    // Need more concrete info from driver
    if ((strncmp(line, "mipi dsi cmd panel", strlen("mipi dsi cmd panel")) == 0)) {
      pri_panel_info_.type = kCommandModePanel;
      fb_node_index_[kDevicePrimary] = i;
    } else if ((strncmp(line, "mipi dsi video panel", strlen("mipi dsi video panel")) == 0))  {
      pri_panel_info_.type = kVideoModePanel;
      fb_node_index_[kDevicePrimary] = i;
    } else if ((strncmp(line, "lvds panel", strlen("lvds panel")) == 0)) {
      pri_panel_info_.type = kLVDSPanel;
      fb_node_index_[kDevicePrimary] = i;
    } else if ((strncmp(line, "edp panel", strlen("edp panel")) == 0)) {
      pri_panel_info_.type = kEDPPanel;
      fb_node_index_[kDevicePrimary] = i;
    } else if ((strncmp(line, "dtv panel", strlen("dtv panel")) == 0)) {
      fb_node_index_[kDeviceHDMI] = i;
    } else if ((strncmp(line, "writeback panel", strlen("writeback panel")) == 0)) {
      fb_node_index_[kDeviceVirtual] = i;
    } else {
      DLOGW("Unknown panel type = %s index = %d", line, i);
    }
    fclose_(fileptr);
  }
}

void HWFrameBuffer::PopulatePanelInfo(int fb_index) {
  char stringbuffer[kMaxStringLength];
  FILE* fileptr = NULL;
  snprintf(stringbuffer, sizeof(stringbuffer), "%s%d/msm_fb_panel_info", fb_path_, fb_index);
  fileptr = fopen_(stringbuffer, "r");
  if (fileptr == NULL) {
    DLOGW("Failed to open msm_fb_panel_info node");
    return;
  }

  size_t len = kMaxStringLength;
  ssize_t read;
  char *line = stringbuffer;
  while ((read = getline_(&line, &len, fileptr)) != -1) {
    uint32_t token_count = 0;
    const uint32_t max_count = 10;
    char *tokens[max_count] = { NULL };
    if (!ParseLine(line, tokens, max_count, &token_count)) {
      if (!strncmp(tokens[0], "pu_en", strlen("pu_en"))) {
        pri_panel_info_.partial_update = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "xstart", strlen("xstart"))) {
        pri_panel_info_.left_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "walign", strlen("walign"))) {
        pri_panel_info_.width_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "ystart", strlen("ystart"))) {
        pri_panel_info_.top_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "halign", strlen("halign"))) {
        pri_panel_info_.height_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "min_w", strlen("min_w"))) {
        pri_panel_info_.min_roi_width = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "min_h", strlen("min_h"))) {
        pri_panel_info_.min_roi_height = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "roi_merge", strlen("roi_merge"))) {
        pri_panel_info_.needs_roi_merge = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "dynamic_fps_en", strlen("dyn_fps_en"))) {
        pri_panel_info_.dynamic_fps = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "min_fps", strlen("min_fps"))) {
        pri_panel_info_.min_fps = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_fps", strlen("max_fps"))) {
        pri_panel_info_.max_fps= atoi(tokens[1]);
      }
    }
  }
  fclose_(fileptr);
}

// Get SDE HWCapabalities from the sysfs
DisplayError HWFrameBuffer::PopulateHWCapabilities() {
  DisplayError error = kErrorNone;
  FILE *fileptr = NULL;
  char stringbuffer[kMaxStringLength];
  uint32_t token_count = 0;
  const uint32_t max_count = 10;
  char *tokens[max_count] = { NULL };
  snprintf(stringbuffer , sizeof(stringbuffer), "%s%d/mdp/caps", fb_path_,
           fb_node_index_[kHWPrimary]);
  fileptr = fopen_(stringbuffer, "rb");

  if (fileptr == NULL) {
    DLOGE("File '%s' not found", stringbuffer);
    return kErrorHardware;
  }

  size_t len = kMaxStringLength;
  ssize_t read;
  char *line = stringbuffer;
  hw_resource_.hw_version = kHWMdssVersion5;
  while ((read = getline_(&line, &len, fileptr)) != -1) {
    // parse the line and update information accordingly
    if (!ParseLine(line, tokens, max_count, &token_count)) {
      if (!strncmp(tokens[0], "hw_rev", strlen("hw_rev"))) {
        hw_resource_.hw_revision = atoi(tokens[1]);  // HW Rev, v1/v2
      } else if (!strncmp(tokens[0], "rgb_pipes", strlen("rgb_pipes"))) {
        hw_resource_.num_rgb_pipe = (uint8_t)atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "vig_pipes", strlen("vig_pipes"))) {
        hw_resource_.num_vig_pipe = (uint8_t)atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "dma_pipes", strlen("dma_pipes"))) {
        hw_resource_.num_dma_pipe = (uint8_t)atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "cursor_pipes", strlen("cursor_pipes"))) {
        hw_resource_.num_cursor_pipe = (uint8_t)atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "blending_stages", strlen("blending_stages"))) {
        hw_resource_.num_blending_stages = (uint8_t)atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_downscale_ratio", strlen("max_downscale_ratio"))) {
        hw_resource_.max_scale_down = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_upscale_ratio", strlen("max_upscale_ratio"))) {
        hw_resource_.max_scale_up = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_bandwidth_low", strlen("max_bandwidth_low"))) {
        hw_resource_.max_bandwidth_low = atol(tokens[1]);
      } else if (!strncmp(tokens[0], "max_bandwidth_high", strlen("max_bandwidth_high"))) {
        hw_resource_.max_bandwidth_high = atol(tokens[1]);
      } else if (!strncmp(tokens[0], "max_mixer_width", strlen("max_mixer_width"))) {
        hw_resource_.max_mixer_width = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_pipe_bw", strlen("max_pipe_bw"))) {
        hw_resource_.max_pipe_bw = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_mdp_clk", strlen("max_mdp_clk"))) {
        hw_resource_.max_sde_clk = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "clk_fudge_factor", strlen("clk_fudge_factor"))) {
        hw_resource_.clk_fudge_factor = FLOAT(atoi(tokens[1])) / FLOAT(atoi(tokens[2]));
      } else if (!strncmp(tokens[0], "features", strlen("features"))) {
        for (uint32_t i = 0; i < token_count; i++) {
          if (!strncmp(tokens[i], "bwc", strlen("bwc"))) {
            hw_resource_.has_bwc = true;
          } else if (!strncmp(tokens[i], "decimation", strlen("decimation"))) {
            hw_resource_.has_decimation = true;
          } else if (!strncmp(tokens[i], "tile_format", strlen("tile_format"))) {
            hw_resource_.has_macrotile = true;
          } else if (!strncmp(tokens[i], "src_split", strlen("src_split"))) {
            hw_resource_.is_src_split = true;
          } else if (!strncmp(tokens[i], "non_scalar_rgb", strlen("non_scalar_rgb"))) {
            hw_resource_.has_non_scalar_rgb = true;
          } else if (!strncmp(tokens[i], "rotator_downscale", strlen("rotator_downscale"))) {
            hw_resource_.has_rotator_downscale = true;
          }
        }
      }
    }
  }
  fclose_(fileptr);

  // Split info - for MDSS Version 5 - No need to check version here
  snprintf(stringbuffer , sizeof(stringbuffer), "%s%d/msm_fb_split", fb_path_,
           fb_node_index_[kHWPrimary]);
  fileptr = fopen_(stringbuffer, "r");
  if (fileptr) {
    // Format "left right" space as delimiter
    read = getline_(&line, &len, fileptr);
    if (read != -1) {
      if (!ParseLine(line, tokens, max_count, &token_count)) {
        hw_resource_.split_info.left_split = atoi(tokens[0]);
        hw_resource_.split_info.right_split = atoi(tokens[1]);
      }
    }
    fclose_(fileptr);
  }

  // SourceSplit enabled - Get More information
  if (hw_resource_.is_src_split) {
    snprintf(stringbuffer , sizeof(stringbuffer), "%s%d/msm_fb_src_split_info", fb_path_,
             fb_node_index_[kHWPrimary]);
    fileptr = fopen_(stringbuffer, "r");
    if (fileptr) {
      read = getline_(&line, &len, fileptr);
      if (read != -1) {
        if (!strncmp(line, "src_split_always", strlen("src_split_always"))) {
          hw_resource_.always_src_split = true;
        }
      }
      fclose_(fileptr);
    }
  }

  DLOGI("SDE Version = %d, SDE Revision = %x, RGB = %d, VIG = %d, DMA = %d, Cursor = %d",
        hw_resource_.hw_version, hw_resource_.hw_revision, hw_resource_.num_rgb_pipe,
        hw_resource_.num_vig_pipe, hw_resource_.num_dma_pipe, hw_resource_.num_cursor_pipe);
  DLOGI("Upscale Ratio = %d, Downscale Ratio = %d, Blending Stages = %d", hw_resource_.max_scale_up,
        hw_resource_.max_scale_down, hw_resource_.num_blending_stages);
  DLOGI("BWC = %d, Decimation = %d, Tile Format = %d, Rotator Downscale = %d", hw_resource_.has_bwc,
        hw_resource_.has_decimation, hw_resource_.has_macrotile,
        hw_resource_.has_rotator_downscale);
  DLOGI("Left Split = %d, Right Split = %d", hw_resource_.split_info.left_split,
        hw_resource_.split_info.right_split);
  DLOGI("SourceSplit = %d, Always = %d", hw_resource_.is_src_split, hw_resource_.always_src_split);
  DLOGI("MaxLowBw = %"PRIu64", MaxHighBw = %"PRIu64"", hw_resource_.max_bandwidth_low,
        hw_resource_.max_bandwidth_high);
  DLOGI("MaxPipeBw = %"PRIu64" KBps, MaxSDEClock = %"PRIu64" Hz, ClockFudgeFactor = %f",
        hw_resource_.max_pipe_bw, hw_resource_.max_sde_clk, hw_resource_.clk_fudge_factor);

  return error;
}

int HWFrameBuffer::ParseLine(char *input, char *tokens[], const uint32_t max_token,
                             uint32_t *count) {
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

bool HWFrameBuffer::EnableHotPlugDetection(int enable) {
  bool ret_value = true;
  char hpdpath[kMaxStringLength];
  snprintf(hpdpath , sizeof(hpdpath), "%s%d/hpd", fb_path_, fb_node_index_[kDeviceHDMI]);
  int hpdfd = open_(hpdpath, O_RDWR, 0);
  if (hpdfd < 0) {
    DLOGE("Open failed = %s", hpdpath);
    return kErrorHardware;
  }
  char value = enable ? '1' : '0';
  ssize_t length = write_(hpdfd, &value, 1);
  if (length <= 0) {
    DLOGE("Write failed 'hpd' = %d", enable);
    ret_value = false;
  }
  close_(hpdfd);

  return ret_value;
}

int HWFrameBuffer::GetHDMIModeCount() {
  ssize_t length = -1;
  char edid_str[256] = {'\0'};
  char edid_path[kMaxStringLength] = {'\0'};
  snprintf(edid_path, sizeof(edid_path), "%s%d/edid_modes", fb_path_, fb_node_index_[kHWHDMI]);
  int edid_file = open_(edid_path, O_RDONLY);
  if (edid_file < 0) {
    DLOGE("EDID file open failed.");
    return -1;
  }

  length = read_(edid_file, edid_str, sizeof(edid_str)-1);
  if (length <= 0) {
    DLOGE("%s: edid_modes file empty");
    edid_str[0] = '\0';
  } else {
    DLOGI("EDID mode string: %s", edid_str);
    while (length > 1 && isspace(edid_str[length-1])) {
      --length;
    }
    edid_str[length] = '\0';
  }
  close_(edid_file);

  if (length > 0) {
    // Get EDID modes from the EDID string
    char *ptr = edid_str;
    const uint32_t edid_count_max = 128;
    char *tokens[edid_count_max] = { NULL };
    ParseLine(ptr, tokens, edid_count_max, &hdmi_mode_count_);
    for (uint32_t i = 0; i < hdmi_mode_count_; i++) {
      hdmi_modes_[i] = atoi(tokens[i]);
    }
  }
  return (hdmi_mode_count_ > 0) ? hdmi_mode_count_ : 0;
}

bool HWFrameBuffer::MapHDMIDisplayTiming(const msm_hdmi_mode_timing_info *mode,
                                         fb_var_screeninfo *info) {
  if (!mode || !info) {
    return false;
  }

  info->reserved[0] = 0;
  info->reserved[1] = 0;
  info->reserved[2] = 0;
  info->reserved[3] = (info->reserved[3] & 0xFFFF) | (mode->video_format << 16);
  info->xoffset = 0;
  info->yoffset = 0;
  info->xres = mode->active_h;
  info->yres = mode->active_v;
  info->pixclock = (mode->pixel_freq) * 1000;
  info->vmode = mode->interlaced ? FB_VMODE_INTERLACED : FB_VMODE_NONINTERLACED;
  info->right_margin = mode->front_porch_h;
  info->hsync_len = mode->pulse_width_h;
  info->left_margin = mode->back_porch_h;
  info->lower_margin = mode->front_porch_v;
  info->vsync_len = mode->pulse_width_v;
  info->upper_margin = mode->back_porch_v;

  return true;
}

}  // namespace sde
