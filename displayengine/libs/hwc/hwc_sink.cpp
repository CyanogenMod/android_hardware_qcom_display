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

#include <errno.h>
#include <gralloc_priv.h>
#include <utils/constants.h>

// HWC_MODULE_NAME definition must precede hwc_logger.h include.
#define HWC_MODULE_NAME "HWCSink"
#include "hwc_logger.h"

#include "hwc_sink.h"

namespace sde {

HWCSink::HWCSink(CoreInterface *core_intf, hwc_procs_t const *hwc_procs, DeviceType type, int id)
  : core_intf_(core_intf), hwc_procs_(hwc_procs), type_(type), id_(id),
    device_intf_(NULL) {
}

int HWCSink::Init() {
  return 0;
}

int HWCSink::Deinit() {
  return 0;
}

int HWCSink::Blank(int blank) {
  return 0;
}

int HWCSink::GetDisplayConfigs(uint32_t *configs, size_t *num_configs) {
  return 0;
}

int HWCSink::GetDisplayAttributes(uint32_t config, const uint32_t *attributes, int32_t *values) {
  return 0;
}

DisplayError HWCSink::VSync(const DeviceEventVSync &vsync) {
  return kErrorNone;
}

DisplayError HWCSink::Refresh() {
  return kErrorNone;
}

}  // namespace sde

