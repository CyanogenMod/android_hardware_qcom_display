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

#ifndef __ROTATOR_CTRL_H__
#define __ROTATOR_CTRL_H__

#include <utils/locker.h>
#include <utils/debug.h>
#include <core/display_interface.h>

namespace sde {

class HWRotatorInterface;
class BufferAllocator;
class BufferSyncHandler;
struct HWLayers;
class SessionManager;

class RotatorCtrl {
 public:
  RotatorCtrl();
  DisplayError Init(BufferAllocator *buffer_allocator, BufferSyncHandler *buffer_sync_handler);
  DisplayError Deinit();
  DisplayError RegisterDisplay(DisplayType type, Handle *display_ctx);
  void UnregisterDisplay(Handle display_ctx);
  DisplayError Prepare(Handle display_ctx, HWLayers *hw_layers);
  DisplayError Commit(Handle display_ctx, HWLayers *hw_layers);
  DisplayError PostCommit(Handle display_ctx, HWLayers *hw_layers);

 private:
  enum {
    kSingleBuffering = 1,
    kDoubleBuffering = 2,
    kTripleBuffering = 3,
  };

  struct DisplaRotatorContext {
    DisplayType display_type;

    DisplaRotatorContext() : display_type(kPrimary) { }
  };

  DisplayError PrepareSessions(HWLayers *hw_layers);
  DisplayError GetOutputBuffers(HWLayers *hw_layers);

  HWRotatorInterface *hw_rotator_intf_;
  SessionManager *session_manager_;
};

}  // namespace sde

#endif  // __ROTATOR_CTRL_H__

