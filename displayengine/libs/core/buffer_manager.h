/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*  * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
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

#ifndef __BUFFER_MANAGER_H__
#define __BUFFER_MANAGER_H__

#include <utils/locker.h>
#include <core/buffer_allocator.h>
#include "hw_interface.h"

namespace sde {

class BufferManager {
 public:
  BufferManager(BufferAllocator *buffer_allocator, BufferSyncHandler *buffer_sync_handler);

  void Start();
  DisplayError GetNextBuffer(HWBufferInfo *hw_buffer_info);
  DisplayError Stop(int *session_ids);
  DisplayError SetReleaseFd(uint32_t slot, int fd);
  DisplayError SetSessionId(uint32_t slot, int session_id);

 private:
  static const uint32_t kMaxBufferSlotCount = 32;

  enum kBufferSlotState {
    kBufferSlotFree     = 0,
    kBufferSlotReady    = 1,
    kBufferSlotAcquired = 2,
  };

  struct BufferSlot {
    HWBufferInfo hw_buffer_info;
    kBufferSlotState state;
    int *release_fd;
    uint32_t *offset;
    uint32_t curr_index;

    BufferSlot() : state(kBufferSlotFree), release_fd(NULL), offset(NULL), curr_index(0) { }
    DisplayError Init();
    DisplayError Deinit();
  };

  DisplayError FreeBufferSlot(uint32_t index);

  BufferSlot buffer_slot_[kMaxBufferSlotCount];
  BufferAllocator *buffer_allocator_;
  BufferSyncHandler *buffer_sync_handler_;
  uint32_t num_used_slot_;
};

}  // namespace sde

#endif  // __BUFFER_MANAGER_H__


