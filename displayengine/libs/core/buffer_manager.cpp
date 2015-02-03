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

#include <utils/debug.h>
#include <utils/constants.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "buffer_manager.h"

#define __CLASS__ "BufferManager"

namespace sde {

// --------------------------------- BufferSlot Implementation -------------------------------------

DisplayError BufferManager::BufferSlot::Init() {
  uint32_t buffer_count = hw_buffer_info.buffer_config.buffer_count;
  size_t buffer_size = hw_buffer_info.alloc_buffer_info.size;

  release_fd = new int[buffer_count];
  if (release_fd == NULL) {
    return kErrorMemory;
  }

  offset = new uint32_t[buffer_count];
  if (offset == NULL) {
    delete[] release_fd;
    release_fd = NULL;
    return kErrorMemory;
  }

  for (uint32_t idx = 0; idx < buffer_count; idx++) {
    release_fd[idx] = -1;
    offset[idx] = UINT32((buffer_size / buffer_count) * idx);
  }
  curr_index = 0;

  return kErrorNone;
}

DisplayError BufferManager::BufferSlot::Deinit() {
  uint32_t buffer_count = hw_buffer_info.buffer_config.buffer_count;

  for (uint32_t idx = 0; idx < buffer_count; idx++) {
    if (release_fd[idx] >= 0) {
      close(release_fd[idx]);
      release_fd[idx] = -1;
    }
  }

  if (offset) {
    delete[] offset;
    offset = NULL;
  }

  if (release_fd) {
    delete[] release_fd;
    release_fd = NULL;
  }

  state = kBufferSlotFree;
  hw_buffer_info = HWBufferInfo();

  return kErrorNone;
}

// BufferManager State Transition
// *******************************************************
// Current State *             Next State
//               *****************************************
//               *   FREE       READY        ACQUIRED
// *******************************************************
//  FREE         *    NA         NA        GetNextBuffer()
//  READY        *   Stop()      NA        GetNextBuffer()
//  ACQUIRED     *    NA       Start()          NA
//********************************************************

// ------------------------------- BufferManager Implementation ------------------------------------

BufferManager::BufferManager(BufferAllocator *buffer_allocator,
                             BufferSyncHandler *buffer_sync_handler)
    : buffer_allocator_(buffer_allocator), buffer_sync_handler_(buffer_sync_handler),
      num_used_slot_(0) {
}

void BufferManager::Start() {
  uint32_t slot = 0, num_ready_slot = 0;

  // Change the state of acquired buffer_slot to kBufferSlotReady
  while ((num_ready_slot < num_used_slot_) && (slot < kMaxBufferSlotCount)) {
    if (buffer_slot_[slot].state == kBufferSlotFree) {
      slot++;
      continue;
    }

    buffer_slot_[slot++].state = kBufferSlotReady;
    num_ready_slot++;
  }
}

DisplayError BufferManager::GetNextBuffer(HWBufferInfo *hw_buffer_info) {
  DisplayError error = kErrorNone;
  const BufferConfig &buffer_config = hw_buffer_info->buffer_config;

  DLOGI_IF(kTagBufferManager, "Input: w = %d h = %d f = %d", buffer_config.width,
           buffer_config.height, buffer_config.format);

  uint32_t free_slot = num_used_slot_;
  uint32_t acquired_slot = kMaxBufferSlotCount;
  uint32_t num_used_slot = 0;

  // First look for a buffer slot in ready state, if no buffer slot found in ready state matching
  // with current input config, assign a buffer slot in free state and allocate buffers for it.
  for (uint32_t slot = 0; slot < kMaxBufferSlotCount && num_used_slot < num_used_slot_; slot++) {
    HWBufferInfo &hw_buffer_info = buffer_slot_[slot].hw_buffer_info;

    if (buffer_slot_[slot].state == kBufferSlotFree) {
      free_slot = slot;
    } else {
      if ((buffer_slot_[slot].state == kBufferSlotReady)) {
        if ((::memcmp(&buffer_config, &hw_buffer_info.buffer_config, sizeof(BufferConfig)) == 0)) {
          buffer_slot_[slot].state = kBufferSlotAcquired;
          acquired_slot = slot;
          break;
        }
      }
      num_used_slot++;
    }
  }

  // If the input config does not match with existing config, then allocate buffers for the new
  // buffer slot and change the state to kBufferSlotAcquired
  if (acquired_slot == kMaxBufferSlotCount) {
    if (free_slot >= kMaxBufferSlotCount) {
      return kErrorMemory;
    }

    buffer_slot_[free_slot].hw_buffer_info.buffer_config = hw_buffer_info->buffer_config;

    error = buffer_allocator_->AllocateBuffer(&buffer_slot_[free_slot].hw_buffer_info);
    if (error != kErrorNone) {
      return error;
    }

    buffer_slot_[free_slot].Init();
    buffer_slot_[free_slot].state = kBufferSlotAcquired;
    acquired_slot = free_slot;
    num_used_slot_++;

    DLOGI_IF(kTagBufferManager, "Allocate Buffer acquired_slot = %d ", acquired_slot);
  }

  const AllocatedBufferInfo &alloc_buffer_info =
    buffer_slot_[acquired_slot].hw_buffer_info.alloc_buffer_info;
  uint32_t curr_index = buffer_slot_[acquired_slot].curr_index;

  // Wait for the release fence fd before buffer slot being given to the client.
  buffer_sync_handler_->SyncWait(buffer_slot_[acquired_slot].release_fd[curr_index]);
  buffer_slot_[acquired_slot].release_fd[curr_index] = -1;

  hw_buffer_info->output_buffer.width = buffer_config.width;
  hw_buffer_info->output_buffer.height = buffer_config.height;
  hw_buffer_info->output_buffer.format = buffer_config.format;
  hw_buffer_info->output_buffer.flags.secure = buffer_config.secure;

  hw_buffer_info->output_buffer.planes[0].stride = alloc_buffer_info.stride;
  hw_buffer_info->output_buffer.planes[0].fd = alloc_buffer_info.fd;
  hw_buffer_info->output_buffer.planes[0].offset = buffer_slot_[acquired_slot].offset[curr_index];
  hw_buffer_info->slot = acquired_slot;

  DLOGI_IF(kTagBufferManager, "Output: w = %d h = %d f = %d session_id %d acquired slot = %d " \
           "num_used_slot %d curr_index = %d offset %d", hw_buffer_info->output_buffer.width,
           hw_buffer_info->output_buffer.height, hw_buffer_info->output_buffer.format,
           hw_buffer_info->session_id, acquired_slot, num_used_slot_, curr_index,
           hw_buffer_info->output_buffer.planes[0].offset);

  return kErrorNone;
}

DisplayError BufferManager::Stop(int *session_ids) {
  DisplayError error = kErrorNone;
  uint32_t slot = 0, count = 0;

  // Free all the buffer slots which were not acquired in the current cycle and deallocate the
  // buffers associated with it.
  while ((num_used_slot_ > 0) && (slot < kMaxBufferSlotCount)) {
    if (buffer_slot_[slot].state == kBufferSlotReady) {
      if (buffer_slot_[slot].hw_buffer_info.session_id != -1) {
        session_ids[count++] = buffer_slot_[slot].hw_buffer_info.session_id;
      }

      error = FreeBufferSlot(slot);
      if (error != kErrorNone) {
        return error;
      }

      num_used_slot_--;
    }
    slot++;
  }

  session_ids[count] = -1;

  return kErrorNone;
}

DisplayError BufferManager::SetReleaseFd(uint32_t slot, int fd) {
  if ((slot >= kMaxBufferSlotCount) || (buffer_slot_[slot].state != kBufferSlotAcquired)) {
    DLOGE("Invalid Parameters slot %d state %s", slot, buffer_slot_[slot].state);
    kErrorParameters;
  }

  uint32_t &curr_index = buffer_slot_[slot].curr_index;
  const HWBufferInfo &hw_buffer_info = buffer_slot_[slot].hw_buffer_info;
  uint32_t buffer_count = hw_buffer_info.buffer_config.buffer_count;

  // 1. Store the release fence fd, so that buffer manager waits for the release fence fd to be
  //    signaled and gives the buffer slot to the client.
  // 2. Modify the curr_index to point to next buffer.
  buffer_slot_[slot].release_fd[curr_index] = fd;
  curr_index = (curr_index + 1) % buffer_count;

  DLOGI_IF(kTagBufferManager, "w = %d h = %d f = %d session_id %d slot = %d curr_index = %d " \
           "sync fd %d", hw_buffer_info.output_buffer.width, hw_buffer_info.output_buffer.height,
           hw_buffer_info.output_buffer.format, hw_buffer_info.session_id, slot, curr_index, fd);

  return kErrorNone;
}


DisplayError BufferManager::SetSessionId(uint32_t slot, int session_id) {
  if ((slot >= kMaxBufferSlotCount) || (buffer_slot_[slot].state != kBufferSlotAcquired)) {
    DLOGE("Invalid Parameters slot %d state %s", slot, buffer_slot_[slot].state);
    kErrorParameters;
  }

  HWBufferInfo *hw_buffer_info = &buffer_slot_[slot].hw_buffer_info;

  hw_buffer_info->session_id = session_id;

  DLOGI_IF(kTagBufferManager, "w = %d h = %d f = %d session_id %d slot = %d",
           hw_buffer_info->output_buffer.width, hw_buffer_info->output_buffer.height,
           hw_buffer_info->output_buffer.format, hw_buffer_info->session_id, slot);

  return kErrorNone;
}

DisplayError BufferManager::FreeBufferSlot(uint32_t slot) {
  DisplayError error = kErrorNone;

  HWBufferInfo *hw_buffer_info = &buffer_slot_[slot].hw_buffer_info;

  error = buffer_allocator_->FreeBuffer(hw_buffer_info);
  if (error != kErrorNone) {
    return error;
  }

  DLOGI_IF(kTagBufferManager, "session_id %d slot = %d num_used_slot %d",
           hw_buffer_info->session_id, slot, num_used_slot_);

  buffer_slot_[slot].Deinit();

  return kErrorNone;
}

}  // namespace sde
