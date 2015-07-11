/* Copyright (c) 2015, The Linux Foundataion. All rights reserved.
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
*
*/

#ifndef __CPUHINT_H__
#define __CPUHINT_H__

#include <core/sdm_types.h>

namespace sdm {

class HWCDebugHandler;

class CPUHint {
 public:
  CPUHint();
  ~CPUHint();
  DisplayError Init(HWCDebugHandler *debug_handler);
  void Set();
  void Reset();

 private:
  enum { HINT =  0x4701 /* 47-display layer hint, 01-Enable */ };
  bool enabled_;
  // frames to wait before setting this hint
  int pre_enable_window_;
  int frame_countdown_;
  int lock_handle_;
  bool lock_acquired_;
  void *lib_handle_;
  int (*fn_lock_acquire_)(int handle, int duration, int *hints, int num_args);
  int (*fn_lock_release_)(int value);
};

}  // namespace sdm

#endif  // __CPUHINT_H__
