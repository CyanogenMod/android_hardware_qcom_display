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

#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <utils/debug.h>
#include "hw_primary.h"

#define __CLASS__ "HWPrimary"

namespace sde {

DisplayError HWPrimaryInterface::Create(HWPrimaryInterface **intf, HWInfoInterface *hw_info_intf,
                                        BufferSyncHandler *buffer_sync_handler) {
  DisplayError error = kErrorNone;
  HWPrimary *hw_primary = NULL;

  hw_primary = new HWPrimary(buffer_sync_handler, hw_info_intf);
  error = hw_primary->Init();
  if (error != kErrorNone) {
    delete hw_primary;
  } else {
    *intf = hw_primary;
  }

  return error;
}

DisplayError HWPrimaryInterface::Destroy(HWPrimaryInterface *intf) {
  HWPrimary *hw_primary = static_cast<HWPrimary *>(intf);
  hw_primary->Deinit();
  delete hw_primary;

  return kErrorNone;
}

HWPrimary::HWPrimary(BufferSyncHandler *buffer_sync_handler, HWInfoInterface *hw_info_intf)
  : HWDevice(buffer_sync_handler), event_thread_name_("SDE_EventThread"), fake_vsync_(false),
    exit_threads_(false) {
  HWDevice::device_type_ = kDevicePrimary;
  HWDevice::device_name_ = "Primary Display Device";
  HWDevice::hw_info_intf_ = hw_info_intf;
}

DisplayError HWPrimary::Init() {
  DisplayError error = kErrorNone;
  char node_path[kMaxStringLength] = {0};
  char data[kMaxStringLength] = {0};
  const char* event_name[kNumDisplayEvents] = {"vsync_event", "show_blank_event", "idle_notify",
                                              "msm_fb_thermal_level"};

  error = HWDevice::Init();
  if (error != kErrorNone) {
    goto CleanupOnError;
  }

  // Open nodes for polling
  for (int event = 0; event < kNumDisplayEvents; event++) {
    poll_fds_[event].fd = -1;
  }

  if (!fake_vsync_) {
    for (int event = 0; event < kNumDisplayEvents; event++) {
      pollfd &poll_fd = poll_fds_[event];

      if ((hw_panel_info_.mode == kModeCommand) &&
          (!strncmp(event_name[event], "idle_notify", strlen("idle_notify")))) {
        continue;
      }

      snprintf(node_path, sizeof(node_path), "%s%d/%s", fb_path_, fb_node_index_,
               event_name[event]);

      poll_fd.fd = open_(node_path, O_RDONLY);
      if (poll_fd.fd < 0) {
        DLOGE("open failed for event=%d, error=%s", event, strerror(errno));
        error = kErrorHardware;
        goto CleanupOnError;
      }

      // Read once on all fds to clear data on all fds.
      pread_(poll_fd.fd, data , kMaxStringLength, 0);
      poll_fd.events = POLLPRI | POLLERR;
    }
  }

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
  for (int event = 0; event < kNumDisplayEvents; event++) {
    int &fd = poll_fds_[event].fd;
    if (fd >= 0) {
      close_(fd);
    }
  }

  return error;
}

DisplayError HWPrimary::Deinit() {
  exit_threads_ = true;
  pthread_join(event_thread_, NULL);

  for (int event = 0; event < kNumDisplayEvents; event++) {
    close_(poll_fds_[event].fd);
  }

  return kErrorNone;
}

DisplayError HWPrimary::Open(HWEventHandler *eventhandler) {
  return HWDevice::Open(eventhandler);
}

DisplayError HWPrimary::Close() {
  return HWDevice::Close();
}

DisplayError HWPrimary::GetNumDisplayAttributes(uint32_t *count) {
  return HWDevice::GetNumDisplayAttributes(count);
}

DisplayError HWPrimary::GetDisplayAttributes(HWDisplayAttributes *display_attributes,
                                             uint32_t index) {
  DTRACE_SCOPED();

  // Variable screen info
  STRUCT_VAR(fb_var_screeninfo, var_screeninfo);

  if (ioctl_(device_fd_, FBIOGET_VSCREENINFO, &var_screeninfo) < 0) {
    IOCTL_LOGE(FBIOGET_VSCREENINFO, device_type_);
    return kErrorHardware;
  }

  // Frame rate
  STRUCT_VAR(msmfb_metadata, meta_data);
  meta_data.op = metadata_op_frame_rate;
  if (ioctl_(device_fd_, MSMFB_METADATA_GET, &meta_data) < 0) {
    IOCTL_LOGE(MSMFB_METADATA_GET, device_type_);
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
  display_attributes->h_total = var_screeninfo.xres + var_screeninfo.right_margin +
      var_screeninfo.left_margin + var_screeninfo.hsync_len;
  display_attributes->x_dpi =
      (FLOAT(var_screeninfo.xres) * 25.4f) / FLOAT(var_screeninfo.width);
  display_attributes->y_dpi =
      (FLOAT(var_screeninfo.yres) * 25.4f) / FLOAT(var_screeninfo.height);
  display_attributes->fps = FLOAT(meta_data.data.panel_frame_rate);
  display_attributes->vsync_period_ns = UINT32(1000000000L / display_attributes->fps);
  display_attributes->is_device_split = (hw_panel_info_.split_info.left_split ||
      (var_screeninfo.xres > hw_resource_.max_mixer_width)) ? true : false;
  display_attributes->split_left = hw_panel_info_.split_info.left_split ?
      hw_panel_info_.split_info.left_split : display_attributes->x_pixels / 2;
  display_attributes->always_src_split = hw_panel_info_.split_info.always_src_split;

  return kErrorNone;
}

DisplayError HWPrimary::SetDisplayAttributes(uint32_t index) {
  return HWDevice::SetDisplayAttributes(index);
}

DisplayError HWPrimary::GetConfigIndex(uint32_t mode, uint32_t *index) {
  return HWDevice::GetConfigIndex(mode, index);
}

DisplayError HWPrimary::PowerOn() {
  return HWDevice::PowerOn();
}

DisplayError HWPrimary::PowerOff() {
  if (ioctl_(device_fd_, FBIOBLANK, FB_BLANK_POWERDOWN) < 0) {
    IOCTL_LOGE(FB_BLANK_POWERDOWN, device_type_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWPrimary::Doze() {
  if (ioctl_(device_fd_, FBIOBLANK, FB_BLANK_NORMAL) < 0) {
    IOCTL_LOGE(FB_BLANK_NORMAL, device_type_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWPrimary::DozeSuspend() {
  if (ioctl_(device_fd_, FBIOBLANK, FB_BLANK_VSYNC_SUSPEND) < 0) {
    IOCTL_LOGE(FB_BLANK_VSYNC_SUSPEND, device_type_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWPrimary::Standby() {
  return HWDevice::Standby();
}

DisplayError HWPrimary::Validate(HWLayers *hw_layers) {
  return HWDevice::Validate(hw_layers);
}

DisplayError HWPrimary::Commit(HWLayers *hw_layers) {
  return HWDevice::Commit(hw_layers);
}

DisplayError HWPrimary::Flush() {
  return HWDevice::Flush();
}

DisplayError HWPrimary::GetHWPanelInfo(HWPanelInfo *panel_info) {
  return HWDevice::GetHWPanelInfo(panel_info);
}

void* HWPrimary::DisplayEventThread(void *context) {
  if (context) {
    return reinterpret_cast<HWPrimary *>(context)->DisplayEventThreadHandler();
  }

  return NULL;
}

void* HWPrimary::DisplayEventThreadHandler() {
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
      event_handler_->VSync(ts);
    }

    pthread_exit(0);
  }

  typedef void (HWPrimary::*EventHandler)(char*);
  EventHandler event_handler[kNumDisplayEvents] = { &HWPrimary::HandleVSync,
                                                    &HWPrimary::HandleBlank,
                                                    &HWPrimary::HandleIdleTimeout,
                                                    &HWPrimary::HandleThermal };

  while (!exit_threads_) {
    int error = poll_(poll_fds_, kNumDisplayEvents, -1);
    if (error < 0) {
      DLOGW("poll failed. error = %s", strerror(errno));
      continue;
    }
    for (int event = 0; event < kNumDisplayEvents; event++) {
      pollfd &poll_fd = poll_fds_[event];

      if (poll_fd.revents & POLLPRI) {
        ssize_t length = pread_(poll_fd.fd, data, kMaxStringLength, 0);
        if (length < 0) {
          // If the read was interrupted - it is not a fatal error, just continue.
          DLOGW("pread failed. event = %d, error = %s", event, strerror(errno));
          continue;
        }

        (this->*event_handler[event])(data);
      }
    }
  }

  pthread_exit(0);

  return NULL;
}

void HWPrimary::HandleVSync(char *data) {
  int64_t timestamp = 0;
  if (!strncmp(data, "VSYNC=", strlen("VSYNC="))) {
    timestamp = strtoull(data + strlen("VSYNC="), NULL, 0);
  }
  event_handler_->VSync(timestamp);
}

void HWPrimary::HandleBlank(char *data) {
  // TODO(user): Need to send blank Event
}

void HWPrimary::HandleIdleTimeout(char *data) {
  event_handler_->IdleTimeout();
}

void HWPrimary::HandleThermal(char *data) {
  int64_t thermal_level = 0;
  if (!strncmp(data, "thermal_level=", strlen("thermal_level="))) {
    thermal_level = strtoull(data + strlen("thermal_level="), NULL, 0);
  }

  DLOGI("Received thermal notification with thermal level = %d",thermal_level);

  event_handler_->ThermalEvent(thermal_level);
}

void HWPrimary::SetIdleTimeoutMs(uint32_t timeout_ms) {
  char node_path[kMaxStringLength] = {0};

  DLOGI("Setting idle timeout to = %d ms", timeout_ms);

  snprintf(node_path, sizeof(node_path), "%s%d/idle_time", fb_path_, fb_node_index_);

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

DisplayError HWPrimary::SetVSyncState(bool enable) {
  DTRACE_SCOPED();

  int vsync_on = enable ? 1 : 0;
  if (ioctl_(device_fd_, MSMFB_OVERLAY_VSYNC_CTRL, &vsync_on) < 0) {
    IOCTL_LOGE(MSMFB_OVERLAY_VSYNC_CTRL, device_type_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWPrimary::SetDisplayMode(const HWDisplayMode hw_display_mode) {
  DisplayError error = kErrorNone;
  uint32_t mode = -1;

  switch (hw_display_mode) {
  case kModeVideo:
    mode = kModeLPMVideo;
    break;
  case kModeCommand:
    mode = kModeLPMCommand;
    break;
  default:
    DLOGW("Failed to translate SDE display mode %d to a MSMFB_LPM_ENABLE mode",
          hw_display_mode);
    return kErrorParameters;
  }

  if (ioctl_(device_fd_, MSMFB_LPM_ENABLE, &mode) < 0) {
    IOCTL_LOGE(MSMFB_LPM_ENABLE, device_type_);
    return kErrorHardware;
  }

  DLOGI("Triggering display mode change to %d on next commit.", hw_display_mode);
  synchronous_commit_ = true;

  return kErrorNone;
}

}  // namespace sde

