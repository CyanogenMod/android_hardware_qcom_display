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

#ifndef __OFFLINE_CTRL_H__
#define __OFFLINE_CTRL_H__

#include <utils/locker.h>
#include <utils/debug.h>

#include "hw_interface.h"

namespace sde {

class OfflineCtrl {
 public:
  OfflineCtrl();
  DisplayError Init(HWInterface *hw_intf, HWResourceInfo hw_res_info);
  DisplayError Deinit();
  DisplayError RegisterDisplay(DisplayType type, Handle *display_ctx);
  void UnregisterDisplay(Handle display_ctx);
  DisplayError Prepare(Handle display_ctx, HWLayers *hw_layers);
  DisplayError Commit(Handle display_ctx, HWLayers *hw_layers);

 private:
  struct DisplayOfflineContext {
    DisplayType display_type;
    bool pending_rot_commit;

    DisplayOfflineContext() : display_type(kPrimary), pending_rot_commit(false) { }
  };

  bool IsRotationRequired(HWLayers *hw_layers);

  HWInterface *hw_intf_;
  Handle hw_rotator_device_;
};

}  // namespace sde

#endif  // __OFFLINE_CTRL_H__

