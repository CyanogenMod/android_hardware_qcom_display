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

#ifndef __HW_FRAMEBUFFER_H__
#define __HW_FRAMEBUFFER_H__

#include <linux/msm_mdp.h>
#include "hw_interface.h"

namespace sde {

struct HWContext {
  HWBlockType type;
  int device_fd;
};

class HWFrameBuffer : public HWInterface {
 public:
  HWFrameBuffer();
  DisplayError Init();
  DisplayError Deinit();
  virtual DisplayError GetHWCapabilities(HWResourceInfo *hw_res_info);
  virtual DisplayError Open(HWBlockType type, Handle *device);
  virtual DisplayError Close(Handle device);
  virtual DisplayError GetNumDeviceAttributes(Handle device, uint32_t *count);
  virtual DisplayError GetDeviceAttributes(Handle device, HWDeviceAttributes *device_attributes,
                                       uint32_t mode);
  virtual DisplayError PowerOn(Handle device);
  virtual DisplayError PowerOff(Handle device);
  virtual DisplayError Doze(Handle device);
  virtual DisplayError Standby(Handle device);
  virtual DisplayError Validate(Handle device, HWLayers *hw_layers);
  virtual DisplayError Commit(Handle device, HWLayers *hw_layers);

 private:
  inline void SetFormat(uint32_t *target, const LayerBufferFormat &source);
  inline void SetBlending(uint32_t *target, const LayerBlending &source);
  inline void SetRect(mdp_rect *target, const LayerRect &source);

  // For dynamically linking virtual driver
  int (*ioctl_)(int, int, ...);
  int (*open_)(const char *, int, ...);
  int (*close_)(int);
};

}  // namespace sde

#endif  // __HW_FRAMEBUFFER_H__

