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
#define __STDC_FORMAT_MACROS
#include <utils/debug.h>

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

#include "hw_framebuffer.h"

#define IOCTL_LOGE(ioctl) DLOGE("ioctl %s, errno = %d, desc = %s", #ioctl, errno, strerror(errno))

#ifdef DISPLAY_CORE_VIRTUAL_DRIVER
extern int virtual_ioctl(int fd, int cmd, ...);
extern int virtual_open(const char *file_name, int access, ...);
extern int virtual_close(int fd);
extern int virtual_poll(struct pollfd *fds,  nfds_t num, int timeout);
extern ssize_t virtual_pread(int fd, void *data, size_t count, off_t offset);
extern FILE* virtual_fopen(const char *fname, const char *mode);
extern int virtual_fclose(FILE* fileptr);
extern ssize_t virtual_getline(char **lineptr, size_t *linelen, FILE *stream);


#endif

namespace sde {

HWFrameBuffer::HWFrameBuffer() : event_thread_name_("SDE_EventThread"), fake_vsync_(false),
                                 exit_threads_(false), fb_path_("/sys/class/graphics/fb") {
  // Pointer to actual driver interfaces.
  ioctl_ = ::ioctl;
  open_ = ::open;
  close_ = ::close;
  poll_ = ::poll;
  pread_ = ::pread;
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
    fopen_ = virtual_fopen;
    fclose_ = virtual_fclose;
    getline_ = virtual_getline;
  }
#endif
  for (int i = 0; i < kHWBlockMax; i ++) {
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

  return kErrorNone;
}

DisplayError HWFrameBuffer::GetHWCapabilities(HWResourceInfo *hw_res_info) {
  *hw_res_info = hw_resource_;

  return kErrorNone;
}

DisplayError HWFrameBuffer::Open(HWBlockType type, Handle *device, HWEventHandler* eventhandler) {
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

  // Store EventHandlers for two Physical displays
  if (device_id < kNumPhysicalDisplays)
    event_handler_[device_id] = eventhandler;

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

DisplayError HWFrameBuffer::SetVSyncState(Handle device, bool enable) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);
  int vsync_on = enable ? 1 : 0;
  if (ioctl_(hw_context->device_fd, MSMFB_OVERLAY_VSYNC_CTRL, &vsync_on) == -1) {
    IOCTL_LOGE(MSMFB_OVERLAY_VSYNC_CTRL);
    return kErrorHardware;
  }

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
      event_handler_[0]->VSync(ts);
    }

    pthread_exit(0);
  }

  typedef void (HWFrameBuffer::*EventHandler)(int, char*);
  EventHandler event_handler[kNumDisplayEvents] = { &HWFrameBuffer::HandleVSync,
                                                    &HWFrameBuffer::HandleBlank };

  while (!exit_threads_) {
    int error = poll_(poll_fds_[0], kNumPhysicalDisplays * kNumDisplayEvents, -1);
    if (error < 0) {
      DLOGE("poll failed errno: %s", strerror(errno));
      continue;
    }

    for (int display = 0; display < kNumPhysicalDisplays; display++) {
      for (int event = 0; event < kNumDisplayEvents; event++) {
        pollfd &poll_fd = poll_fds_[display][event];

        if (poll_fd.revents & POLLPRI) {
          ssize_t length = pread_(poll_fd.fd, data, kMaxStringLength, 0);
          if (length < 0) {
            // If the read was interrupted - it is not a fatal error, just continue.
            DLOGE("Failed to read event:%d for display=%d: %s", event, display, strerror(errno));
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

  return;
}

void HWFrameBuffer::HandleBlank(int display_id, char* data) {
  // TODO(user): Need to send blank Event
  return;
}

void HWFrameBuffer::PopulateFBNodeIndex() {
  char stringbuffer[kMaxStringLength];
  DisplayError error = kErrorNone;
  HWBlockType hwblock = kHWPrimary;
  char *line = stringbuffer;
  size_t len = kMaxStringLength;
  ssize_t read;


  for (int i = 0; i < kHWBlockMax; i++) {
    snprintf(stringbuffer, sizeof(stringbuffer), "%s%d/msm_fb_type", fb_path_, i);
    FILE* fileptr = fopen_(stringbuffer, "r");
    if (fileptr == NULL) {
      DLOGE("File not found %s", stringbuffer);
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
      hwblock = kHWPrimary;
    } else if ((strncmp(line, "mipi dsi video panel", strlen("mipi dsi video panel")) == 0))  {
      pri_panel_info_.type = kVideoModePanel;
      hwblock = kHWPrimary;
    } else if ((strncmp(line, "lvds panel", strlen("lvds panel")) == 0)) {
      pri_panel_info_.type = kLVDSPanel;
      hwblock = kHWPrimary;
    } else if ((strncmp(line, "edp panel", strlen("edp panel")) == 0)) {
      pri_panel_info_.type = kEDPPanel;
      hwblock = kHWPrimary;
    } else if ((strncmp(line, "dtv panel", strlen("dtv panel")) == 0)) {
      hwblock = kHWHDMI;
    } else if ((strncmp(line, "writeback panel", strlen("writeback panel")) == 0)) {
      hwblock = kHWWriteback0;
    } else {
      DLOGE("Unknown panel type = %s index = %d", line, i);
    }
    fb_node_index_[hwblock] = i;
    fclose_(fileptr);
  }
}

void HWFrameBuffer::PopulatePanelInfo(int fb_index) {
  char stringbuffer[kMaxStringLength];
  FILE* fileptr = NULL;
  snprintf(stringbuffer, sizeof(stringbuffer), "%s%d/msm_fb_panel_info", fb_path_, fb_index);
  fileptr = fopen_(stringbuffer, "r");
  if (fileptr == NULL) {
    DLOGE("Failed to open msm_fb_panel_info node");
    return;
  }

  size_t len = kMaxStringLength;
  ssize_t read;
  char *line = stringbuffer;
  while ((read = getline_(&line, &len, fileptr)) != -1) {
    int token_count = 0;
    const int max_count = 10;
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
  int token_count = 0;
  const int max_count = 10;
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
      } else if (!strncmp(tokens[0], "features", strlen("features"))) {
        for (int i = 0; i < token_count; i++) {
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

  DLOGI("SDE Version: %d SDE Revision: %x RGB : %d, VIG: %d DMA: %d Cursor: %d",
        hw_resource_.hw_version, hw_resource_.hw_revision, hw_resource_.num_rgb_pipe,
        hw_resource_.num_vig_pipe, hw_resource_.num_dma_pipe, hw_resource_.num_cursor_pipe);
  DLOGI("Upscale Ratio: %d Downscale Ratio: %d Blending Stages: %d", hw_resource_.max_scale_up,
        hw_resource_.max_scale_down, hw_resource_.num_blending_stages);
  DLOGI("BWC: %d Decimation: %d Tile Format: %d: Rotator Downscale: %d",  hw_resource_.has_bwc,
        hw_resource_.has_decimation, hw_resource_.has_macrotile,
        hw_resource_.has_rotator_downscale);
  DLOGI("Left Split: %d Right Split: %d", hw_resource_.split_info.left_split,
        hw_resource_.split_info.right_split);
  DLOGI("SourceSplit: %d Always: %d", hw_resource_.is_src_split, hw_resource_.always_src_split);
  DLOGI("MaxLowBw: %"PRIu64" MaxHighBw: %"PRIu64"", hw_resource_.max_bandwidth_low,
        hw_resource_.max_bandwidth_high);

  return error;
}

int HWFrameBuffer::ParseLine(char *input, char *tokens[], int max_token, int *count) {
  char *tmp_token = NULL;
  char *temp_ptr;
  int index = 0;
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

}  // namespace sde
