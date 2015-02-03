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

#include "hw_interface.h"
#include "hw_framebuffer.h"

namespace sde {

DisplayError HWInterface::Create(HWInterface **intf, BufferSyncHandler *buffer_sync_handler) {
  DisplayError error = kErrorNone;
  HWFrameBuffer *hw_frame_buffer = NULL;

  hw_frame_buffer = new HWFrameBuffer(buffer_sync_handler);
  error = hw_frame_buffer->Init();
  if (UNLIKELY(error != kErrorNone)) {
    delete hw_frame_buffer;
  } else {
    *intf = hw_frame_buffer;
  }

  return error;
}

DisplayError HWInterface::Destroy(HWInterface *intf) {
  HWFrameBuffer *hw_frame_buffer = static_cast<HWFrameBuffer *>(intf);
  hw_frame_buffer->Deinit();
  delete hw_frame_buffer;
  return kErrorNone;
}

}  // namespace sde

