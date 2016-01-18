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

#ifndef __HWC_DISPLAY_PRIMARY_H__
#define __HWC_DISPLAY_PRIMARY_H__

#include "cpuhint.h"
#include "hwc_display.h"

namespace sdm {

class HWCDisplayPrimary : public HWCDisplay {
 public:
  enum {
    SET_METADATA_DYN_REFRESH_RATE,
    SET_BINDER_DYN_REFRESH_RATE,
    SET_DISPLAY_MODE,
    SET_QDCM_SOLID_FILL_INFO,
    UNSET_QDCM_SOLID_FILL_INFO,
  };

  static int Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                    HWCDisplay **hwc_display);
  static void Destroy(HWCDisplay *hwc_display);
  virtual int Init();
  virtual int Prepare(hwc_display_contents_1_t *content_list);
  virtual int Commit(hwc_display_contents_1_t *content_list);
  virtual int Perform(uint32_t operation, ...);
  virtual void SetSecureDisplay(bool secure_display_active);
  virtual DisplayError Refresh();
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);

 private:
  HWCDisplayPrimary(CoreInterface *core_intf, hwc_procs_t const **hwc_procs);
  void SetMetaDataRefreshRateFlag(bool enable);
  virtual DisplayError SetDisplayMode(uint32_t mode);
  void ProcessBootAnimCompleted(hwc_display_contents_1_t *content_list);
  void SetQDCMSolidFillInfo(bool enable, uint32_t color);
  void ToggleCPUHint(bool set);
  void ForceRefreshRate(uint32_t refresh_rate);
  uint32_t GetOptimalRefreshRate(bool one_updating_layer);

  CPUHint *cpu_hint_;
  bool handle_idle_timeout_ = false;
};

}  // namespace sdm

#endif  // __HWC_DISPLAY_PRIMARY_H__

