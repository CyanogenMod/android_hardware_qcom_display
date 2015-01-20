/*
* Copyright (c) 2014 - 2015, The Linux Foundation. All rights reserved.
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

#include <stdlib.h>
#include <utils/debug.h>
#include <cutils/log.h>
#include <cutils/properties.h>

namespace sde {

Debug Debug::debug_;

Debug::Debug() : debug_handler_(&default_debug_handler_), virtual_driver_(false) {
  char property[PROPERTY_VALUE_MAX];
  if (property_get("displaycore.virtualdriver", property, NULL) > 0) {
    virtual_driver_ = (atoi(property) == 1);
  }
}

uint32_t Debug::GetSimulationFlag() {
  char property[PROPERTY_VALUE_MAX];
  if (property_get("debug.hwc.simulate", property, NULL) > 0) {
    return atoi(property);
  }

  return 0;
}

uint32_t Debug::GetHDMIResolution() {
  char property[PROPERTY_VALUE_MAX];
  if (property_get("hw.hdmi.resolution", property, NULL) > 0) {
    return atoi(property);
  }

  return 0;
}

uint32_t Debug::GetIdleTimeoutMs() {
  char property[PROPERTY_VALUE_MAX];
  if (property_get("debug.mdpcomp.idletime", property, NULL) > 0) {
    return atoi(property);
  }

  return 0;
}

}  // namespace sde

