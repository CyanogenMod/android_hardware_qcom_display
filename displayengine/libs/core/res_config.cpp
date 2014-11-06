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

// SDE_LOG_TAG definition must precede debug.h include.
#define SDE_LOG_TAG kTagCore
#define SDE_MODULE_NAME "ResConfig"
#include <utils/debug.h>

#include <utils/constants.h>
#include <math.h>

#include "res_manager.h"

namespace sde {

DisplayError ResManager::Config(ResManagerDevice *res_mgr_device, HWLayers *hw_layers) {
  HWBlockType hw_block_id = res_mgr_device->hw_block_id;
  HWDeviceAttributes &device_attributes = res_mgr_device->device_attributes;
  HWLayersInfo &layer_info = hw_layers->info;

  for (uint32_t i = 0; i < layer_info.count; i++) {
    Layer& layer = layer_info.stack->layers.layer[layer_info.index[i]];
    float w_scale, h_scale;
    if (!IsValidDimension(layer, &w_scale, &h_scale)) {
      DLOGV("Invalid dimension");
      return kErrorNotSupported;
    }

    LayerRect scissor;
    scissor.right = FLOAT(device_attributes.split_left);
    scissor.bottom = FLOAT(device_attributes.y_pixels);
    LayerRect crop = layer.src_rect;
    LayerRect dst = layer.dst_rect;
    LayerRect cropRight = crop;
    LayerRect dstRight = dst;
    CalculateCropRects(&crop, &dst, scissor, layer.transform);
    HWPipeInfo *pipe_info = &hw_layers->config[i].left_pipe;

    pipe_info->src_roi = crop;
    pipe_info->dst_roi = dst;

    float crop_width = cropRight.right - cropRight.left;
    pipe_info = &hw_layers->config[i].right_pipe;
    if ((dstRight.right - dstRight.left) > kMaxInterfaceWidth ||
         crop_width > kMaxInterfaceWidth ||
        ((hw_block_id == kHWPrimary) && hw_res_info_.is_src_split &&
         (crop_width > device_attributes.split_left))) {
      scissor.left = FLOAT(device_attributes.split_left);
      scissor.top = 0.0f;
      scissor.right = FLOAT(device_attributes.x_pixels);
      scissor.bottom = FLOAT(device_attributes.y_pixels);
      CalculateCropRects(&cropRight, &dstRight, scissor, layer.transform);
      pipe_info->src_roi = cropRight;
      pipe_info->dst_roi = dstRight;
      pipe_info->pipe_id = -1;
    } else {
      // need not right pipe
      pipe_info->pipe_id = 0;
    }
  }

  return kErrorNone;
}

bool ResManager::IsValidDimension(const Layer& layer, float *width_scale, float *height_scale) {
  if (IsNonIntegralSrcCrop(layer.src_rect)) {
    return false;
  }

  LayerRect crop;
  LayerRect dst;
  IntegerizeRect(&crop, layer.src_rect);
  IntegerizeRect(&dst, layer.dst_rect);

  bool rotated90 = (static_cast<int>(layer.transform.rotation) == 90);
  float crop_w = rotated90 ? crop.bottom - crop.top : crop.right - crop.left;
  float crop_h = rotated90 ? crop.right - crop.left : crop.bottom - crop.top;
  float dst_w = dst.right - dst.left;
  float dst_h = dst.bottom - dst.top;

  if ((dst_w < 1) || (dst_h < 1)) {
    return false;
  }

  float w_scale = crop_w / dst_w;
  float h_scale = crop_h / dst_h;

  if ((crop_w < kMaxCropWidth) ||(crop_h < kMaxCropHeight)) {
    return false;
  }

  if ((w_scale > 1.0f) || (h_scale > 1.0f)) {
    const uint32_t max_scale_down = hw_res_info_.max_scale_down;

    if (!hw_res_info_.has_decimation) {
      if (crop_w > kMaxSourcePipeWidth || w_scale > max_scale_down || h_scale > max_scale_down) {
        return false;
      }
    } else {
      if (w_scale > max_scale_down || h_scale > max_scale_down) {
        return false;
      }
    }
  }

  if (((w_scale < 1.0f) || (h_scale < 1.0f)) && (w_scale > 0.0f) && (h_scale > 0.0f)) {
    const uint32_t max_scale_up = hw_res_info_.max_scale_up;
    const float w_uscale = 1.0f / w_scale;
    const float h_uscale = 1.0f / h_scale;

    if (w_uscale > max_scale_up || h_uscale > max_scale_up) {
      return false;
    }
  }

  *width_scale = w_scale;
  *height_scale = h_scale;

  return true;
}

void ResManager::CalculateCut(float *left_cut_ratio,
    float *top_cut_ratio, float *right_cut_ratio, float *bottom_cut_ratio,
    const LayerTransform& transform) {
  if (transform.flip_horizontal) {
    Swap(*left_cut_ratio, *right_cut_ratio);
  }

  if (transform.flip_vertical) {
    Swap(*top_cut_ratio, *bottom_cut_ratio);
  }

  if (UINT32(transform.rotation) == 90) {
    // Anti clock swapping
    float tmp_cut_ratio = *left_cut_ratio;
    *left_cut_ratio = *top_cut_ratio;
    *top_cut_ratio = *right_cut_ratio;
    *right_cut_ratio = *bottom_cut_ratio;
    *bottom_cut_ratio = tmp_cut_ratio;
  }
}

void ResManager::CalculateCropRects(LayerRect *crop, LayerRect *dst,
    const LayerRect& scissor, const LayerTransform& transform) {
  float& crop_l = crop->left;
  float& crop_t = crop->top;
  float& crop_r = crop->right;
  float& crop_b = crop->bottom;
  float crop_w = crop->right - crop->left;
  float crop_h = crop->bottom - crop->top;

  float& dst_l = dst->left;
  float& dst_t = dst->top;
  float& dst_r = dst->right;
  float& dst_b = dst->bottom;
  float dst_w = (dst->right > dst->left) ? dst->right - dst->left :
    dst->left - dst->right;
  float dst_h = (dst->bottom > dst->top) ? dst->bottom > dst->top :
    dst->top > dst->bottom;

  const float& sci_l = scissor.left;
  const float& sci_t = scissor.top;
  const float& sci_r = scissor.right;
  const float& sci_b = scissor.bottom;

  float left_cut_ratio = 0.0, right_cut_ratio = 0.0, top_cut_ratio = 0.0,
    bottom_cut_ratio = 0.0;

  if (dst_l < sci_l) {
    left_cut_ratio = (sci_l - dst_l) / dst_w;
    dst_l = sci_l;
  }

  if (dst_r > sci_r) {
    right_cut_ratio = (dst_r - sci_r) / dst_w;
    dst_r = sci_r;
  }

  if (dst_t < sci_t) {
    top_cut_ratio = (sci_t - dst_t) / (dst_h);
    dst_t = sci_t;
  }

  if (dst_b > sci_b) {
    bottom_cut_ratio = (dst_b - sci_b) / (dst_h);
    dst_b = sci_b;
  }

  CalculateCut(&left_cut_ratio, &top_cut_ratio, &right_cut_ratio, &bottom_cut_ratio, transform);
  crop_l += crop_w * left_cut_ratio;
  crop_t += crop_h * top_cut_ratio;
  crop_r -= crop_w * right_cut_ratio;
  crop_b -= crop_h * bottom_cut_ratio;
}

bool ResManager::IsNonIntegralSrcCrop(const LayerRect& crop) {
  if (crop.left - roundf(crop.left)     ||
      crop.top - roundf(crop.top)       ||
      crop.right - roundf(crop.right)   ||
      crop.bottom - roundf(crop.bottom)) {
    return true;
  } else {
    return false;
  }
}

void ResManager::IntegerizeRect(LayerRect *dst_rect, const LayerRect &src_rect) {
  dst_rect->left = ceilf(src_rect.left);
  dst_rect->top = ceilf(src_rect.top);
  dst_rect->right = floorf(src_rect.right);
  dst_rect->bottom = floorf(src_rect.bottom);
}

}  // namespace sde

