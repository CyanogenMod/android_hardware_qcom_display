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

#ifndef __SESSION_MANAGER_H__
#define __SESSION_MANAGER_H__

#include <utils/locker.h>
#include <core/buffer_allocator.h>
#include "hw_interface.h"

namespace sde {

class HWRotatorInterface;

class SessionManager {
 public:
  SessionManager(HWRotatorInterface *hw_intf, BufferAllocator *buffer_allocator,
                 BufferSyncHandler *buffer_sync_handler);

  void Start(const int &client_id);
  DisplayError Stop(const int &client_id);
  DisplayError OpenSession(const int &client_id, HWRotatorSession *hw_rotator_session);
  DisplayError GetNextBuffer(const int &client_id, HWRotatorSession *hw_rotator_session);
  DisplayError SetReleaseFd(const int &client_id, HWRotatorSession *hw_rotator_session);

 private:
  // TODO(user): Read from hw capability instead of hardcoding
  static const int kMaxSessionCount = 32;

  enum SessionState {
    kSessionReleased = 0,
    kSessionReady    = 1,
    kSessionAcquired = 2,
  };

  struct Session {
    HWRotatorSession hw_rotator_session;
    BufferInfo buffer_info;
    SessionState state;
    int *release_fd;
    uint32_t *offset;
    uint32_t curr_index;
    int client_id;

    Session() : state(kSessionReleased), release_fd(NULL), offset(NULL), curr_index(0),
                client_id(-1) { }
  };

  DisplayError AcquireSession(HWRotatorSession *hw_rotator_session, Session *session);
  DisplayError ReleaseSession(Session *session);

  Locker locker_;
  Session session_list_[kMaxSessionCount];
  HWRotatorInterface *hw_rotator_intf_;
  BufferAllocator *buffer_allocator_;
  BufferSyncHandler *buffer_sync_handler_;
  uint32_t active_session_count_;           // number of sessions in ready/acquired state.
};

}  // namespace sde

#endif  // __SESSION_MANAGER_H__


