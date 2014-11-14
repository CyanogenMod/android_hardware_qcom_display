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

// SDE_LOG_TAG definition must precede debug.h include.
#define SDE_LOG_TAG kTagCore
#define SDE_MODULE_NAME "CoreImpl"
#include <utils/debug.h>

#include <utils/locker.h>
#include <utils/constants.h>

#include "core_impl.h"
#include "device_primary.h"
#include "device_hdmi.h"
#include "device_virtual.h"

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

  HWResourceInfo hw_res_info;
  error = hw_intf_->GetHWCapabilities(&hw_res_info);
  if (UNLIKELY(error != kErrorNone)) {
    HWInterface::Destroy(hw_intf_);
    return error;
  }

  error = comp_mgr_.Init(hw_res_info);
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

DisplayError CoreImpl::CreateDevice(DeviceType type, DeviceEventHandler *event_handler,
                                    DeviceInterface **intf) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!event_handler || !intf)) {
    return kErrorParameters;
  }

  DeviceBase *device_base = NULL;
  switch (type) {
  case kPrimary:
    device_base = new DevicePrimary(event_handler, hw_intf_, &comp_mgr_);
    break;

  case kHWHDMI:
    device_base = new DeviceHDMI(event_handler, hw_intf_, &comp_mgr_);
    break;

  case kVirtual:
    device_base = new DeviceVirtual(event_handler, hw_intf_, &comp_mgr_);
    break;

  default:
    DLOGE("Spurious device type %d", type);
    return kErrorParameters;
  }

  if (UNLIKELY(!device_base)) {
    return kErrorMemory;
  }

  DisplayError error = device_base->Init();
  if (UNLIKELY(error != kErrorNone)) {
    delete device_base;
    return error;
  }

  *intf = device_base;
  return kErrorNone;
}

DisplayError CoreImpl::DestroyDevice(DeviceInterface *intf) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!intf)) {
    return kErrorParameters;
  }

  DeviceBase *device_base = static_cast<DeviceBase *>(intf);
  device_base->Deinit();
  delete device_base;

  return kErrorNone;
}

}  // namespace sde

