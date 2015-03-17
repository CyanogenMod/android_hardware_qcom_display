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

#include "session_manager.h"
#include "hw_rotator_interface.h"

#define __CLASS__ "SessionManager"

namespace sde {

// SessionManager State Transition
// *******************************************************
// Current State *             Next State
//               *****************************************
//               * RELEASED    READY        ACQUIRED
// *******************************************************
//  RELEASED     *    NA         NA        OpenSession()
//  READY        *   Stop()      NA        OpenSession()
//  ACQUIRED     *    NA       Start()          NA
//********************************************************

// ------------------------------- SessionManager Implementation -----------------------------------

SessionManager::SessionManager(HWRotatorInterface *hw_rotator_intf,
                               BufferAllocator *buffer_allocator,
                               BufferSyncHandler *buffer_sync_handler)
  : hw_rotator_intf_(hw_rotator_intf), buffer_allocator_(buffer_allocator),
    buffer_sync_handler_(buffer_sync_handler), active_session_count_(0) {
}

void SessionManager::Start() {
  uint32_t session_count = 0;
  uint32_t ready_session_count = 0;

  // Change the state of acquired session to kSessionReady
  while ((ready_session_count < active_session_count_) && (session_count < kMaxSessionCount)) {
    if (session_list_[session_count].state == kSessionReleased) {
      session_count++;
      continue;
    }

    session_list_[session_count++].state = kSessionReady;
    ready_session_count++;
  }
}

DisplayError SessionManager::OpenSession(HWRotatorSession *hw_rotator_session) {
  DisplayError error = kErrorNone;

  const HWSessionConfig &input_config = hw_rotator_session->hw_session_config;

  DLOGI_IF(kTagRotator, "Src buffer: width = %d, height = %d, format = %d",
           input_config.src_width, input_config.src_height, input_config.src_format);
  DLOGI_IF(kTagRotator, "Dst buffer: width = %d, height = %d, format = %d",
           input_config.dst_width, input_config.dst_height, input_config.dst_format);
  DLOGI_IF(kTagRotator, "buffer_count = %d, secure = %d, cache = %d, frame_rate = %d",
           input_config.buffer_count, input_config.secure, input_config.cache,
           input_config.frame_rate);

  uint32_t free_session = active_session_count_;
  uint32_t acquired_session = kMaxSessionCount;
  uint32_t ready_session_count = 0;

  // First look for a session in ready state, if no session found in ready state matching
  // with current input session config, assign a session in released state.
  for (uint32_t session_count = 0; session_count < kMaxSessionCount &&
       ready_session_count < active_session_count_; session_count++) {
    HWSessionConfig &hw_session_config =
           session_list_[session_count].hw_rotator_session.hw_session_config;

    if (session_list_[session_count].state == kSessionReleased) {
      free_session = session_count;
      continue;
    }

    if (session_list_[session_count].state != kSessionReady) {
      continue;
    }

    if (input_config == hw_session_config) {
      session_list_[session_count].state = kSessionAcquired;
      acquired_session = session_count;
      break;
    }

    ready_session_count++;
  }

  // If the input config does not match with existing config, then add new session and change the
  // state to kSessionAcquired
  if (acquired_session == kMaxSessionCount) {
    if (free_session >= kMaxSessionCount) {
      return kErrorMemory;
    }

    error = AcquireSession(hw_rotator_session, &session_list_[free_session]);
    if (error !=kErrorNone) {
      return error;
    }

    acquired_session = free_session;
    hw_rotator_session->session_id = acquired_session;
    active_session_count_++;

    DLOGV_IF(kTagRotator, "Acquire new session Output: width = %d, height = %d, format = %d, " \
             "session_id %d", hw_rotator_session->output_buffer.width,
             hw_rotator_session->output_buffer.height, hw_rotator_session->output_buffer.format,
             hw_rotator_session->session_id);

    return kErrorNone;
  }

  hw_rotator_session->output_buffer.width = input_config.dst_width;
  hw_rotator_session->output_buffer.height = input_config.dst_height;
  hw_rotator_session->output_buffer.format = input_config.dst_format;
  hw_rotator_session->output_buffer.flags.secure = input_config.secure;
  hw_rotator_session->session_id = acquired_session;

  DLOGV_IF(kTagRotator, "Acquire existing session Output: width = %d, height = %d, format = %d, " \
           "session_id %d", hw_rotator_session->output_buffer.width,
           hw_rotator_session->output_buffer.height, hw_rotator_session->output_buffer.format,
           hw_rotator_session->session_id);

  return kErrorNone;
}

DisplayError SessionManager::GetNextBuffer(HWRotatorSession *hw_rotator_session) {
  DisplayError error = kErrorNone;

  int session_id = hw_rotator_session->session_id;
  if (session_id > kMaxSessionCount) {
    return kErrorParameters;
  }

  Session *session = &session_list_[session_id];
  if (session->state != kSessionAcquired) {
    DLOGE("Invalid session state %d", session->state);
    kErrorParameters;
  }

  uint32_t curr_index = session->curr_index;

  BufferInfo *buffer_info = &session->buffer_info;
  if (buffer_info->alloc_buffer_info.fd < 0) {
    const uint32_t &buffer_count = buffer_info->buffer_config.buffer_count;
    const size_t &buffer_size = buffer_info->alloc_buffer_info.size;

    error = buffer_allocator_->AllocateBuffer(buffer_info);
    if (error != kErrorNone) {
      return error;
    }

    for (uint32_t idx = 0; idx < buffer_count; idx++) {
      session->offset[idx] = UINT32((buffer_size / buffer_count) * idx);
    }
  }

  // Wait for the release fence fd before the session being given to the client.
  buffer_sync_handler_->SyncWait(session->release_fd[curr_index]);
  close(session->release_fd[curr_index]);
  session->release_fd[curr_index] = -1;

  hw_rotator_session->output_buffer.planes[0].stride = buffer_info->alloc_buffer_info.stride;
  hw_rotator_session->output_buffer.planes[0].fd = buffer_info->alloc_buffer_info.fd;
  hw_rotator_session->output_buffer.planes[0].offset = session->offset[curr_index];

  DLOGI_IF(kTagRotator, "Output: width = %d, height = %d, format = %d, stride %d, " \
           "curr_index = %d, offset %d, fd %d, session_id %d,",
           hw_rotator_session->output_buffer.width, hw_rotator_session->output_buffer.height,
           hw_rotator_session->output_buffer.format,
           hw_rotator_session->output_buffer.planes[0].stride, curr_index,
           hw_rotator_session->output_buffer.planes[0].offset,
           hw_rotator_session->output_buffer.planes[0].fd, hw_rotator_session->session_id);

  return kErrorNone;
}

DisplayError SessionManager::Stop() {
  DisplayError error = kErrorNone;
  uint32_t session_id = 0;

  // Release all the sessions which were not acquired in the current cycle and deallocate the
  // buffers associated with it.
  while ((active_session_count_ > 0) && (session_id < kMaxSessionCount)) {
    if (session_list_[session_id].state == kSessionReady) {
      error = ReleaseSession(&session_list_[session_id]);
      if (error != kErrorNone) {
        return error;
      }
      active_session_count_--;

      DLOGI_IF(kTagRotator, "session_id = %d, active_session_count = %d", session_id,
               active_session_count_);
    }
    session_id++;
  }

  return kErrorNone;
}

DisplayError SessionManager::SetReleaseFd(HWRotatorSession *hw_rotator_session) {
  int session_id = hw_rotator_session->session_id;
  if (session_id > kMaxSessionCount) {
    return kErrorParameters;
  }

  Session *session = &session_list_[session_id];
  if (session->state != kSessionAcquired) {
    DLOGE("Invalid session state %d", session->state);
    kErrorParameters;
  }

  uint32_t &curr_index = session->curr_index;
  uint32_t buffer_count = hw_rotator_session->hw_session_config.buffer_count;

  // 1. Store the release fence fd, so that session manager waits for the release fence fd
  //    to be signaled and populates the session info to the client.
  // 2. Modify the curr_index to point to next buffer.
  session->release_fd[curr_index] = hw_rotator_session->output_buffer.release_fence_fd;

  curr_index = (curr_index + 1) % buffer_count;

  DLOGI_IF(kTagRotator, "session_id %d, curr_index = %d, release fd %d", session_id,
           curr_index, hw_rotator_session->output_buffer.release_fence_fd);

  return kErrorNone;
}

DisplayError SessionManager::AcquireSession(HWRotatorSession *hw_rotator_session,
                                            Session *session) {
  DisplayError error = kErrorNone;
  const HWSessionConfig &input_config = hw_rotator_session->hw_session_config;

  error = hw_rotator_intf_->OpenSession(hw_rotator_session);
  if (error != kErrorNone) {
    return error;
  }

  hw_rotator_session->output_buffer = LayerBuffer();
  hw_rotator_session->output_buffer.width = input_config.dst_width;
  hw_rotator_session->output_buffer.height = input_config.dst_height;
  hw_rotator_session->output_buffer.format = input_config.dst_format;
  hw_rotator_session->output_buffer.flags.secure = input_config.secure;

  uint32_t buffer_count = hw_rotator_session->hw_session_config.buffer_count;

  session->release_fd = new int[buffer_count];
  if (session->release_fd == NULL) {
    return kErrorMemory;
  }

  session->offset = new uint32_t[buffer_count];
  if (session->offset == NULL) {
    delete[] session->release_fd;
    session->release_fd = NULL;
    return kErrorMemory;
  }

  for (uint32_t idx = 0; idx < buffer_count; idx++) {
    session->release_fd[idx] = -1;
    session->offset[idx] = 0;
  }
  session->curr_index = 0;

  session->hw_rotator_session = HWRotatorSession();
  session->buffer_info = BufferInfo();

  BufferInfo *buffer_info = &session->buffer_info;
  buffer_info->buffer_config.buffer_count = hw_rotator_session->hw_session_config.buffer_count;
  buffer_info->buffer_config.secure = hw_rotator_session->hw_session_config.secure;
  buffer_info->buffer_config.cache = hw_rotator_session->hw_session_config.cache;
  buffer_info->buffer_config.width = hw_rotator_session->hw_session_config.dst_width;
  buffer_info->buffer_config.height = hw_rotator_session->hw_session_config.dst_height;
  buffer_info->buffer_config.format = hw_rotator_session->hw_session_config.dst_format;

  session->state = kSessionAcquired;
  session->hw_rotator_session = *hw_rotator_session;

  return kErrorNone;
}

DisplayError SessionManager::ReleaseSession(Session *session) {
  DisplayError error = kErrorNone;

  BufferInfo *buffer_info = &session->buffer_info;

  error = buffer_allocator_->FreeBuffer(buffer_info);
  if (error != kErrorNone) {
    return error;
  }

  error = hw_rotator_intf_->CloseSession(&session->hw_rotator_session);
  if (error != kErrorNone) {
    return error;
  }

  uint32_t buffer_count = buffer_info->buffer_config.buffer_count;

  for (uint32_t idx = 0; idx < buffer_count; idx++) {
    if (session->release_fd[idx] >= 0) {
      close(session->release_fd[idx]);
      session->release_fd[idx] = -1;
    }
  }
  session->state = kSessionReleased;

  if (session->offset) {
    delete[] session->offset;
    session->offset = NULL;
  }

  if (session->release_fd) {
    delete[] session->release_fd;
    session->release_fd = NULL;
  }

  return kErrorNone;
}

}  // namespace sde
