/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

#ifndef __HW_ROTATOR_H__
#define __HW_ROTATOR_H__

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/msm_mdp_ext.h>
#include <video/msm_hdmi_modes.h>
#include <linux/mdss_rotator.h>
#include <poll.h>
#include <pthread.h>

#include "hw_device.h"
#include "hw_rotator_interface.h"

namespace sde {

class HWRotator : public HWDevice, public HWRotatorInterface {
 public:
  explicit HWRotator(BufferSyncHandler *buffer_sync_handler);
  virtual DisplayError Open();
  virtual DisplayError Close();
  virtual DisplayError OpenSession(HWRotatorSession *hw_session_info);
  virtual DisplayError CloseSession(HWRotatorSession *hw_session_info);
  virtual DisplayError Validate(HWLayers *hw_layers);
  virtual DisplayError Commit(HWLayers *hw_layers);

 private:
  void ResetParams();
  void SetCtrlParams(HWLayers *hw_layers);
  void SetBufferParams(HWLayers *hw_layers);

  struct mdp_rotation_request mdp_rot_request_;
  struct mdp_rotation_item mdp_rot_layers_[kMaxSDELayers * 2];  // split panel (left + right)
};

}  // namespace sde

#endif  // __HW_ROTATOR_H__

