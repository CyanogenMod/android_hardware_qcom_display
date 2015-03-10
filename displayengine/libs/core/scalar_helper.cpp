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

#ifdef USES_SCALAR

#include <dlfcn.h>
#include <utils/debug.h>
#include "scalar_helper.h"

#define __CLASS__ "ScalarHelper"

namespace sde {

ScalarHelper* ScalarHelper::scalar_helper_ = NULL;

ScalarHelper* ScalarHelper::GetInstance() {
  if (scalar_helper_ == NULL) {
    scalar_helper_ = new ScalarHelper();
  }
  return scalar_helper_;
}

// Scalar helper functions
static void SetPipeInfo(HWPipeInfo* hw_pipe, scalar::PipeInfo* pipe) {
  pipe->id = hw_pipe->pipe_id;
  pipe->horz_deci = hw_pipe->horizontal_decimation;
  pipe->vert_deci = hw_pipe->vertical_decimation;

  pipe->src_rect.x = UINT32(hw_pipe->src_roi.left);
  pipe->src_rect.y = UINT32(hw_pipe->src_roi.top);
  pipe->src_rect.w = UINT32(hw_pipe->src_roi.right) - pipe->src_rect.x;
  pipe->src_rect.h = UINT32(hw_pipe->src_roi.bottom) - pipe->src_rect.y;

  pipe->dst_rect.x = UINT32(hw_pipe->dst_roi.left);
  pipe->dst_rect.y = UINT32(hw_pipe->dst_roi.top);
  pipe->dst_rect.w = UINT32(hw_pipe->dst_roi.right) - pipe->dst_rect.x;
  pipe->dst_rect.h = UINT32(hw_pipe->dst_roi.bottom) - pipe->dst_rect.y;
}

static void UpdateSrcRoi(scalar::PipeInfo* pipe, HWPipeInfo* hw_pipe) {
  hw_pipe->src_roi.left   = FLOAT(pipe->src_rect.x);
  hw_pipe->src_roi.top    = FLOAT(pipe->src_rect.y);
  hw_pipe->src_roi.right  = FLOAT(pipe->src_rect.x + pipe->src_rect.w);
  hw_pipe->src_roi.bottom = FLOAT(pipe->src_rect.y + pipe->src_rect.h);
}

static uint32_t GetScalarFormat(LayerBufferFormat source) {
  uint32_t format = scalar::UNKNOWN_FORMAT;

  switch (source) {
  case kFormatARGB8888:                 format = scalar::ARGB_8888;         break;
  case kFormatRGBA8888:                 format = scalar::RGBA_8888;         break;
  case kFormatBGRA8888:                 format = scalar::BGRA_8888;         break;
  case kFormatXRGB8888:                 format = scalar::XRGB_8888;         break;
  case kFormatRGBX8888:                 format = scalar::RGBX_8888;         break;
  case kFormatBGRX8888:                 format = scalar::BGRX_8888;         break;
  case kFormatRGB888:                   format = scalar::RGB_888;           break;
  case kFormatRGB565:                   format = scalar::RGB_565;           break;
  case kFormatYCbCr420Planar:           format = scalar::Y_CB_CR_H2V2;      break;
  case kFormatYCrCb420Planar:           format = scalar::Y_CR_CB_H2V2;      break;
  case kFormatYCbCr420SemiPlanar:       format = scalar::Y_CBCR_H2V2;       break;
  case kFormatYCrCb420SemiPlanar:       format = scalar::Y_CRCB_H2V2;       break;
  case kFormatYCbCr422Packed:           format = scalar::YCBYCR_H2V1;       break;
  case kFormatYCbCr420SemiPlanarVenus:  format = scalar::Y_CBCR_H2V2_VENUS; break;
  case kFormatRGBA8888Ubwc:             format = scalar::RGBA_8888_UBWC;    break;
  case kFormatRGB565Ubwc:               format = scalar::RGB_565_UBWC;      break;
  case kFormatYCbCr420SPVenusUbwc:      format = scalar::Y_CBCR_H2V2_UBWC;  break;
  default:
    DLOGE("Unsupported source format: %x", source);
    break;
  }
  return format;
}

void ScalarHelper::Init() {
  lib_scalar_handle_   = NULL;
  ScalarConfigureScale = NULL;

  lib_scalar_handle_ = dlopen(SCALAR_LIBRARY_NAME, RTLD_NOW);
  if (lib_scalar_handle_) {
    void **scalar_func = reinterpret_cast<void **>(&ScalarConfigureScale);
    *scalar_func = ::dlsym(lib_scalar_handle_, "configureScale");
  } else {
    DLOGW("Unable to load %s !", SCALAR_LIBRARY_NAME);
  }
}

void ScalarHelper::Deinit() {
  if (lib_scalar_handle_) {
    dlclose(lib_scalar_handle_);
    lib_scalar_handle_ = NULL;
  }
}

bool ScalarHelper::ConfigureScale(HWLayers *hw_layers) {

  if (!lib_scalar_handle_ || !ScalarConfigureScale) {
    // No scalar library
    return true;
  }

  // Reset scale data
  memset(&scale_data_, 0, sizeof(scale_data_));
  HWLayersInfo &hw_layer_info = hw_layers->info;

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer &layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];
    uint32_t width = layer.input_buffer->width;
    LayerBufferFormat format = layer.input_buffer->format;
    HWPipeInfo* left_pipe = &hw_layers->config[i].left_pipe;
    HWPipeInfo* right_pipe = &hw_layers->config[i].right_pipe;

    // Prepare data structure for lib scalar
    uint32_t flags = 0;
    struct scalar::LayerInfo layer_info;

    if (layer.transform.rotation == 90.0f) {
      // Flips will be taken care by rotator, if layer requires 90 rotation
      flags |= scalar::SCALAR_SOURCE_ROTATED_90;
    } else {
      flags |= layer.transform.flip_vertical ? scalar::SCALAR_FLIP_UD : 0;
      flags |= layer.transform.flip_horizontal ? scalar::SCALAR_FLIP_LR : 0;
    }

    for (uint32_t count = 0; count < 2; count++) {
      HWPipeInfo* hw_pipe = (count == 0) ? left_pipe : right_pipe;
      HWRotateInfo* rotate_info = &hw_layers->config[i].rotates[count];
      scalar::PipeInfo* pipe = (count == 0) ? &layer_info.left_pipe : &layer_info.right_pipe;

      if (rotate_info->valid) {
        width = rotate_info->hw_buffer_info.buffer_config.width;
        format = rotate_info->hw_buffer_info.buffer_config.format;
      }

      pipe->flags = flags;
      pipe->scale_data = GetScaleRef(i, !count);
      pipe->scale_data->src_width = width;
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
    if (ScalarConfigureScale(&layer_info) < 0) {
      DLOGE("Scalar library failed to configure scale data!");
      return false;
    }

    // Update Src Roi in HWPipeInfo
    if (layer_info.left_pipe.scale_data->enable_pxl_ext)
      UpdateSrcRoi(&layer_info.left_pipe, left_pipe);
    if (layer_info.right_pipe.scale_data->enable_pxl_ext)
      UpdateSrcRoi(&layer_info.right_pipe, right_pipe);
  }
  return true;
}

void ScalarHelper::UpdateSrcWidth(uint32_t index, bool left, uint32_t* width) {
  *width = GetScaleRef(index, left)->src_width;
}

void ScalarHelper::SetScaleData(uint32_t index, bool left, mdp_scale_data* mdp_scale) {

  if (!lib_scalar_handle_ || !ScalarConfigureScale)
    return;

  scalar::Scale* scale = GetScaleRef(index, left);
  mdp_scale->enable_pxl_ext = scale->enable_pxl_ext;

  for (int i = 0; i < MAX_PLANES; i++) {
    mdp_scale->init_phase_x[i] = scale->init_phase_x[i];
    mdp_scale->phase_step_x[i] = scale->phase_step_x[i];
    mdp_scale->init_phase_y[i] = scale->init_phase_y[i];
    mdp_scale->phase_step_y[i] = scale->phase_step_y[i];

    mdp_scale->num_ext_pxls_left[i] = scale->left.extension[i];
    mdp_scale->num_ext_pxls_top[i] = scale->top.extension[i];
    mdp_scale->num_ext_pxls_right[i] = scale->right.extension[i];
    mdp_scale->num_ext_pxls_btm[i] = scale->bottom.extension[i];

    mdp_scale->left_ftch[i] = scale->left.overfetch[i];
    mdp_scale->top_ftch[i] = scale->top.overfetch[i];
    mdp_scale->right_ftch[i] = scale->right.overfetch[i];
    mdp_scale->btm_ftch[i] = scale->bottom.overfetch[i];

    mdp_scale->left_rpt[i] = scale->left.repeat[i];
    mdp_scale->top_rpt[i] = scale->top.repeat[i];
    mdp_scale->right_rpt[i] = scale->right.repeat[i];
    mdp_scale->btm_rpt[i] = scale->bottom.repeat[i];

    mdp_scale->roi_w[i] = scale->roi_width[i];
  }
}

} // namespace sde

#endif
