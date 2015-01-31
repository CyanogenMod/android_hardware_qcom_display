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

#include <utils/constants.h>
#include <gralloc_priv.h>
#include <sync/sync.h>

#include "hwc_display_virtual.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCDisplayVirtual"

namespace sde {

HWCDisplayVirtual::HWCDisplayVirtual(CoreInterface *core_intf, hwc_procs_t const **hwc_procs)
  : HWCDisplay(core_intf, hwc_procs, kVirtual, HWC_DISPLAY_VIRTUAL),
    dump_output_layer_(false) {
}

int HWCDisplayVirtual::Init() {
  int status = 0;

  output_buffer_ = new LayerBuffer();
  if (!output_buffer_) {
    return -ENOMEM;
  }

  return HWCDisplay::Init();
}

int HWCDisplayVirtual::Deinit() {
  int status = 0;

  status = HWCDisplay::Deinit();
  if (status) {
    return status;
  }

  if (output_buffer_) {
    delete output_buffer_;
  }

  return status;
}

int HWCDisplayVirtual::Prepare(hwc_display_contents_1_t *content_list) {
  int status = 0;
  status = AllocateLayerStack(content_list);
  if (status) {
    return status;
  }

  status = SetOutputBuffer(content_list);
  if (status) {
    return status;
  }

  status = PrepareLayerStack(content_list);
  if (status) {
    return status;
  }

  return 0;
}

int HWCDisplayVirtual::Commit(hwc_display_contents_1_t *content_list) {
  int status = 0;

  status = HWCDisplay::CommitLayerStack(content_list);
  if (status) {
    return status;
  }

  DumpOutputBuffer(content_list);

  status = HWCDisplay::PostCommitLayerStack(content_list);
  if (status) {
    return status;
  }

  if (content_list->outbufAcquireFenceFd >= 0) {
    close(content_list->outbufAcquireFenceFd);
    content_list->outbufAcquireFenceFd = -1;
  }

  return 0;
}

int HWCDisplayVirtual::SetActiveConfig(hwc_display_contents_1_t *content_list) {
  const private_handle_t *output_handle =
        static_cast<const private_handle_t *>(content_list->outbuf);
  DisplayError error = kErrorNone;
  int status = 0;

  if (output_handle) {
    LayerBufferFormat format = GetSDEFormat(output_handle->format, output_handle->flags);
    if (format == kFormatInvalid) {
      return -EINVAL;
    }

    if ((output_handle->width != INT(output_buffer_->width)) ||
        (output_handle->height != INT(output_buffer_->height)) ||
        (format != output_buffer_->format)) {
      DisplayConfigVariableInfo variable_info;

      variable_info.x_pixels = output_handle->width;
      variable_info.y_pixels = output_handle->height;
      // TODO(user): Need to get the framerate of primary display and update it.
      variable_info.fps = 60;

      error = display_intf_->SetActiveConfig(&variable_info);
      if (error != kErrorNone) {
        return -EINVAL;
      }

      status = SetOutputBuffer(content_list);
      if (status) {
        return status;
      }
    }
  }

  return 0;
}

int HWCDisplayVirtual::SetOutputBuffer(hwc_display_contents_1_t *content_list) {
  int status = 0;

  const private_handle_t *output_handle =
        static_cast<const private_handle_t *>(content_list->outbuf);

  // Fill output buffer parameters (width, height, format, plane information, fence)
  output_buffer_->acquire_fence_fd = content_list->outbufAcquireFenceFd;

  if (output_handle) {
    output_buffer_->format = GetSDEFormat(output_handle->format, output_handle->flags);
    if (output_buffer_->format == kFormatInvalid) {
      return -EINVAL;
    }

    output_buffer_->width = output_handle->width;
    output_buffer_->height = output_handle->height;
    output_buffer_->flags.secure = 0;
    output_buffer_->flags.video = 0;

    // ToDo: Need to extend for non-RGB formats
    output_buffer_->planes[0].fd = output_handle->fd;
    output_buffer_->planes[0].offset = output_handle->offset;
    output_buffer_->planes[0].stride = output_handle->width;
  }

  layer_stack_.output_buffer = output_buffer_;

  return status;
}

void HWCDisplayVirtual::DumpOutputBuffer(hwc_display_contents_1_t *content_list) {
  const private_handle_t *output_handle = (const private_handle_t *)(content_list->outbuf);
  char dir_path[PATH_MAX];

  if (!dump_frame_count_ || flush_ || !dump_output_layer_) {
    return;
  }

  snprintf(dir_path, sizeof(dir_path), "/data/misc/display/frame_dump_%s", GetDisplayString());

  if (mkdir(dir_path, 777) != 0 && errno != EEXIST) {
    DLOGW("Failed to create %s directory errno = %d, desc = %s", dir_path, errno, strerror(errno));
    return;
  }

  // if directory exists already, need to explicitly change the permission.
  if (errno == EEXIST && chmod(dir_path, 0777) != 0) {
    DLOGW("Failed to change permissions on %s directory", dir_path);
    return;
  }

  if (output_handle && output_handle->base) {
    char dump_file_name[PATH_MAX];
    size_t result = 0;

    if (content_list->outbufAcquireFenceFd >= 0) {
      int error = sync_wait(content_list->outbufAcquireFenceFd, 1000);
      if (error < 0) {
        DLOGW("sync_wait error errno = %d, desc = %s", errno,  strerror(errno));
        return;
      }
    }

    snprintf(dump_file_name, sizeof(dump_file_name), "%s/output_layer_%dx%d_%s_frame%d.raw",
             dir_path, output_handle->width, output_handle->height,
             GetHALPixelFormatString(output_handle->format), dump_frame_index_);

    FILE* fp = fopen(dump_file_name, "w+");
    if (fp) {
      result = fwrite(reinterpret_cast<void *>(output_handle->base), output_handle->size, 1, fp);
      fclose(fp);
    }

    DLOGI("Frame Dump of %s is %s", dump_file_name, result ? "Successful" : "Failed");
  }
}

void HWCDisplayVirtual::SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type) {
  HWCDisplay::SetFrameDumpConfig(count, bit_mask_layer_type);
  dump_output_layer_ = ((bit_mask_layer_type & (1 << OUTPUT_LAYER_DUMP)) != 0);

  DLOGI("output_layer_dump_enable %d", dump_output_layer_);
}

}  // namespace sde

