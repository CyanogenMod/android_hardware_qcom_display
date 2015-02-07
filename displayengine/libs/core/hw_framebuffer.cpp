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

#define IOCTL_LOGE(ioctl, type) DLOGE("ioctl %s, device = %d errno = %d, desc = %s", #ioctl, \
                                      type, errno, strerror(errno))

#ifdef DISPLAY_CORE_VIRTUAL_DRIVER
extern int virtual_ioctl(int fd, int cmd, ...);
extern int virtual_open(const char *file_name, int access, ...);
extern int virtual_close(int fd);
extern int virtual_poll(struct pollfd *fds,  nfds_t num, int timeout);
extern ssize_t virtual_pread(int fd, void *data, size_t count, off_t offset);
extern ssize_t virtual_pwrite(int fd, const void *data, size_t count, off_t offset);
extern FILE* virtual_fopen(const char *fname, const char *mode);
extern int virtual_fclose(FILE* fileptr);
extern ssize_t virtual_getline(char **lineptr, size_t *linelen, FILE *stream);

#endif

namespace sde {

HWFrameBuffer::HWFrameBuffer(BufferSyncHandler *buffer_sync_handler)
  : event_thread_name_("SDE_EventThread"), fake_vsync_(false), exit_threads_(false),
    fb_path_("/sys/devices/virtual/graphics/fb"), hotplug_enabled_(false),
    buffer_sync_handler_(buffer_sync_handler) {
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

#ifdef DISPLAY_CORE_VIRTUAL_DRIVER
  // If debug property to use virtual driver is set, point to virtual driver interfaces.
  if (Debug::IsVirtualDriver()) {
    ioctl_ = virtual_ioctl;
    open_ = virtual_open;
    close_ = virtual_close;
    poll_ = virtual_poll;
    pread_ = virtual_pread;
    pwrite_ = virtual_pwrite;
    fopen_ = virtual_fopen;
    fclose_ = virtual_fclose;
    getline_ = virtual_getline;
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
  const char* event_name[kNumDisplayEvents] = {"vsync_event", "show_blank_event", "idle_notify"};

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

        if ((primary_panel_info_.type == kCommandModePanel) && (display == kDevicePrimary) &&
            (!strncmp(event_name[event], "idle_notify", strlen("idle_notify")))) {
          continue;
        }

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

  // Disable HPD at start if HDMI is external, it will be enabled later when the display powers on
  // This helps for framework reboot or adb shell stop/start
  EnableHotPlugDetection(0);

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
      close_(poll_fds_[display][event].fd);
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
  case kDeviceRotator:
    snprintf(device_name, sizeof(device_name), "%s", "/dev/mdss_rotator");
    break;
  default:
    break;
  }

  hw_context->device_fd = open_(device_name, O_RDWR);
  if (hw_context->device_fd < 0) {
    DLOGE("open %s failed err = %d errstr = %s", device_name, errno,  strerror(errno));
    delete hw_context;
    return kErrorResources;
  }

  hw_context->type = type;

  *device = hw_context;

  return error;
}

DisplayError HWFrameBuffer::Close(Handle device) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  switch (hw_context->type) {
  case kDevicePrimary:
  case kDeviceVirtual:
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
  case kDeviceVirtual:
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
  DTRACE_SCOPED();

  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  int &device_fd = hw_context->device_fd;
  // Variable screen info
  STRUCT_VAR(fb_var_screeninfo, var_screeninfo);

  switch (hw_context->type) {
  case kDevicePrimary:
    {
      if (ioctl_(device_fd, FBIOGET_VSCREENINFO, &var_screeninfo) < 0) {
        IOCTL_LOGE(FBIOGET_VSCREENINFO, hw_context->type);
        return kErrorHardware;
      }

      // Frame rate
      STRUCT_VAR(msmfb_metadata, meta_data);
      meta_data.op = metadata_op_frame_rate;
      if (ioctl_(device_fd, MSMFB_METADATA_GET, &meta_data) < 0) {
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

  case kDeviceVirtual:
    break;

  default:
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::SetDisplayAttributes(Handle device, uint32_t index) {
  DTRACE_SCOPED();

  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  DisplayError error = kErrorNone;

  switch (hw_context->type) {
  case kDevicePrimary:
  case kDeviceVirtual:
    break;

  case kDeviceHDMI:
    {
      // Variable screen info
      STRUCT_VAR(fb_var_screeninfo, vscreeninfo);
      if (ioctl_(hw_context->device_fd, FBIOGET_VSCREENINFO, &vscreeninfo) < 0) {
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
      metadata.op = metadata_op_vic;
      metadata.data.video_info_code = timing_mode->video_format;
      if (ioctl(hw_context->device_fd, MSMFB_METADATA_SET, &metadata) < 0) {
        IOCTL_LOGE(MSMFB_METADATA_SET, hw_context->type);
        return kErrorHardware;
      }

      DLOGI("SetInfo<Mode=%d %dx%d (%d,%d,%d),(%d,%d,%d) %dMHz>", vscreeninfo.reserved[3] & 0xFF00,
            vscreeninfo.xres, vscreeninfo.yres, vscreeninfo.right_margin, vscreeninfo.hsync_len,
            vscreeninfo.left_margin, vscreeninfo.lower_margin, vscreeninfo.vsync_len,
            vscreeninfo.upper_margin, vscreeninfo.pixclock/1000000);

      vscreeninfo.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_ALL | FB_ACTIVATE_FORCE;
      if (ioctl_(hw_context->device_fd, FBIOPUT_VSCREENINFO, &vscreeninfo) < 0) {
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

DisplayError HWFrameBuffer::GetConfigIndex(Handle device, uint32_t mode, uint32_t *index) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  switch (hw_context->type) {
  case kDevicePrimary:
  case kDeviceVirtual:
    return kErrorNone;
    break;
  case kDeviceHDMI:
    // Check if the mode is valid and return corresponding index
    for (uint32_t i = 0; i < hdmi_mode_count_; i++) {
      if (hdmi_modes_[i] == mode) {
        *index = i;
        DLOGI("Index = %d for config = %d", *index, mode);
        return kErrorNone;
      }
    }
    break;
  default:
    return kErrorParameters;
  }

  DLOGE("Config = %d not supported", mode);
  return kErrorNotSupported;
}


DisplayError HWFrameBuffer::PowerOn(Handle device) {
  DTRACE_SCOPED();

  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  if (ioctl_(hw_context->device_fd, FBIOBLANK, FB_BLANK_UNBLANK) < 0) {
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
  DTRACE_SCOPED();

  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  HWDisplay *hw_display = &hw_context->hw_display;

  switch (hw_context->type) {
  case kDevicePrimary:
    if (ioctl_(hw_context->device_fd, FBIOBLANK, FB_BLANK_POWERDOWN) < 0) {
      IOCTL_LOGE(FB_BLANK_POWERDOWN, hw_context->type);
      return kErrorHardware;
    }
    break;
  default:
    break;
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
  DTRACE_SCOPED();

  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  int vsync_on = enable ? 1 : 0;
  if (ioctl_(hw_context->device_fd, MSMFB_OVERLAY_VSYNC_CTRL, &vsync_on) < 0) {
    IOCTL_LOGE(MSMFB_OVERLAY_VSYNC_CTRL, hw_context->type);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::OpenRotatorSession(Handle device, HWLayers *hw_layers) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  HWRotator *hw_rotator = &hw_context->hw_rotator;

  hw_rotator->Reset();

  HWLayersInfo &hw_layer_info = hw_layers->info;

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer& layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];
    LayerBuffer *input_buffer = layer.input_buffer;
    bool rot90 = (layer.transform.rotation == 90.0f);

    for (uint32_t count = 0; count < 2; count++) {
      HWRotateInfo *rotate_info = &hw_layers->config[i].rotates[count];

      if (rotate_info->valid) {
        HWBufferInfo *rot_buf_info = &rotate_info->hw_buffer_info;

        if (rot_buf_info->session_id < 0) {
          STRUCT_VAR(mdp_rotation_config, mdp_rot_config);
          mdp_rot_config.version = MDP_ROTATION_REQUEST_VERSION_1_0;
          mdp_rot_config.input.width = input_buffer->width;
          mdp_rot_config.input.height = input_buffer->height;
          SetFormat(input_buffer->format, &mdp_rot_config.input.format);
          mdp_rot_config.output.width = rot_buf_info->output_buffer.width;
          mdp_rot_config.output.height = rot_buf_info->output_buffer.height;
          SetFormat(rot_buf_info->output_buffer.format, &mdp_rot_config.output.format);
          mdp_rot_config.frame_rate = layer.frame_rate;

          if (ioctl_(hw_context->device_fd, MDSS_ROTATION_OPEN, &mdp_rot_config) < 0) {
            IOCTL_LOGE(MDSS_ROTATION_OPEN, hw_context->type);
            return kErrorHardware;
          }

          rot_buf_info->session_id = mdp_rot_config.session_id;

          DLOGV_IF(kTagDriverConfig, "session_id %d", rot_buf_info->session_id);
        }
      }
    }
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::CloseRotatorSession(Handle device, int32_t session_id) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  if (ioctl_(hw_context->device_fd, MDSS_ROTATION_CLOSE, (uint32_t)session_id) < 0) {
    IOCTL_LOGE(MDSS_ROTATION_CLOSE, hw_context->type);
    return kErrorHardware;
  }

  DLOGV_IF(kTagDriverConfig, "session_id %d", session_id);

  return kErrorNone;
}

DisplayError HWFrameBuffer::Validate(Handle device, HWLayers *hw_layers) {
  DTRACE_SCOPED();

  DisplayError error = kErrorNone;

  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  switch (hw_context->type) {
    case kDevicePrimary:
    case kDeviceHDMI:
    case kDeviceVirtual:
      error = DisplayValidate(hw_context, hw_layers);
      if (error != kErrorNone) {
        return error;
      }
      break;
    case kDeviceRotator:
      error = RotatorValidate(hw_context, hw_layers);
      if (error != kErrorNone) {
        return error;
      }
      break;
    default:
      break;
  }
  return error;
}

DisplayError HWFrameBuffer::Commit(Handle device, HWLayers *hw_layers) {
  DisplayError error = kErrorNone;

  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  switch (hw_context->type) {
    case kDevicePrimary:
    case kDeviceHDMI:
    case kDeviceVirtual:
      error = DisplayCommit(hw_context, hw_layers);
      if (error != kErrorNone) {
        return error;
      }
      break;
    case kDeviceRotator:
      error = RotatorCommit(hw_context, hw_layers);
      if (error != kErrorNone) {
        return error;
      }
      break;
    default:
      break;
  }
  return error;
}

DisplayError HWFrameBuffer::DisplayValidate(HWContext *hw_context, HWLayers *hw_layers) {
  DisplayError error = kErrorNone;
  HWDisplay *hw_display = &hw_context->hw_display;

  hw_display->Reset();

  HWLayersInfo &hw_layer_info = hw_layers->info;
  LayerStack *stack = hw_layer_info.stack;

  DLOGV_IF(kTagDriverConfig, "************************** %s Validate Input ***********************",
           GetDeviceString(hw_context->type));
  DLOGV_IF(kTagDriverConfig, "SDE layer count is %d", hw_layer_info.count);

  mdp_layer_commit_v1 &mdp_commit = hw_display->mdp_disp_commit.commit_v1;
  mdp_input_layer *mdp_layers = hw_display->mdp_in_layers;
  mdp_output_layer *mdp_out_layer = &hw_display->mdp_out_layer;
  uint32_t &mdp_layer_count = mdp_commit.input_layer_cnt;

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    uint32_t layer_index = hw_layer_info.index[i];
    Layer &layer = stack->layers[layer_index];
    LayerBuffer *input_buffer = layer.input_buffer;
    HWPipeInfo *left_pipe = &hw_layers->config[i].left_pipe;
    HWPipeInfo *right_pipe = &hw_layers->config[i].right_pipe;
    mdp_input_layer mdp_layer;

    for (uint32_t count = 0; count < 2; count++) {
      HWPipeInfo *pipe_info = (count == 0) ? left_pipe : right_pipe;
      HWRotateInfo *rotate_info = &hw_layers->config[i].rotates[count];

      if (rotate_info->valid) {
        input_buffer = &rotate_info->hw_buffer_info.output_buffer;
      }

      if (pipe_info->valid) {
        mdp_input_layer &mdp_layer = mdp_layers[mdp_layer_count];
        mdp_layer_buffer &mdp_buffer = mdp_layer.buffer;

        mdp_buffer.width = input_buffer->width;
        mdp_buffer.height = input_buffer->height;

        error = SetFormat(input_buffer->format, &mdp_buffer.format);
        if (error != kErrorNone) {
          return error;
        }

        mdp_layer.alpha = layer.plane_alpha;
        mdp_layer.z_order = static_cast<uint16_t>(i);
        mdp_layer.transp_mask = 0xffffffff;
        SetBlending(layer.blending, &mdp_layer.blend_op);
        mdp_layer.pipe_ndx = pipe_info->pipe_id;
        mdp_layer.horz_deci = pipe_info->horizontal_decimation;
        mdp_layer.vert_deci = pipe_info->vertical_decimation;

        SetRect(pipe_info->src_roi, &mdp_layer.src_rect);
        SetRect(pipe_info->dst_roi, &mdp_layer.dst_rect);

        // Flips will be taken care by rotator, if layer requires 90 rotation. So Dont use MDP for
        // flip operation, if layer transform is 90.
        if (!layer.transform.rotation) {
          if (layer.transform.flip_vertical) {
            mdp_layer.flags |= MDP_LAYER_FLIP_UD;
          }

          if (layer.transform.flip_horizontal) {
            mdp_layer.flags |= MDP_LAYER_FLIP_LR;
          }
        }

        mdp_layer_count++;

        DLOGV_IF(kTagDriverConfig, "******************* Layer[%d] %s pipe Input ******************",
                 i, count ? "Right" : "Left");
        DLOGV_IF(kTagDriverConfig, "in_w %d, in_h %d, in_f %d", mdp_buffer.width, mdp_buffer.height,
                 mdp_buffer.format);
        DLOGV_IF(kTagDriverConfig, "plane_alpha %d, zorder %d, blending %d, horz_deci %d, "
                 "vert_deci %d", mdp_layer.alpha, mdp_layer.z_order, mdp_layer.blend_op,
                 mdp_layer.horz_deci, mdp_layer.vert_deci);
        DLOGV_IF(kTagDriverConfig, "src_rect [%d, %d, %d, %d]", mdp_layer.src_rect.x,
                 mdp_layer.src_rect.y, mdp_layer.src_rect.w, mdp_layer.src_rect.h);
        DLOGV_IF(kTagDriverConfig, "dst_rect [%d, %d, %d, %d]", mdp_layer.dst_rect.x,
                 mdp_layer.dst_rect.y, mdp_layer.dst_rect.w, mdp_layer.dst_rect.h);
        DLOGV_IF(kTagDriverConfig, "*************************************************************");
      }
    }
  }

  if (hw_context->type == kDeviceVirtual) {
    LayerBuffer *output_buffer = hw_layers->info.stack->output_buffer;
    // TODO(user): Need to assign the writeback id from the resource manager, since the support
    // has not been added hard coding it to 2 for now.
    mdp_out_layer->writeback_ndx = 2;
    mdp_out_layer->buffer.width = output_buffer->width;
    mdp_out_layer->buffer.height = output_buffer->height;
    SetFormat(output_buffer->format, &mdp_out_layer->buffer.format);

    DLOGI_IF(kTagDriverConfig, "******************* Output buffer Info **********************");
    DLOGI_IF(kTagDriverConfig, "out_w %d, out_h %d, out_f %d, wb_id %d",
             mdp_out_layer->buffer.width, mdp_out_layer->buffer.height,
             mdp_out_layer->buffer.format, mdp_out_layer->writeback_ndx);
    DLOGI_IF(kTagDriverConfig, "*************************************************************");
  }

  mdp_commit.flags |= MDP_VALIDATE_LAYER;
  if (ioctl_(hw_context->device_fd, MSMFB_ATOMIC_COMMIT, &hw_display->mdp_disp_commit) < 0) {
    IOCTL_LOGE(MSMFB_ATOMIC_COMMIT, hw_context->type);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::DisplayCommit(HWContext *hw_context, HWLayers *hw_layers) {
  DTRACE_SCOPED();

  HWDisplay *hw_display = &hw_context->hw_display;
  HWLayersInfo &hw_layer_info = hw_layers->info;
  LayerStack *stack = hw_layer_info.stack;

  DLOGV_IF(kTagDriverConfig, "*************************** %s Commit Input ************************",
           GetDeviceString(hw_context->type));
  DLOGV_IF(kTagDriverConfig, "SDE layer count is %d", hw_layer_info.count);

  mdp_layer_commit_v1 &mdp_commit = hw_display->mdp_disp_commit.commit_v1;
  mdp_input_layer *mdp_layers = hw_display->mdp_in_layers;
  mdp_output_layer *mdp_out_layer = &hw_display->mdp_out_layer;
  uint32_t mdp_layer_index = 0;

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    uint32_t layer_index = hw_layer_info.index[i];
    LayerBuffer *input_buffer = stack->layers[layer_index].input_buffer;
    HWPipeInfo *left_pipe = &hw_layers->config[i].left_pipe;
    HWPipeInfo *right_pipe = &hw_layers->config[i].right_pipe;

    for (uint32_t count = 0; count < 2; count++) {
      HWPipeInfo *pipe_info = (count == 0) ? left_pipe : right_pipe;
      HWRotateInfo *rotate_info = &hw_layers->config[i].rotates[count];

      if (rotate_info->valid) {
        input_buffer = &rotate_info->hw_buffer_info.output_buffer;
      }

      if (pipe_info->valid) {
        mdp_layer_buffer &mdp_buffer = mdp_layers[mdp_layer_index].buffer;
        mdp_input_layer &mdp_layer = mdp_layers[mdp_layer_index];
        if (input_buffer->planes[0].fd >= 0) {
          mdp_buffer.plane_count = 1;
          mdp_buffer.planes[0].fd = input_buffer->planes[0].fd;
          mdp_buffer.planes[0].offset = input_buffer->planes[0].offset;
          SetStride(hw_context->type, input_buffer->format, input_buffer->planes[0].stride,
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
  if (hw_context->type == kDeviceVirtual) {
    LayerBuffer *output_buffer = hw_layers->info.stack->output_buffer;

    if (output_buffer->planes[0].fd >= 0) {
      mdp_out_layer->buffer.planes[0].fd = output_buffer->planes[0].fd;
      mdp_out_layer->buffer.planes[0].offset = output_buffer->planes[0].offset;
      SetStride(hw_context->type, output_buffer->format, output_buffer->planes[0].stride,
                &mdp_out_layer->buffer.planes[0].stride);
      mdp_out_layer->buffer.plane_count = 1;
    } else {
      DLOGW("Invalid output buffer fd, setting plane count to 0");
      mdp_out_layer->buffer.plane_count = 0;
    }

    mdp_out_layer->buffer.fence = output_buffer->acquire_fence_fd;

    DLOGI_IF(kTagDriverConfig, "******************* Output buffer Info **********************");
    DLOGI_IF(kTagDriverConfig, "out_fd %d, out_offset %d, out_stride %d, acquire_fence %d",
             mdp_out_layer->buffer.planes[0].fd, mdp_out_layer->buffer.planes[0].offset,
             mdp_out_layer->buffer.planes[0].stride,  mdp_out_layer->buffer.fence);
    DLOGI_IF(kTagDriverConfig, "*************************************************************");
  }

  mdp_commit.release_fence = -1;
  mdp_commit.flags &= ~MDP_VALIDATE_LAYER;
  if (ioctl_(hw_context->device_fd, MSMFB_ATOMIC_COMMIT, &hw_display->mdp_disp_commit) < 0) {
    IOCTL_LOGE(MSMFB_ATOMIC_COMMIT, hw_context->type);
    return kErrorHardware;
  }

  stack->retire_fence_fd = mdp_commit.retire_fence;

  // MDP returns only one release fence for the entire layer stack. Duplicate this fence into all
  // layers being composed by MDP.
  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    uint32_t layer_index = hw_layer_info.index[i];
    LayerBuffer *input_buffer = stack->layers[layer_index].input_buffer;
    HWRotateInfo *left_rotate = &hw_layers->config[i].rotates[0];
    HWRotateInfo *right_rotate = &hw_layers->config[i].rotates[1];

    if (!left_rotate->valid && !right_rotate->valid) {
      input_buffer->release_fence_fd = dup(mdp_commit.release_fence);
      continue;
    }

    for (uint32_t count = 0; count < 2; count++) {
      HWRotateInfo *rotate_info = &hw_layers->config[i].rotates[count];
      if (rotate_info->valid) {
        input_buffer = &rotate_info->hw_buffer_info.output_buffer;
        input_buffer->release_fence_fd = dup(mdp_commit.release_fence);
        close_(input_buffer->acquire_fence_fd);
        input_buffer->acquire_fence_fd = -1;
      }
    }
  }
  DLOGI_IF(kTagDriverConfig, "*************************** %s Commit Input ************************",
           GetDeviceString(hw_context->type));
  DLOGI_IF(kTagDriverConfig, "retire_fence_fd %d", stack->retire_fence_fd);
  DLOGI_IF(kTagDriverConfig, "*************************************************************");

  close_(mdp_commit.release_fence);

  return kErrorNone;
}

DisplayError HWFrameBuffer::RotatorValidate(HWContext *hw_context, HWLayers *hw_layers) {
  HWRotator *hw_rotator = &hw_context->hw_rotator;
  DLOGV_IF(kTagDriverConfig, "************************* %s Validate Input ************************",
           GetDeviceString(hw_context->type));

  hw_rotator->Reset();

  mdp_rotation_request *mdp_rot_request = &hw_rotator->mdp_rot_req;
  HWLayersInfo &hw_layer_info = hw_layers->info;

  uint32_t &rot_count = mdp_rot_request->count;
  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer& layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];

    for (uint32_t count = 0; count < 2; count++) {
      HWRotateInfo *rotate_info = &hw_layers->config[i].rotates[count];

      if (rotate_info->valid) {
        HWBufferInfo *rot_buf_info = &rotate_info->hw_buffer_info;
        mdp_rotation_item *mdp_rot_item = &mdp_rot_request->list[rot_count];
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

        SetRect(rotate_info->src_roi, &mdp_rot_item->src_rect);
        SetRect(rotate_info->dst_roi, &mdp_rot_item->dst_rect);

        // TODO(user): Need to assign the writeback id and pipe id  returned from resource manager.
        mdp_rot_item->pipe_idx = 0;
        mdp_rot_item->wb_idx = 0;

        mdp_rot_item->input.width = layer.input_buffer->width;
        mdp_rot_item->input.height = layer.input_buffer->height;
        SetFormat(layer.input_buffer->format, &mdp_rot_item->input.format);

        mdp_rot_item->output.width = rot_buf_info->output_buffer.width;
        mdp_rot_item->output.height = rot_buf_info->output_buffer.height;
        SetFormat(rot_buf_info->output_buffer.format, &mdp_rot_item->output.format);

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

  mdp_rot_request->flags = MDSS_ROTATION_REQUEST_VALIDATE;
  if (ioctl_(hw_context->device_fd, MDSS_ROTATION_REQUEST, mdp_rot_request) < 0) {
    IOCTL_LOGE(MDSS_ROTATION_REQUEST, hw_context->type);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::RotatorCommit(HWContext *hw_context, HWLayers *hw_layers) {
  HWRotator *hw_rotator = &hw_context->hw_rotator;
  mdp_rotation_request *mdp_rot_request = &hw_rotator->mdp_rot_req;
  HWLayersInfo &hw_layer_info = hw_layers->info;
  uint32_t rot_count = 0;

  DLOGV_IF(kTagDriverConfig, "************************* %s Commit Input **************************",
           GetDeviceString(hw_context->type));
  DLOGV_IF(kTagDriverConfig, "Rotate layer count is %d", mdp_rot_request->count);

  mdp_rot_request->list = hw_rotator->mdp_rot_layers;

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer& layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];

    for (uint32_t count = 0; count < 2; count++) {
      HWRotateInfo *rotate_info = &hw_layers->config[i].rotates[count];

      if (rotate_info->valid) {
        HWBufferInfo *rot_buf_info = &rotate_info->hw_buffer_info;
        mdp_rotation_item *mdp_rot_item = &mdp_rot_request->list[rot_count];

        mdp_rot_item->input.planes[0].fd = layer.input_buffer->planes[0].fd;
        mdp_rot_item->input.planes[0].offset = layer.input_buffer->planes[0].offset;
        SetStride(hw_context->type, layer.input_buffer->format, layer.input_buffer->width,
                  &mdp_rot_item->input.planes[0].stride);
        mdp_rot_item->input.plane_count = 1;
        mdp_rot_item->input.fence = layer.input_buffer->acquire_fence_fd;

        mdp_rot_item->output.planes[0].fd = rot_buf_info->output_buffer.planes[0].fd;
        mdp_rot_item->output.planes[0].offset = rot_buf_info->output_buffer.planes[0].offset;
        SetStride(hw_context->type, rot_buf_info->output_buffer.format,
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

  mdp_rot_request->flags &= ~MDSS_ROTATION_REQUEST_VALIDATE;
  if (ioctl_(hw_context->device_fd, MDSS_ROTATION_REQUEST, mdp_rot_request) < 0) {
    IOCTL_LOGE(MDSS_ROTATION_REQUEST, hw_context->type);
    return kErrorHardware;
  }

  rot_count = 0;
  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer& layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];

    layer.input_buffer->release_fence_fd = -1;

    for (uint32_t count = 0; count < 2; count++) {
      HWRotateInfo *rotate_info = &hw_layers->config[i].rotates[count];

      if (rotate_info->valid) {
        HWBufferInfo *rot_buf_info = &rotate_info->hw_buffer_info;
        mdp_rotation_item *mdp_rot_item = &mdp_rot_request->list[rot_count];

        SyncMerge(layer.input_buffer->release_fence_fd, dup(mdp_rot_item->output.fence),
                  &layer.input_buffer->release_fence_fd);

        rot_buf_info->output_buffer.acquire_fence_fd = dup(mdp_rot_item->output.fence);

        close_(mdp_rot_item->output.fence);
        rot_count++;
      }
    }
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::Flush(Handle device) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  HWDisplay *hw_display = &hw_context->hw_display;

  hw_display->Reset();
  mdp_layer_commit_v1 &mdp_commit = hw_display->mdp_disp_commit.commit_v1;
  mdp_commit.input_layer_cnt = 0;
  mdp_commit.flags &= ~MDP_VALIDATE_LAYER;

  if (ioctl_(hw_context->device_fd, MSMFB_ATOMIC_COMMIT, &hw_display->mdp_disp_commit) == -1) {
    IOCTL_LOGE(MSMFB_ATOMIC_COMMIT, hw_context->type);
    return kErrorHardware;
  }

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
  case kFormatRGBA8888Ubwc:             *target = MDP_RGBA_8888_UBWC;    break;
  case kFormatRGB565Ubwc:               *target = MDP_RGB_565_UBWC;      break;
  case kFormatYCbCr420SPVenusUbwc:      *target = MDP_Y_CBCR_H2V2_UBWC;  break;
  default:
    DLOGE("Unsupported format type %d", source);
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError HWFrameBuffer::SetStride(HWDeviceType device_type, LayerBufferFormat format,
                                      uint32_t width, uint32_t *target) {
  // TODO(user): This SetStride function is an workaround to satisfy the driver expectation for
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
    *target = width * 4;
    break;
  case kFormatRGB888:
    *target = width * 3;
    break;
  case kFormatRGB565:
    *target = width * 3;
    break;
  case kFormatYCbCr420SemiPlanarVenus:
  case kFormatYCbCr420Planar:
  case kFormatYCrCb420Planar:
  case kFormatYCbCr420SemiPlanar:
  case kFormatYCrCb420SemiPlanar:
    *target = width;
    break;
  case kFormatYCbCr422Packed:
    *target = width * 2;
    break;
  default:
    DLOGE("Unsupported format type %d", format);
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
  target->x = UINT32(source.left);
  target->y = UINT32(source.top);
  target->w = UINT32(source.right) - target->x;
  target->h = UINT32(source.bottom) - target->y;
}

void HWFrameBuffer::SyncMerge(const int &fd1, const int &fd2, int *target) {
  if (fd1 >= 0 && fd2 >= 0) {
    buffer_sync_handler_->SyncMerge(fd1, fd2, target);
  } else if (fd1 >= 0) {
    *target = fd1;
  } else if (fd2 >= 0) {
    *target = fd2;
  }
}

const char *HWFrameBuffer::GetDeviceString(HWDeviceType type) {
  switch (type) {
  case kDevicePrimary:
    return "Primary Display Device";
  case kDeviceHDMI:
    return "HDMI Display Device";
  case kDeviceVirtual:
    return "Virtual Display Device";
  case kDeviceRotator:
    return "Rotator Device";
  default:
    return "Invalid Device";
  }
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

  typedef void (HWFrameBuffer::*EventHandler)(int, char*);
  EventHandler event_handler[kNumDisplayEvents] = { &HWFrameBuffer::HandleVSync,
                                                    &HWFrameBuffer::HandleBlank,
                                                    &HWFrameBuffer::HandleIdleTimeout };

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

void HWFrameBuffer::HandleIdleTimeout(int display_id, char *data) {
  event_handler_[display_id]->IdleTimeout();
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
      primary_panel_info_.type = kCommandModePanel;
      fb_node_index_[kDevicePrimary] = i;
    } else if ((strncmp(line, "mipi dsi video panel", strlen("mipi dsi video panel")) == 0))  {
      primary_panel_info_.type = kVideoModePanel;
      fb_node_index_[kDevicePrimary] = i;
    } else if ((strncmp(line, "lvds panel", strlen("lvds panel")) == 0)) {
      primary_panel_info_.type = kLVDSPanel;
      fb_node_index_[kDevicePrimary] = i;
    } else if ((strncmp(line, "edp panel", strlen("edp panel")) == 0)) {
      primary_panel_info_.type = kEDPPanel;
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
        primary_panel_info_.partial_update = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "xstart", strlen("xstart"))) {
        primary_panel_info_.left_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "walign", strlen("walign"))) {
        primary_panel_info_.width_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "ystart", strlen("ystart"))) {
        primary_panel_info_.top_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "halign", strlen("halign"))) {
        primary_panel_info_.height_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "min_w", strlen("min_w"))) {
        primary_panel_info_.min_roi_width = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "min_h", strlen("min_h"))) {
        primary_panel_info_.min_roi_height = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "roi_merge", strlen("roi_merge"))) {
        primary_panel_info_.needs_roi_merge = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "dynamic_fps_en", strlen("dyn_fps_en"))) {
        primary_panel_info_.dynamic_fps = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "min_fps", strlen("min_fps"))) {
        primary_panel_info_.min_fps = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_fps", strlen("max_fps"))) {
        primary_panel_info_.max_fps= atoi(tokens[1]);
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
  ssize_t length = pwrite_(hpdfd, &value, 1, 0);
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

  length = pread_(edid_file, edid_str, sizeof(edid_str)-1, 0);
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

void HWFrameBuffer::SetIdleTimeoutMs(Handle device, uint32_t timeout_ms) {
  char node_path[kMaxStringLength] = {0};
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  DLOGI("idle timeout = %d ms", timeout_ms);

  switch (hw_context->type) {
  case kDevicePrimary:
    {
      // Idle fallback feature is supported only for video mode panel.
      if (primary_panel_info_.type == kCommandModePanel) {
        return;
      }

      snprintf(node_path, sizeof(node_path), "%s%d/idle_time", fb_path_,
               fb_node_index_[hw_context->type]);

      // Open a sysfs node to send the timeout value to driver.
      int fd = open_(node_path, O_WRONLY);
      if (fd < 0) {
        DLOGE("Unable to open %s, node %s", node_path, strerror(errno));
        return;
      }

      char timeout_string[64];
      snprintf(timeout_string, sizeof(timeout_string), "%d", timeout_ms);

      // Notify driver about the timeout value
      ssize_t length = pwrite_(fd, timeout_string, strlen(timeout_string), 0);
      if (length < -1) {
        DLOGE("Unable to write into %s, node %s", node_path, strerror(errno));
      }

      close_(fd);
    }
    break;
  default:
    break;
  }
}

}  // namespace sde

