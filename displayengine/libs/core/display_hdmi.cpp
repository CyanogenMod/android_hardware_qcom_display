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

#include <utils/constants.h>
#include <utils/debug.h>

#include "display_hdmi.h"

#define __CLASS__ "DisplayHDMI"

namespace sde {

DisplayHDMI::DisplayHDMI(DisplayEventHandler *event_handler, HWInterface *hw_intf,
                         CompManager *comp_manager, OfflineCtrl *offline_ctrl)
  : DisplayBase(kHDMI, event_handler, kDeviceHDMI, hw_intf, comp_manager, offline_ctrl) { }

int DisplayHDMI::GetBestConfig() {
  uint32_t best_config_mode = 0;
  HWDisplayAttributes *best = &display_attributes_[0];
  if (num_modes_ == 1) {
    return best_config_mode;
  }

  // From the available configs, select the best
  // Ex: 1920x1080@60Hz is better than 1920x1080@30 and 1920x1080@30 is better than 1280x720@60
  for (uint32_t index = 1; index < num_modes_; index++) {
    HWDisplayAttributes *current = &display_attributes_[index];
    // compare the two modes: in the order of Resolution followed by refreshrate
    if (current->y_pixels > best->y_pixels) {
      best_config_mode = index;
    } else if (current->y_pixels == best->y_pixels) {
      if (current->x_pixels > best->x_pixels) {
        best_config_mode = index;
      } else if (current->x_pixels == best->x_pixels) {
        if (current->vsync_period_ns < best->vsync_period_ns) {
          best_config_mode = index;
        }
      }
    }
    if (best_config_mode == index) {
      best = &display_attributes_[index];
    }
  }

  // Used for changing HDMI Resolution - override the best with user set config
  uint32_t user_config = Debug::GetHDMIResolution();
  if (user_config) {
    uint32_t config_index = -1;
    // For the config, get the corresponding index
    DisplayError error = hw_intf_->GetConfigIndex(hw_device_, user_config, &config_index);
    if (error == kErrorNone)
      return config_index;
  }

  return best_config_mode;
}

}  // namespace sde

