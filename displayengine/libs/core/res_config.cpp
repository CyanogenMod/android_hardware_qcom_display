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

#include <math.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/rect.h>

#include "res_manager.h"

#define __CLASS__ "ResManager"

using scalar::PipeInfo;
using scalar::LayerInfo;

namespace sde {

void ResManager::RotationConfig(const LayerTransform &transform, const float &downscale,
                                LayerRect *src_rect, struct HWLayerConfig *layer_config,
                                uint32_t *rotate_count) {
  HWRotateInfo *rotate = &layer_config->rotates[0];
  float src_width = src_rect->right - src_rect->left;
  float src_height = src_rect->bottom - src_rect->top;
  LayerRect dst_rect;
  // Rotate output is a temp buffer, always output to the top left corner for saving memory
  dst_rect.top = 0.0f;
  dst_rect.left = 0.0f;

  rotate->downscale_ratio = downscale;

  // downscale when doing rotation
  if (IsRotationNeeded(transform.rotation)) {
    if (downscale > 1.0f) {
      src_height = ROUND_UP_ALIGN_DOWN(src_height, downscale);
      src_rect->bottom = src_rect->top + src_height;
      src_width = ROUND_UP_ALIGN_DOWN(src_width, downscale);
      src_rect->right = src_rect->left + src_width;
    }
    dst_rect.right = src_height / downscale;
    dst_rect.bottom = src_width / downscale;
  } else {
    if (downscale > 1.0f) {
      src_width = ROUND_UP_ALIGN_DOWN(src_width, downscale);
      src_rect->right = src_rect->left + src_width;
      src_height = ROUND_UP_ALIGN_DOWN(src_height, downscale);
      src_rect->bottom = src_rect->top + src_height;
    }
    dst_rect.right = src_width / downscale;
    dst_rect.bottom = src_height / downscale;
  }

  rotate->src_roi = *src_rect;
  rotate->valid = true;
  rotate->dst_roi = dst_rect;

  *src_rect = dst_rect;
  layer_config->num_rotate = 1;
  (*rotate_count)++;
}

DisplayError ResManager::SrcSplitConfig(DisplayResourceContext *display_resource_ctx,
                                        const LayerTransform &transform, const LayerRect &src_rect,
                                        const LayerRect &dst_rect, HWLayerConfig *layer_config,
                                        uint32_t align_x) {
  HWDisplayAttributes &display_attributes = display_resource_ctx->display_attributes;
  HWPipeInfo *left_pipe = &layer_config->left_pipe;
  HWPipeInfo *right_pipe = &layer_config->right_pipe;

  if ((src_rect.right - src_rect.left) > kMaxSourcePipeWidth ||
      (dst_rect.right - dst_rect.left) > kMaxInterfaceWidth || hw_res_info_.always_src_split) {
    SplitRect(transform.flip_horizontal, src_rect, dst_rect, &left_pipe->src_roi,
              &left_pipe->dst_roi, &right_pipe->src_roi, &right_pipe->dst_roi, align_x);
    left_pipe->valid = true;
    right_pipe->valid = true;
  } else {
    left_pipe->src_roi = src_rect;
    left_pipe->dst_roi = dst_rect;
    left_pipe->valid = true;
    right_pipe->Reset();
  }

  return kErrorNone;
}

DisplayError ResManager::DisplaySplitConfig(DisplayResourceContext *display_resource_ctx,
                                            const LayerTransform &transform,
                                            const LayerRect &src_rect, const LayerRect &dst_rect,
                                            HWLayerConfig *layer_config, uint32_t align_x) {
  LayerRect scissor_dst_left, scissor_dst_right;
  HWDisplayAttributes &display_attributes = display_resource_ctx->display_attributes;

  // for display split case
  HWPipeInfo *left_pipe = &layer_config->left_pipe;
  HWPipeInfo *right_pipe = &layer_config->right_pipe;
  LayerRect scissor, dst_left, crop_left, crop_right, dst_right;
  scissor.right = FLOAT(display_attributes.split_left);
  scissor.bottom = FLOAT(display_attributes.y_pixels);

  crop_left = src_rect;
  dst_left = dst_rect;
  crop_right = crop_left;
  dst_right = dst_left;
  bool crop_left_valid = CalculateCropRects(scissor, transform, &crop_left, &dst_left);

  scissor.left = FLOAT(display_attributes.split_left);
  scissor.top = 0.0f;
  scissor.right = FLOAT(display_attributes.x_pixels);
  scissor.bottom = FLOAT(display_attributes.y_pixels);
  bool crop_right_valid = false;

  if (IsValidRect(scissor)) {
    crop_right_valid = CalculateCropRects(scissor, transform, &crop_right, &dst_right);
  }

  if (crop_left_valid && (crop_left.right - crop_left.left) > kMaxSourcePipeWidth) {
    if (crop_right_valid) {
      DLOGV_IF(kTagResources, "Need more than 2 pipes: left width = %.0f, right width = %.0f",
               crop_left.right - crop_left.left, crop_right.right - crop_right.left);
      return kErrorNotSupported;
    }
    // 2 pipes both are on the left
    SplitRect(transform.flip_horizontal, crop_left, dst_left, &left_pipe->src_roi,
              &left_pipe->dst_roi, &right_pipe->src_roi, &right_pipe->dst_roi, align_x);
    left_pipe->valid = true;
    right_pipe->valid = true;
    crop_right_valid = true;
  } else if (crop_right_valid && (crop_right.right - crop_right.left) > kMaxSourcePipeWidth) {
    if (crop_left_valid) {
      DLOGV_IF(kTagResources, "Need more than 2 pipes: left width = %.0f, right width = %.0f",
               crop_left.right - crop_left.left, crop_right.right - crop_right.left);
      return kErrorNotSupported;
    }
    // 2 pipes both are on the right
    SplitRect(transform.flip_horizontal, crop_right, dst_right, &left_pipe->src_roi,
              &left_pipe->dst_roi, &right_pipe->src_roi, &right_pipe->dst_roi, align_x);
    left_pipe->valid = true;
    right_pipe->valid = true;
    crop_left_valid = true;
  } else if (crop_left_valid) {
    // assign left pipe
    left_pipe->src_roi = crop_left;
    left_pipe->dst_roi = dst_left;
    left_pipe->valid = true;
  } else {
    // Set default value, left_pipe is not needed.
    left_pipe->Reset();
  }

  // assign right pipe if needed
  if (crop_right_valid) {
    if (left_pipe->valid) {
      right_pipe->src_roi = crop_right;
      right_pipe->dst_roi = dst_right;
      right_pipe->valid = true;
    } else {
      // If left pipe is not used, use left pipe first.
      left_pipe->src_roi = crop_right;
      left_pipe->dst_roi = dst_right;
      left_pipe->valid = true;
      right_pipe->Reset();
    }
  } else {
    // need not right pipe
    right_pipe->Reset();
  }

  return kErrorNone;
}

DisplayError ResManager::Config(DisplayResourceContext *display_resource_ctx, HWLayers *hw_layers,
                                uint32_t *rotate_count) {
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;
  HWDisplayAttributes &display_attributes = display_resource_ctx->display_attributes;
  HWLayersInfo &layer_info = hw_layers->info;
  DisplayError error = kErrorNone;
  uint32_t z_order = 0;

  for (uint32_t i = 0; i < layer_info.count; i++) {
    Layer& layer = layer_info.stack->layers[layer_info.index[i]];
    float rot_scale = 1.0f;
    if (!IsValidDimension(layer.src_rect, layer.dst_rect)) {
      DLOGV_IF(kTagResources, "Input is invalid");
      LogRect(kTagResources, "input layer src_rect", layer.src_rect);
      LogRect(kTagResources, "input layer dst_rect", layer.dst_rect);
      return kErrorNotSupported;
    }

    LayerRect scissor, src_rect, dst_rect;
    src_rect = layer.src_rect;
    dst_rect = layer.dst_rect;
    scissor.right = FLOAT(display_attributes.x_pixels);
    scissor.bottom = FLOAT(display_attributes.y_pixels);

    struct HWLayerConfig *layer_config = &hw_layers->config[i];
    HWPipeInfo &left_pipe = layer_config->left_pipe;
    HWPipeInfo &right_pipe = layer_config->right_pipe;

    if (!CalculateCropRects(scissor, layer.transform, &src_rect, &dst_rect)) {
      layer_config->Reset();
      left_pipe.Reset();
      right_pipe.Reset();
      continue;
    }

    uint32_t align_x = 1, align_y = 1;
    if (IsYuvFormat(layer.input_buffer->format)) {
      // TODO(user) Select x and y alignment according to the format
      align_x = 2;
      align_y = 2;
      NormalizeRect(align_x, align_y, &src_rect);
    }

    if (ValidateScaling(layer, src_rect, dst_rect, &rot_scale)) {
      return kErrorNotSupported;
    }

    // config rotator first
    for (uint32_t j = 0; j < kMaxRotatePerLayer; j++) {
      layer_config->rotates[j].Reset();
    }
    layer_config->num_rotate = 0;

    LayerTransform transform = layer.transform;
    if (IsRotationNeeded(transform.rotation) || UINT32(rot_scale) != 1) {
      RotationConfig(layer.transform, rot_scale, &src_rect, layer_config, rotate_count);
      // rotator will take care of flipping, reset tranform
      transform = LayerTransform();
    }

    if (hw_res_info_.is_src_split) {
      error = SrcSplitConfig(display_resource_ctx, transform, src_rect,
                             dst_rect, layer_config, align_x);
    } else {
      error = DisplaySplitConfig(display_resource_ctx, transform, src_rect,
                                 dst_rect, layer_config, align_x);
    }

    if (error != kErrorNone) {
      break;
    }

    error = AlignPipeConfig(layer, transform, &left_pipe, &right_pipe, align_x, align_y);
    if (error != kErrorNone) {
      break;
    }

    DLOGV_IF(kTagResources, "==== layer = %d, left pipe valid = %d ====",
             i, layer_config->left_pipe.valid);
    LogRect(kTagResources, "input layer src_rect", layer.src_rect);
    LogRect(kTagResources, "input layer dst_rect", layer.dst_rect);
    for (uint32_t k = 0; k < layer_config->num_rotate; k++) {
      DLOGV_IF(kTagResources, "rotate num = %d, scale_x = %.2f", k, rot_scale);
      LogRect(kTagResources, "rotate src", layer_config->rotates[k].src_roi);
      LogRect(kTagResources, "rotate dst", layer_config->rotates[k].dst_roi);
    }

    LogRect(kTagResources, "cropped src_rect", src_rect);
    LogRect(kTagResources, "cropped dst_rect", dst_rect);
    LogRect(kTagResources, "left pipe src", layer_config->left_pipe.src_roi);
    LogRect(kTagResources, "left pipe dst", layer_config->left_pipe.dst_roi);
    if (hw_layers->config[i].right_pipe.valid) {
      LogRect(kTagResources, "right pipe src", layer_config->right_pipe.src_roi);
      LogRect(kTagResources, "right pipe dst", layer_config->right_pipe.dst_roi);
    }
    // set z_order, left_pipe should always be valid
    left_pipe.z_order = z_order;
    if (layer_config->right_pipe.valid) {
      // use different z_order if 2 pipes are on one mixer and without src split support
      if (!hw_res_info_.is_src_split &&
          ((left_pipe.dst_roi.right <= display_attributes.split_left &&
            right_pipe.dst_roi.right <= display_attributes.split_left) ||
           (left_pipe.dst_roi.left >= display_attributes.split_left &&
            right_pipe.dst_roi.left >= display_attributes.split_left))) {
        z_order++;
      }
      layer_config->right_pipe.z_order = z_order;
    }
    z_order++;
    if (z_order >= hw_res_info_.num_blending_stages) {
      DLOGV_IF(kTagResources, "z_order is over the limit: z_order = %d", z_order);
      return kErrorResources;
    }
  }

  return error;
}

DisplayError ResManager::ValidateScaling(const Layer &layer, const LayerRect &crop,
                                         const LayerRect &dst, float *rot_scale) {
  bool rotated90 = IsRotationNeeded(layer.transform.rotation) && (rot_scale != NULL);
  float crop_width = rotated90 ? crop.bottom - crop.top : crop.right - crop.left;
  float crop_height = rotated90 ? crop.right - crop.left : crop.bottom - crop.top;
  float dst_width = dst.right - dst.left;
  float dst_height = dst.bottom - dst.top;

  if ((dst_width < 1.0f) || (dst_height < 1.0f)) {
    DLOGV_IF(kTagResources, "dst ROI is too small w = %.0f, h = %.0f, right = %.0f, bottom = %.0f",
             dst_width, dst_height, dst.right, dst.bottom);
    return kErrorNotSupported;
  }

  if ((crop_width < 1.0f) || (crop_height < 1.0f)) {
    DLOGV_IF(kTagResources, "src ROI is too small w = %.0f, h = %.0f, right = %.0f, bottom = %.0f",
             crop_width, crop_height, crop.right, crop.bottom);
    return kErrorNotSupported;
  }

  if ((UINT32(crop_width - dst_width) == 1) || (UINT32(crop_height - dst_height) == 1)) {
    DLOGV_IF(kTagResources, "One pixel downscaling detected crop_w = %.0f, dst_w = %.0f, " \
             "crop_h = %.0f, dst_h = %.0f", crop_width, dst_width, crop_height, dst_height);
    return kErrorNotSupported;
  }

  float scale_x = crop_width / dst_width;
  float scale_y = crop_height / dst_height;
  uint32_t rot_scale_local = 1;

  if ((UINT32(scale_x) > 1) || (UINT32(scale_y) > 1)) {
    float max_scale_down = FLOAT(hw_res_info_.max_scale_down);

    if (hw_res_info_.has_rotator_downscale && !property_setting_.disable_rotator_downscaling &&
        rot_scale && !IsMacroTileFormat(layer.input_buffer)) {
      float scale_min = MIN(scale_x, scale_y);
      float scale_max = MAX(scale_x, scale_y);
      // use rotator to downscale when over the pipe scaling ability
      if (UINT32(scale_min) >= 2 && scale_max > max_scale_down) {
        // downscaling ratio needs be the same for both direction, use the smaller one.
        rot_scale_local = 1 << UINT32(ceilf(log2f(scale_min / max_scale_down)));
        if (rot_scale_local > kMaxRotateDownScaleRatio)
          rot_scale_local = kMaxRotateDownScaleRatio;
        scale_x /= FLOAT(rot_scale_local);
        scale_y /= FLOAT(rot_scale_local);
      }
      *rot_scale = FLOAT(rot_scale_local);
    }

    if (hw_res_info_.has_decimation && !property_setting_.disable_decimation &&
               !IsMacroTileFormat(layer.input_buffer)) {
      max_scale_down *= FLOAT(kMaxDecimationDownScaleRatio);
    }

    if (scale_x > max_scale_down || scale_y > max_scale_down) {
      DLOGV_IF(kTagResources,
               "Scaling down is over the limit: is_tile = %d, scale_x = %.0f, scale_y = %.0f, " \
               "crop_w = %.0f, dst_w = %.0f, has_deci = %d, disable_deci = %d, rot_scale = %d",
               IsMacroTileFormat(layer.input_buffer), scale_x, scale_y, crop_width, dst_width,
               hw_res_info_.has_decimation, property_setting_.disable_decimation, rot_scale_local);
      return kErrorNotSupported;
    }
  }

  float max_scale_up = FLOAT(hw_res_info_.max_scale_up);
  if (UINT32(scale_x) < 1 && scale_x > 0.0f) {
    if ((1.0f / scale_x) > max_scale_up) {
      DLOGV_IF(kTagResources, "Scaling up is over limit scale_x = %f", 1.0f / scale_x);
      return kErrorNotSupported;
    }
  }

  if (UINT32(scale_y) < 1 && scale_y > 0.0f) {
    if ((1.0f / scale_y) > max_scale_up) {
      DLOGV_IF(kTagResources, "Scaling up is over limit scale_y = %f", 1.0f / scale_y);
      return kErrorNotSupported;
    }
  }

  DLOGV_IF(kTagResources, "scale_x = %.4f, scale_y = %.4f, rot_scale = %d",
           scale_x, scale_y, rot_scale_local);

  return kErrorNone;
}

void ResManager::CalculateCut(const LayerTransform &transform, float *left_cut_ratio,
                              float *top_cut_ratio, float *right_cut_ratio,
                              float *bottom_cut_ratio) {
  if (transform.flip_horizontal) {
    Swap(*left_cut_ratio, *right_cut_ratio);
  }

  if (transform.flip_vertical) {
    Swap(*top_cut_ratio, *bottom_cut_ratio);
  }

  if (IsRotationNeeded(transform.rotation)) {
    // Anti clock swapping
    float tmp_cut_ratio = *left_cut_ratio;
    *left_cut_ratio = *top_cut_ratio;
    *top_cut_ratio = *right_cut_ratio;
    *right_cut_ratio = *bottom_cut_ratio;
    *bottom_cut_ratio = tmp_cut_ratio;
  }
}

bool ResManager::CalculateCropRects(const LayerRect &scissor, const LayerTransform &transform,
                                    LayerRect *crop, LayerRect *dst) {
  float &crop_left = crop->left;
  float &crop_top = crop->top;
  float &crop_right = crop->right;
  float &crop_bottom = crop->bottom;
  float crop_width = crop->right - crop->left;
  float crop_height = crop->bottom - crop->top;

  float &dst_left = dst->left;
  float &dst_top = dst->top;
  float &dst_right = dst->right;
  float &dst_bottom = dst->bottom;
  float dst_width = dst->right - dst->left;
  float dst_height = dst->bottom - dst->top;

  const float &sci_left = scissor.left;
  const float &sci_top = scissor.top;
  const float &sci_right = scissor.right;
  const float &sci_bottom = scissor.bottom;

  float left_cut_ratio = 0.0, right_cut_ratio = 0.0, top_cut_ratio = 0.0, bottom_cut_ratio = 0.0;
  bool need_cut = false;

  if (dst_left < sci_left) {
    left_cut_ratio = (sci_left - dst_left) / dst_width;
    dst_left = sci_left;
    need_cut = true;
  }

  if (dst_right > sci_right) {
    right_cut_ratio = (dst_right - sci_right) / dst_width;
    dst_right = sci_right;
    need_cut = true;
  }

  if (dst_top < sci_top) {
    top_cut_ratio = (sci_top - dst_top) / (dst_height);
    dst_top = sci_top;
    need_cut = true;
  }

  if (dst_bottom > sci_bottom) {
    bottom_cut_ratio = (dst_bottom - sci_bottom) / (dst_height);
    dst_bottom = sci_bottom;
    need_cut = true;
  }

  if (!need_cut)
    return true;

  CalculateCut(transform, &left_cut_ratio, &top_cut_ratio, &right_cut_ratio, &bottom_cut_ratio);

  crop_left += crop_width * left_cut_ratio;
  crop_top += crop_height * top_cut_ratio;
  crop_right -= crop_width * right_cut_ratio;
  crop_bottom -= crop_height * bottom_cut_ratio;
  NormalizeRect(1, 1, crop);
  NormalizeRect(1, 1, dst);
  if (IsValidRect(*crop) && IsValidRect(*dst))
    return true;
  else
    return false;
}

bool ResManager::IsValidDimension(const LayerRect &src, const LayerRect &dst) {
  // Make sure source in integral
  if (src.left - roundf(src.left)     ||
      src.top - roundf(src.top)       ||
      src.right - roundf(src.right)   ||
      src.bottom - roundf(src.bottom)) {
    DLOGV_IF(kTagResources, "Input ROI is not integral");
    return false;
  }

  if (src.left > src.right || src.top > src.bottom || dst.left > dst.right ||
      dst.top > dst.bottom) {
    return false;
  } else {
    return true;
  }
}

DisplayError ResManager::SetDecimationFactor(HWPipeInfo *pipe) {
  float max_down_scale = FLOAT(hw_res_info_.max_scale_down);
  float src_h = pipe->src_roi.bottom - pipe->src_roi.top;
  float dst_h = pipe->dst_roi.bottom - pipe->dst_roi.top;
  float down_scale_h = src_h / dst_h;

  float src_w = pipe->src_roi.right - pipe->src_roi.left;
  float dst_w = pipe->dst_roi.right - pipe->dst_roi.left;
  float down_scale_w = src_w / dst_w;


  pipe->horizontal_decimation = 0;
  pipe->vertical_decimation = 0;

  // TODO(user): Need to check for the maximum downscale limit for decimation and return error
  if (!hw_res_info_.has_decimation && ((down_scale_w > max_down_scale) ||
      (down_scale_h > max_down_scale))) {
    DLOGV("Downscaling exceeds the maximum MDP downscale limit and decimation not enabled");
    return kErrorNotSupported;
  }

  if ((down_scale_w <= max_down_scale) && (down_scale_h <= max_down_scale)) {
    return kErrorNone;
  }

  // Decimation is the remaining downscale factor after doing max SDE downscale.
  // In SDE, decimation is supported in powers of 2.
  // For ex: If a pipe needs downscale of 8 but max_down_scale is 4
  // So decimation = powf(2.0, ceilf(log2f(8) - log2f(4))) = powf(2.0, 1.0) = 2
  pipe->horizontal_decimation = UINT8(ceilf(log2f(down_scale_w) - log2f(max_down_scale)));
  pipe->vertical_decimation = UINT8(ceilf(log2f(down_scale_h) - log2f(max_down_scale)));

  DLOGI_IF(kTagResources, "horizontal_decimation %d, vertical_decimation %d",
           pipe->horizontal_decimation, pipe->vertical_decimation);

  return kErrorNone;
}

void ResManager::SplitRect(bool flip_horizontal, const LayerRect &src_rect,
                           const LayerRect &dst_rect, LayerRect *src_left, LayerRect *dst_left,
                           LayerRect *src_right, LayerRect *dst_right, uint32_t align_x) {
  // Split rectangle horizontally and evenly into two.
  float src_width = src_rect.right - src_rect.left;
  float dst_width = dst_rect.right - dst_rect.left;
  float src_width_ori = src_width;
  src_width = ROUND_UP_ALIGN_DOWN(src_width / 2, align_x);
  dst_width = ROUND_UP_ALIGN_DOWN(dst_width * src_width / src_width_ori, 1);

  if (flip_horizontal) {
    src_left->top = src_rect.top;
    src_left->left = src_rect.left;
    src_left->right = src_rect.left + src_width;
    src_left->bottom = src_rect.bottom;

    dst_left->top = dst_rect.top;
    dst_left->left = dst_rect.left + dst_width;
    dst_left->right = dst_rect.right;
    dst_left->bottom = dst_rect.bottom;

    src_right->top = src_rect.top;
    src_right->left = src_left->right;
    src_right->right = src_rect.right;
    src_right->bottom = src_rect.bottom;

    dst_right->top = dst_rect.top;
    dst_right->left = dst_rect.left;
    dst_right->right = dst_left->left;
    dst_right->bottom = dst_rect.bottom;
  } else {
    src_left->top = src_rect.top;
    src_left->left = src_rect.left;
    src_left->right = src_rect.left + src_width;
    src_left->bottom = src_rect.bottom;

    dst_left->top = dst_rect.top;
    dst_left->left = dst_rect.left;
    dst_left->right = dst_rect.left + dst_width;
    dst_left->bottom = dst_rect.bottom;

    src_right->top = src_rect.top;
    src_right->left = src_left->right;
    src_right->right = src_rect.right;
    src_right->bottom = src_rect.bottom;

    dst_right->top = dst_rect.top;
    dst_right->left = dst_left->right;
    dst_right->right = dst_rect.right;
    dst_right->bottom = dst_rect.bottom;
  }
}

// Scalar helper functions
static void SetPipeInfo(HWPipeInfo* hw_pipe, PipeInfo* pipe) {
  pipe->id = hw_pipe->pipe_id;
  pipe->scale_data = &hw_pipe->scale_data;
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

static void UpdateSrcRoi(PipeInfo* pipe, HWPipeInfo* hw_pipe) {
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

bool ResManager::ConfigureScaling(HWLayers *hw_layers) {
  HWLayersInfo &hw_layer_info = hw_layers->info;

  for (uint32_t i = 0; i < hw_layer_info.count; i++) {
    Layer &layer = hw_layer_info.stack->layers[hw_layer_info.index[i]];
    LayerBuffer *input_buffer = layer.input_buffer;
    HWPipeInfo* left_pipe = &hw_layers->config[i].left_pipe;
    HWPipeInfo* right_pipe = &hw_layers->config[i].right_pipe;

    // Prepare data structure for lib scalar
    uint32_t flags = 0;
    struct LayerInfo layer_info;

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
      PipeInfo* scalar_pipe = (count == 0) ? &layer_info.left_pipe : &layer_info.right_pipe;

      if (rotate_info->valid)
        input_buffer = &rotate_info->hw_buffer_info.output_buffer;

      scalar_pipe->flags = flags;
      hw_pipe->scale_data.src_width = input_buffer->width;
      SetPipeInfo(hw_pipe, scalar_pipe);
    }
    layer_info.src_format = GetScalarFormat(input_buffer->format);

    DLOGV_IF(kTagResources, "Scalar Input[%d] flags=%x format=%x", i, flags, layer_info.src_format);
    DLOGV_IF(kTagResources, "Left: id=%d hD=%d vD=%d srcRect=[%d %d %d %d] dstRect=[%d %d %d %d]",
        layer_info.left_pipe.id, layer_info.left_pipe.horz_deci, layer_info.left_pipe.vert_deci,
        layer_info.left_pipe.src_rect.x, layer_info.left_pipe.src_rect.y,
        layer_info.left_pipe.src_rect.w, layer_info.left_pipe.src_rect.h,
        layer_info.left_pipe.dst_rect.x, layer_info.left_pipe.dst_rect.y,
        layer_info.left_pipe.dst_rect.w, layer_info.left_pipe.dst_rect.h);
    DLOGV_IF(kTagResources, "Right: id=%d hD=%d vD=%d srcRect=[%d %d %d %d] dstRect=[%d %d %d %d]",
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
    if (left_pipe->scale_data.enable_pxl_ext)
      UpdateSrcRoi(&layer_info.left_pipe, left_pipe);
    if (right_pipe->scale_data.enable_pxl_ext)
      UpdateSrcRoi(&layer_info.right_pipe, right_pipe);
  }

  return true;
}

DisplayError ResManager::AlignPipeConfig(const Layer &layer, const LayerTransform &transform,
                                         HWPipeInfo *left_pipe, HWPipeInfo *right_pipe,
                                         uint32_t align_x, uint32_t align_y) {
  DisplayError error = kErrorNone;
  if (!left_pipe->valid) {
    DLOGE_IF(kTagResources, "left_pipe should not be invalid");
    return kErrorNotSupported;
  }
  // 1. Normalize video layer source rectangle to multiple of 2, as MDP hardware require source
  //    rectangle of video layer to be even.
  // 2. Normalize source and destination rect of a layer to multiple of 1.
  // TODO(user) Check buffer format and check if rotate is involved.

  NormalizeRect(align_x, align_y, &left_pipe->src_roi);
  NormalizeRect(1, 1, &left_pipe->dst_roi);

  if (right_pipe->valid) {
    NormalizeRect(align_x, align_y, &right_pipe->src_roi);
    NormalizeRect(1, 1, &right_pipe->dst_roi);
  }

  if (right_pipe->valid) {
    // Make sure the  left and right ROI are conjunct
    right_pipe->src_roi.left = left_pipe->src_roi.right;
    if (transform.flip_horizontal) {
      right_pipe->dst_roi.right = left_pipe->dst_roi.left;
    } else {
      right_pipe->dst_roi.left = left_pipe->dst_roi.right;
    }
  }
  error = ValidateScaling(layer, left_pipe->src_roi, left_pipe->dst_roi, NULL);
  if (error != kErrorNone) {
    goto PipeConfigExit;
  }

  if (right_pipe->valid) {
    error = ValidateScaling(layer, right_pipe->src_roi, right_pipe->dst_roi, NULL);
  }
PipeConfigExit:
  if (error != kErrorNone) {
    DLOGV_IF(kTagResources, "AlignPipeConfig failed");
  }
  return error;
}

}  // namespace sde
