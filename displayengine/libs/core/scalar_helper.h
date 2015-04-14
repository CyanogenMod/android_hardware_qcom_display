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

#include <hw_interface.h>
#ifdef USES_SCALAR
#include <scalar.h>
#endif

namespace sde {

class Scalar {
 public:
  static DisplayError CreateScalar(Scalar **scalar);
  static void Destroy(Scalar *scalar);
  virtual ~Scalar() { }
  virtual DisplayError ConfigureScale(HWLayers *hw_layers) { return kErrorNone; }

 protected:
  virtual DisplayError Init() { return kErrorNone; }
  virtual void Deinit() { }
};

#ifdef USES_SCALAR
class ScalarHelper : public Scalar {
 public:
  ScalarHelper();
  virtual DisplayError ConfigureScale(HWLayers *hw_layers);

 protected:
  virtual DisplayError Init();
  virtual void Deinit();

 private:
  const char *scalar_library_name_;
  const char *configure_scale_api_;
  void* lib_scalar_;
  int (*configure_scale_)(scalar::LayerInfo *layer);
  void SetPipeInfo(const HWPipeInfo &hw_pipe, scalar::PipeInfo *pipe);
  void UpdateSrcRoi(const scalar::PipeInfo &pipe, HWPipeInfo *hw_pipe);
  void SetScaleData(const scalar::PipeInfo &pipe, ScaleData *scale_data);
  uint32_t GetScalarFormat(LayerBufferFormat source);
};
#endif

}  // namespace sde

#endif  // __SCALAR_HELPER_H__
