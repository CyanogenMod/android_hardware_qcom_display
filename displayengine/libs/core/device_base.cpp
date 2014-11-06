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
#define SDE_MODULE_NAME "DeviceBase"
#include <utils/debug.h>

#include <utils/constants.h>

#include "device_base.h"

namespace sde {

DeviceBase::DeviceBase(DeviceType device_type, DeviceEventHandler *event_handler,
             HWBlockType hw_block_type, HWInterface *hw_intf, CompManager *comp_manager)
  : device_type_(device_type), event_handler_(event_handler), hw_block_type_(hw_block_type),
    hw_intf_(hw_intf), comp_manager_(comp_manager), state_(kStateOff), hw_device_(0),
    comp_mgr_device_(0), device_attributes_(NULL), num_modes_(0), active_mode_index_(0),
    pending_commit_(false) {
}

DisplayError DeviceBase::Init() {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  error = hw_intf_->Open(hw_block_type_, &hw_device_);
  if (UNLIKELY(error != kErrorNone)) {
    return error;
  }

  error = hw_intf_->GetNumDeviceAttributes(hw_device_, &num_modes_);
  if (UNLIKELY(error != kErrorNone)) {
    goto CleanupOnError;
  }

  device_attributes_ = new HWDeviceAttributes[num_modes_];
  if (!device_attributes_) {
    error = kErrorMemory;
    goto CleanupOnError;
  }

  for (uint32_t i = 0; i < num_modes_; i++) {
    error = hw_intf_->GetDeviceAttributes(hw_device_, &device_attributes_[i], i);
    if (UNLIKELY(error != kErrorNone)) {
      goto CleanupOnError;
    }
  }

  active_mode_index_ = 0;

  error = comp_manager_->RegisterDevice(device_type_, device_attributes_[active_mode_index_],
                                        &comp_mgr_device_);
  if (UNLIKELY(error != kErrorNone)) {
    goto CleanupOnError;
  }

  return kErrorNone;

CleanupOnError:
  comp_manager_->UnregisterDevice(comp_mgr_device_);

  if (device_attributes_) {
    delete[] device_attributes_;
  }

  hw_intf_->Close(hw_device_);

  return error;
}

DisplayError DeviceBase::Deinit() {
  SCOPE_LOCK(locker_);

  comp_manager_->UnregisterDevice(comp_mgr_device_);
  delete[] device_attributes_;
  hw_intf_->Close(hw_device_);

  return kErrorNone;
}

DisplayError DeviceBase::Prepare(LayerStack *layer_stack) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  if (UNLIKELY(!layer_stack)) {
    return kErrorParameters;
  }

  pending_commit_ = false;

  if (LIKELY(state_ == kStateOn)) {
    // Clean hw layers for reuse.
    hw_layers_.info.Reset();
    hw_layers_.info.stack = layer_stack;

    while (true) {
      error = comp_manager_->Prepare(comp_mgr_device_, &hw_layers_);
      if (UNLIKELY(error != kErrorNone)) {
        break;
      }

      error = hw_intf_->Validate(hw_device_, &hw_layers_);
      if (LIKELY(error == kErrorNone)) {
        // Strategy is successful now, wait for Commit().
        comp_manager_->PostPrepare(comp_mgr_device_, &hw_layers_);
        pending_commit_ = true;
        break;
      }
    }
  }

  return error;
}

DisplayError DeviceBase::Commit(LayerStack *layer_stack) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  if (UNLIKELY(!layer_stack)) {
    return kErrorParameters;
  }

  if (UNLIKELY(!pending_commit_)) {
    DLOGE("Commit: Corresponding Prepare() is not called.");
    return kErrorUndefined;
  }

  if (LIKELY(state_ == kStateOn)) {
    error = hw_intf_->Commit(hw_device_, &hw_layers_);
    if (LIKELY(error == kErrorNone)) {
      comp_manager_->PostCommit(comp_mgr_device_, &hw_layers_);
    } else {
      DLOGE("Unexpected error. Commit failed on driver.");
    }
  }

  pending_commit_ = false;

  return kErrorNone;
}

DisplayError DeviceBase::GetDeviceState(DeviceState *state) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!state)) {
    return kErrorParameters;
  }

  *state = state_;
  return kErrorNone;
}

DisplayError DeviceBase::GetNumVariableInfoConfigs(uint32_t *count) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!count)) {
    return kErrorParameters;
  }

  *count = num_modes_;

  return kErrorNone;
}

DisplayError DeviceBase::GetConfig(DeviceConfigFixedInfo *fixed_info) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!fixed_info)) {
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError DeviceBase::GetConfig(DeviceConfigVariableInfo *variable_info, uint32_t mode) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!variable_info || mode >= num_modes_)) {
    return kErrorParameters;
  }

  *variable_info = device_attributes_[mode];

  return kErrorNone;
}

DisplayError DeviceBase::GetVSyncState(bool *enabled) {
  SCOPE_LOCK(locker_);

  if (UNLIKELY(!enabled)) {
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError DeviceBase::SetDeviceState(DeviceState state) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  DLOGI("Set state: %d", state);

  if (UNLIKELY(state == state_)) {
    DLOGI("Same state transition is requested.");
    return kErrorNone;
  }

  switch (state) {
  case kStateOff:
    comp_manager_->Purge(comp_mgr_device_);
    error = hw_intf_->PowerOff(hw_device_);
    break;

  case kStateOn:
    error = hw_intf_->PowerOn(hw_device_);
    break;

  case kStateDoze:
    error = hw_intf_->Doze(hw_device_);
    break;

  case kStateStandby:
    error = hw_intf_->Standby(hw_device_);
    break;

  default:
    DLOGE("Spurious state %d transition requested.", state);
    break;
  }

  if (UNLIKELY(error == kErrorNone)) {
    state_ = state;
  }

  return error;
}

DisplayError DeviceBase::SetConfig(uint32_t mode) {
  SCOPE_LOCK(locker_);

  return kErrorNone;
}

DisplayError DeviceBase::SetVSyncState(bool enabled) {
  SCOPE_LOCK(locker_);

  return kErrorNone;
}

uint32_t DeviceBase::GetDump(uint8_t *buffer, uint32_t length) {
  SCOPE_LOCK(locker_);

  return 0;
}

}  // namespace sde

