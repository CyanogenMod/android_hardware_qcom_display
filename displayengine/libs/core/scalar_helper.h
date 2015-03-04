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

#ifndef __SCALAR_HELPER_H__
#define __SCALAR_HELPER_H__

#ifdef USES_SCALAR

#include <sys/types.h>
#include <linux/msm_mdp_ext.h>
#include <hw_interface.h>
#include <scalar.h>

#define SCALAR_LIBRARY_NAME "libscalar.so"

namespace sde {

class ScalarHelper {

 public:
  void Init();
  void Deinit();
  bool ConfigureScale(HWLayers *hw_layers);
  void UpdateSrcWidth(uint32_t index, bool left, uint32_t* src_width);
  void SetScaleData(uint32_t index, bool left, mdp_scale_data* mdp_scale);
  static ScalarHelper* GetInstance();

 private:
  explicit ScalarHelper() { }
  struct ScaleData {
    scalar::Scale left_scale;
    scalar::Scale right_scale;
  };
  struct ScaleData scale_data_[kMaxSDELayers];
  void* lib_scalar_handle_;
  int (*ScalarConfigureScale)(struct scalar::LayerInfo* layer);
  scalar::Scale* GetScaleRef(uint32_t index, bool left) {
    return (left ? &scale_data_[index].left_scale : &scale_data_[index].right_scale);
  }
  static ScalarHelper* scalar_helper_;  // Singleton Instance
};

}  // namespace sde

#endif
#endif  // __SCALAR_HELPER_H__
