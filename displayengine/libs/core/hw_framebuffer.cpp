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

// DISPLAY_LOG_TAG definition must precede logger.h include.
#define DISPLAY_LOG_TAG kTagCore
#define DISPLAY_MODULE_NAME "HWFrameBuffer"
#include <utils/logger.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <utils/constants.h>
#include <utils/debug.h>

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

DisplayError HWFrameBuffer::GetCapabilities(HWResourceInfo *hw_res_info) {
  return kErrorNone;
}

DisplayError HWFrameBuffer::Open(HWInterfaceType type, Handle *device) {
  DisplayError error = kErrorNone;

  HWContext *hw_context = new HWContext();
  if (UNLIKELY(!hw_context)) {
    return kErrorMemory;
  }

  *device = hw_context;
  return kErrorNone;
}

DisplayError HWFrameBuffer::Close(Handle device) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  delete hw_context;
  return kErrorNone;
}

DisplayError HWFrameBuffer::GetConfig(Handle device, DeviceConfigVariableInfo *variable_info) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

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

DisplayError HWFrameBuffer::Prepare(Handle device, HWLayers *hw_layers) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  return kErrorNone;
}

DisplayError HWFrameBuffer::Commit(Handle device, HWLayers *hw_layers) {
  HWContext *hw_context = reinterpret_cast<HWContext *>(device);

  return kErrorNone;
}

}  // namespace sde

