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

#include <dlfcn.h>
#include <utils/debug.h>
#include "scalar_helper.h"

#define __CLASS__ "ScalarHelper"

namespace sde {

#ifdef USES_SCALAR
ScalarHelper::ScalarHelper()
  : scalar_library_name_("libscalar.so"), configure_scale_api_("configureScale"),
    lib_scalar_(NULL), configure_scale_(NULL) {
}

DisplayError ScalarHelper::Init() {
  lib_scalar_ = dlopen(scalar_library_name_, RTLD_NOW);
  if (lib_scalar_) {
    void **scalar_func = reinterpret_cast<void **>(&configure_scale_);
    *scalar_func = ::dlsym(lib_scalar_, configure_scale_api_);
    if (!configure_scale_) {
      DLOGE("Unable to find symbol for %s API!", configure_scale_api_);
      dlclose(lib_scalar_);
      return kErrorUndefined;
    }
  } else {
    DLOGW("Unable to load %s !", scalar_library_name_);
    return kErrorNotSupported;
  }

  return kErrorNone;
}

void ScalarHelper::Deinit() {
  if (lib_scalar_) {
    dlclose(lib_scalar_);
  }
}

// Helper functions
void ScalarHelper::SetPipeInfo(const HWPipeInfo &hw_pipe, scalar::PipeInfo *pipe) {
  pipe->id = hw_pipe.pipe_id;
  pipe->horz_deci = hw_pipe.horizontal_decimation;
  pipe->vert_deci = hw_pipe.vertical_decimation;

  pipe->src_rect.x = UINT32(hw_pipe.src_roi.left);
  pipe->src_rect.y = UINT32(hw_pipe.src_roi.top);
  pipe->src_rect.w = UINT32(hw_pipe.src_roi.right) - pipe->src_rect.x;
  pipe->src_rect.h = UINT32(hw_pipe.src_roi.bottom) - pipe->src_rect.y;

  pipe->dst_rect.x = UINT32(hw_pipe.dst_roi.left);
  pipe->dst_rect.y = UINT32(hw_pipe.dst_roi.top);
  pipe->dst_rect.w = UINT32(hw_pipe.dst_roi.right) - pipe->dst_rect.x;
  pipe->dst_rect.h = UINT32(hw_pipe.dst_roi.bottom) - pipe->dst_rect.y;
}

void ScalarHelper::UpdateSrcRoi(const scalar::PipeInfo &pipe, HWPipeInfo *hw_pipe) {
  hw_pipe->src_roi.left   = FLOAT(pipe.src_rect.x);
  hw_pipe->src_roi.top    = FLOAT(pipe.src_rect.y);
  hw_pipe->src_roi.right  = FLOAT(pipe.src_rect.x + pipe.src_rect.w);
  hw_pipe->src_roi.bottom = FLOAT(pipe.src_rect.y + pipe.src_rect.h);
}

void ScalarHelper::SetScaleData(const scalar::PipeInfo &pipe, ScaleData *scale_data) {
  scalar::Scale *scale = pipe.scale_data;
  scale_data->src_width = pipe.src_width;
  scale_data->src_height = pipe.src_height;
  scale_data->enable_pixel_ext = scale->enable_pxl_ext;

  for (int i = 0; i < 4; i++) {
    HWPlane &plane = scale_data->plane[i];
    plane.init_phase_x = scale->init_phase_x[i];
    plane.phase_step_x = scale->phase_step_x[i];
    plane.init_phase_y = scale->init_phase_y[i];
    plane.phase_step_y = scale->phase_step_y[i];

    plane.left.extension = scale->left.extension[i];
    plane.left.overfetch = scale->left.overfetch[i];
    plane.left.repeat = scale->left.repeat[i];

    plane.top.extension = scale->top.extension[i];
    plane.top.overfetch = scale->top.overfetch[i];
    plane.top.repeat = scale->top.repeat[i];

    plane.right.extension = scale->right.extension[i];
    plane.right.overfetch = scale->right.overfetch[i];
    plane.right.repeat = scale->right.repeat[i];

    plane.bottom.extension = scale->bottom.extension[i];
    plane.bottom.overfetch = scale->bottom.overfetch[i];
    plane.bottom.repeat = scale->bottom.repeat[i];

    plane.roi_width = scale->roi_width[i];
  }
}

uint32_t ScalarHelper::GetScalarFormat(LayerBufferFormat source) {
  uint32_t format = scalar::UNKNOWN_FORMAT;

  switch (source) {
  case kFormatARGB8888:                 format = scalar::ARGB_8888;         break;
  case kFormatRGBA8888:                 format = scalar::RGBA_8888;         break;
  case kFormatBGRA8888:                 format = scalar::BGRA_8888;         break;
  case kFormatXRGB8888:                 format = scalar::XRGB_8888;         break;
  case kFormatRGBX8888:                 format = scalar::RGBX_8888;         break;
  case kFormatBGRX8888:                 format = scalar::BGRX_8888;         break;
  case kFormatRGBA5551:                 format = scalar::RGBA_5551;         break;
  case kFormatRGBA4444:                 format = scalar::RGBA_4444;         break;
  case kFormatRGB888:                   format = scalar::RGB_888;           break;
  case kFormatBGR888:                   format = scalar::BGR_888;           break;
  case kFormatRGB565:                   format = scalar::RGB_565;           break;
  case kFormatYCbCr420Planar:           format = scalar::Y_CB_CR_H2V2;      break;
  case kFormatYCrCb420Planar:           format = scalar::Y_CR_CB_H2V2;      break;
  case kFormatYCbCr420SemiPlanar:       format = scalar::Y_CBCR_H2V2;       break;
  case kFormatYCrCb420SemiPlanar:       format = scalar::Y_CRCB_H2V2;       break;
  case kFormatYCbCr422H1V2SemiPlanar:   format = scalar::Y_CBCR_H1V2;       break;
  case kFormatYCrCb422H1V2SemiPlanar:   format = scalar::Y_CRCB_H1V2;       break;
  case kFormatYCbCr422H2V1SemiPlanar:   format = scalar::Y_CBCR_H2V1;       break;
  case kFormatYCrCb422H2V1SemiPlanar:   format = scalar::Y_CRCB_H2V1;       break;
  case kFormatYCbCr422H2V1Packed:       format = scalar::YCBYCR_H2V1;       break;
  case kFormatYCbCr420SemiPlanarVenus:  format = scalar::Y_CBCR_H2V2_VENUS; break;
  case kFormatRGBA8888Ubwc:             format = scalar::RGBA_8888_UBWC;    break;
  case kFormatRGBX8888Ubwc:             format = scalar::RGBX_8888_UBWC;    break;
  case kFormatRGB565Ubwc:               format = scalar::RGB_565_UBWC;      break;
  case kFormatYCbCr420SPVenusUbwc:      format = scalar::Y_CBCR_H2V2_UBWC;  break;
  default:
    DLOGE("Unsupported source format: %x", source);
    break;
  }
  return format;
}

DisplayError ScalarHelper::ConfigureScale(HWLayers *hw_layers) {
  HWLayersInfo &hw_layer_info = hw_layers->info;

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer &layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];
    uint32_t width = layer.input_buffer->width;
    uint32_t height = layer.input_buffer->height;
    LayerBufferFormat format = layer.input_buffer->format;
    HWPipeInfo &left_pipe = hw_layers->config[i].left_pipe;
    HWPipeInfo &right_pipe = hw_layers->config[i].right_pipe;
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;

    // Reset scale data
    memset(&left_pipe.scale_data, 0, sizeof(ScaleData));
    memset(&right_pipe.scale_data, 0, sizeof(ScaleData));

    // Prepare data structure for lib scalar
    uint32_t flags = 0;
    struct scalar::LayerInfo layer_info;
    struct scalar::Scale left_scale, right_scale;

    if (layer.transform.rotation == 90.0f) {
      // Flips will be taken care by rotator, if layer requires 90 rotation
      flags |= scalar::SCALAR_SOURCE_ROTATED_90;
    } else {
      flags |= layer.transform.flip_vertical ? scalar::SCALAR_FLIP_UD : 0;
      flags |= layer.transform.flip_horizontal ? scalar::SCALAR_FLIP_LR : 0;
    }

    for (uint32_t count = 0; count < 2; count++) {
      const HWPipeInfo &hw_pipe = (count == 0) ? left_pipe : right_pipe;
      HWRotateInfo* hw_rotate_info = &hw_rotator_session->hw_rotate_info[count];
      scalar::PipeInfo* pipe = (count == 0) ? &layer_info.left_pipe : &layer_info.right_pipe;

      if (hw_rotate_info->valid) {
        width = UINT32(hw_rotate_info->dst_roi.right - hw_rotate_info->dst_roi.left);
        height = UINT32(hw_rotate_info->dst_roi.bottom - hw_rotate_info->dst_roi.top);
        format = hw_rotator_session->output_buffer.format;
      }

      pipe->flags = flags;
      pipe->scale_data = (count == 0) ? &left_scale : &right_scale;
      pipe->src_width = width;
      pipe->src_height = height;
      SetPipeInfo(hw_pipe, pipe);
    }
    layer_info.src_format = GetScalarFormat(format);

    DLOGV_IF(kTagScalar, "Scalar Input[%d] flags=%x format=%x", i, flags, layer_info.src_format);
    DLOGV_IF(kTagScalar, "Left: id=%d hD=%d vD=%d srcRect=[%d %d %d %d] dstRect=[%d %d %d %d]",
      layer_info.left_pipe.id, layer_info.left_pipe.horz_deci, layer_info.left_pipe.vert_deci,
      layer_info.left_pipe.src_rect.x, layer_info.left_pipe.src_rect.y,
      layer_info.left_pipe.src_rect.w, layer_info.left_pipe.src_rect.h,
      layer_info.left_pipe.dst_rect.x, layer_info.left_pipe.dst_rect.y,
      layer_info.left_pipe.dst_rect.w, layer_info.left_pipe.dst_rect.h);
    DLOGV_IF(kTagScalar, "Right: id=%d hD=%d vD=%d srcRect=[%d %d %d %d] dstRect=[%d %d %d %d]",
      layer_info.right_pipe.id, layer_info.right_pipe.horz_deci, layer_info.right_pipe.vert_deci,
      layer_info.right_pipe.src_rect.x, layer_info.right_pipe.src_rect.y,
      layer_info.right_pipe.src_rect.w, layer_info.right_pipe.src_rect.h,
      layer_info.right_pipe.dst_rect.x, layer_info.right_pipe.dst_rect.y,
      layer_info.right_pipe.dst_rect.w, layer_info.right_pipe.dst_rect.h);

    // Configure scale data structure
    if (configure_scale_(&layer_info) < 0) {
      DLOGE("Scalar library failed to configure scale data!");
      return kErrorParameters;
    }

    // Set ScaleData and update SrcRoi in HWPipeInfo
    if (left_scale.enable_pxl_ext) {
      SetScaleData(layer_info.left_pipe, &left_pipe.scale_data);
      UpdateSrcRoi(layer_info.left_pipe, &left_pipe);
    }
    if (right_scale.enable_pxl_ext) {
      SetScaleData(layer_info.right_pipe, &right_pipe.scale_data);
      UpdateSrcRoi(layer_info.right_pipe, &right_pipe);
    }
  }

  return kErrorNone;
}
#endif

DisplayError Scalar::CreateScalar(Scalar **scalar) {
  Scalar *scalar_obj = NULL;

#ifdef USES_SCALAR
  scalar_obj = new ScalarHelper();
  if (scalar_obj) {
    if (scalar_obj->Init() == kErrorNone) {
      goto OnSuccess;
    } else {
      delete scalar_obj;
    }
  }
#endif

  scalar_obj = new Scalar();
  if (!scalar_obj) {
    return kErrorMemory;
  }

OnSuccess:
  *scalar = scalar_obj;
  return kErrorNone;
}

void Scalar::Destroy(Scalar *scalar) {
  scalar->Deinit();
}

}  // namespace sde
