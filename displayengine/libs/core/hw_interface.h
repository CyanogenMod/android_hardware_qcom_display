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

#ifndef __HW_INTERFACE_H__
#define __HW_INTERFACE_H__

#include <core/device_interface.h>
#include <private/strategy_interface.h>
#include <utils/constants.h>

namespace sde {

enum HWInterfaceType {
  kHWPrimary,
  kHWHDMI,
  kHWWriteback,
};

struct HWResourceInfo {
};

struct HWLayers {
};

class HWInterface {
 public:
  static DisplayError Create(HWInterface **intf);
  static DisplayError Destroy(HWInterface *intf);
  virtual DisplayError GetCapabilities(HWResourceInfo *hw_res_info) = 0;
  virtual DisplayError Open(HWInterfaceType type, Handle *device) = 0;
  virtual DisplayError Close(Handle device) = 0;
  virtual DisplayError GetConfig(Handle device, DeviceConfigVariableInfo *variable_info) = 0;
  virtual DisplayError PowerOn(Handle device) = 0;
  virtual DisplayError PowerOff(Handle device) = 0;
  virtual DisplayError Doze(Handle device) = 0;
  virtual DisplayError Standby(Handle device) = 0;
  virtual DisplayError Prepare(Handle device, HWLayers *hw_layers) = 0;
  virtual DisplayError Commit(Handle device, HWLayers *hw_layers) = 0;

 protected:
  virtual ~HWInterface() { }
};

}  // namespace sde

#endif  // __HW_INTERFACE_H__

