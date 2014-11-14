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
#define DISPLAY_MODULE_NAME "CoreImpl"
#include <utils/logger.h>

#include <utils/locker.h>
#include <utils/constants.h>

#include "core_impl.h"
#include "device_primary.h"

namespace sde {

CoreImpl::CoreImpl(CoreEventHandler *event_handler)
  : event_handler_(event_handler), hw_intf_(NULL) {
}

DisplayError CoreImpl::Init() {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  error = HWInterface::Create(&hw_intf_);
  if (UNLIKELY(error != kErrorNone)) {
    return error;
  }

  error = comp_mgr_.Init();
  if (UNLIKELY(error != kErrorNone)) {
    HWInterface::Destroy(hw_intf_);
    return error;
  }

  return kErrorNone;
}

DisplayError CoreImpl::Deinit() {
  SCOPE_LOCK(locker_);

  comp_mgr_.Deinit();
  HWInterface::Destroy(hw_intf_);

  return kErrorNone;
}

DisplayError CoreImpl::CreateDevice(DeviceType type, DeviceEventHandler *event_handler,
                                    DeviceInterface **intf) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!event_handler || !intf)) {
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError CoreImpl::DestroyDevice(DeviceInterface *intf) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!intf)) {
    return kErrorParameters;
  }

  return kErrorNone;
}

}  // namespace sde

