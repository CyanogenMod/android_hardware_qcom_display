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

#include <utils/locker.h>
#include <utils/constants.h>
#include <utils/debug.h>

#include "core_impl.h"
#include "display_primary.h"
#include "display_hdmi.h"
#include "display_virtual.h"

#define __CLASS__ "CoreImpl"

namespace sde {

CoreImpl::CoreImpl(CoreEventHandler *event_handler, BufferAllocator *buffer_allocator,
                   BufferSyncHandler *buffer_sync_handler)
  : event_handler_(event_handler), buffer_allocator_(buffer_allocator),
    buffer_sync_handler_(buffer_sync_handler), hw_intf_(NULL) {
}

DisplayError CoreImpl::Init() {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  error = HWInterface::Create(&hw_intf_, buffer_sync_handler_);
  if (UNLIKELY(error != kErrorNone)) {
    return error;
  }

  HWResourceInfo hw_res_info;
  error = hw_intf_->GetHWCapabilities(&hw_res_info);
  if (UNLIKELY(error != kErrorNone)) {
    HWInterface::Destroy(hw_intf_);
    return error;
  }

  error = comp_mgr_.Init(hw_res_info, buffer_allocator_, buffer_sync_handler_);
  if (UNLIKELY(error != kErrorNone)) {
    HWInterface::Destroy(hw_intf_);
    return error;
  }

  error = offline_ctrl_.Init(hw_intf_, hw_res_info);
  if (UNLIKELY(error != kErrorNone)) {
    comp_mgr_.Deinit();
    HWInterface::Destroy(hw_intf_);
    return error;
  }

  return kErrorNone;
}

DisplayError CoreImpl::Deinit() {
  SCOPE_LOCK(locker_);

  offline_ctrl_.Deinit();
  comp_mgr_.Deinit();
  HWInterface::Destroy(hw_intf_);

  return kErrorNone;
}

DisplayError CoreImpl::CreateDisplay(DisplayType type, DisplayEventHandler *event_handler,
                                     DisplayInterface **intf) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!event_handler || !intf)) {
    return kErrorParameters;
  }

  DisplayBase *display_base = NULL;
  switch (type) {
  case kPrimary:
    display_base = new DisplayPrimary(event_handler, hw_intf_, &comp_mgr_, &offline_ctrl_);
    break;

  case kHDMI:
    display_base = new DisplayHDMI(event_handler, hw_intf_, &comp_mgr_, &offline_ctrl_);
    break;

  case kVirtual:
    display_base = new DisplayVirtual(event_handler, hw_intf_, &comp_mgr_, &offline_ctrl_);
    break;

  default:
    DLOGE("Spurious display type %d", type);
    return kErrorParameters;
  }

  if (UNLIKELY(!display_base)) {
    return kErrorMemory;
  }

  DisplayError error = display_base->Init();
  if (UNLIKELY(error != kErrorNone)) {
    delete display_base;
    display_base = NULL;
    return error;
  }

  *intf = display_base;
  return kErrorNone;
}

DisplayError CoreImpl::DestroyDisplay(DisplayInterface *intf) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!intf)) {
    return kErrorParameters;
  }

  DisplayBase *display_base = static_cast<DisplayBase *>(intf);
  display_base->Deinit();
  delete display_base;
  display_base = NULL;

  return kErrorNone;
}

}  // namespace sde

