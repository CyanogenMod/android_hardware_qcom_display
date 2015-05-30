/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
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
*/

#include <utils/debug.h>
#include "hw_virtual.h"

#define __CLASS__ "HWVirtual"

namespace sdm {

DisplayError HWVirtualInterface::Create(HWVirtualInterface **intf, HWInfoInterface *hw_info_intf,
                                        BufferSyncHandler *buffer_sync_handler) {
  DisplayError error = kErrorNone;
  HWVirtual *hw_virtual = NULL;

  hw_virtual = new HWVirtual(buffer_sync_handler, hw_info_intf);
  error = hw_virtual->Init();
  if (error != kErrorNone) {
    delete hw_virtual;
  } else {
    *intf = hw_virtual;
  }

  return error;
}

DisplayError HWVirtualInterface::Destroy(HWVirtualInterface *intf) {
  HWVirtual *hw_virtual = static_cast<HWVirtual *>(intf);
  hw_virtual->Deinit();
  delete hw_virtual;

  return kErrorNone;
}

HWVirtual::HWVirtual(BufferSyncHandler *buffer_sync_handler, HWInfoInterface *hw_info_intf)
  : HWDevice(buffer_sync_handler) {
  HWDevice::device_type_ = kDeviceVirtual;
  HWDevice::device_name_ = "Virtual Display Device";
  HWDevice::hw_info_intf_ = hw_info_intf;
}

DisplayError HWVirtual::Init() {
  return HWDevice::Init();
}

DisplayError HWVirtual::Deinit() {
  return kErrorNone;
}

DisplayError HWVirtual::Open(HWEventHandler *eventhandler) {
  return HWDevice::Open(eventhandler);
}

DisplayError HWVirtual::Close() {
  return HWDevice::Close();
}

DisplayError HWVirtual::GetNumDisplayAttributes(uint32_t *count) {
  return HWDevice::GetNumDisplayAttributes(count);
}

DisplayError HWVirtual::GetDisplayAttributes(HWDisplayAttributes *display_attributes,
                                             uint32_t index) {
  return HWDevice::GetDisplayAttributes(display_attributes, index);
}

DisplayError HWVirtual::SetDisplayAttributes(uint32_t index) {
  return HWDevice::SetDisplayAttributes(index);
}

DisplayError HWVirtual::GetConfigIndex(uint32_t mode, uint32_t *index) {
  return HWDevice::GetConfigIndex(mode, index);
}

DisplayError HWVirtual::PowerOn() {
  return HWDevice::PowerOn();
}

DisplayError HWVirtual::PowerOff() {
  return HWDevice::PowerOff();
}

DisplayError HWVirtual::Doze() {
  return HWDevice::Doze();
}

DisplayError HWVirtual::DozeSuspend() {
  return HWDevice::DozeSuspend();
}

DisplayError HWVirtual::Standby() {
  return HWDevice::Standby();
}

DisplayError HWVirtual::Validate(HWLayers *hw_layers) {
  HWDevice::ResetDisplayParams();
  return HWDevice::Validate(hw_layers);
}

DisplayError HWVirtual::Commit(HWLayers *hw_layers) {
  return HWDevice::Commit(hw_layers);
}

DisplayError HWVirtual::Flush() {
  return HWDevice::Flush();
}

DisplayError HWVirtual::GetHWPanelInfo(HWPanelInfo *panel_info) {
  return HWDevice::GetHWPanelInfo(panel_info);
}

DisplayError HWVirtual::GetPPFeaturesVersion(PPFeatureVersion *vers) {
  return kErrorNotSupported;
}

DisplayError HWVirtual::SetPPFeatures(PPFeaturesConfig &feature_list) {
  return kErrorNotSupported;
}

}  // namespace sdm

