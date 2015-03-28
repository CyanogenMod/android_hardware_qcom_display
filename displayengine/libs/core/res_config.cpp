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

namespace sde {

void ResManager::RotationConfig(const Layer &layer, const float &downscale, LayerRect *src_rect,
                                struct HWLayerConfig *layer_config, uint32_t *rotate_count) {
  HWRotateInfo *rotate = &layer_config->rotates[0];
  float src_width = src_rect->right - src_rect->left;
  float src_height = src_rect->bottom - src_rect->top;
  bool rot90 = IsRotationNeeded(layer.transform.rotation);
  bool is_opaque = (layer.blending == kBlendingOpaque);
  LayerRect dst_rect;
  // Rotate output is a temp buffer, always output to the top left corner for saving memory
  dst_rect.top = 0.0f;
  dst_rect.left = 0.0f;

  rotate->downscale_ratio = downscale;

  // downscale when doing rotation
  if (rot90) {
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

  // Set WHF for Rotator output
  LayerBufferFormat output_format;
  SetRotatorOutputFormat(layer.input_buffer->format, is_opaque, rot90, (downscale > 1.0f),
                         &output_format);
  HWBufferInfo *hw_buffer_info = &rotate->hw_buffer_info;
  hw_buffer_info->buffer_config.format = output_format;
  hw_buffer_info->buffer_config.width = UINT32(rotate->dst_roi.right);
  hw_buffer_info->buffer_config.height = UINT32(rotate->dst_roi.bottom);

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
  float src_width = src_rect.right - src_rect.left;
  float dst_width = dst_rect.right - dst_rect.left;
  float src_height = src_rect.bottom - src_rect.top;
  float dst_height = dst_rect.bottom - dst_rect.top;
  float left_mixer_width = FLOAT(display_attributes.split_left);

  uint8_t decimation = 0;
  if (CalculateDecimation((src_height / dst_height), &decimation) != kErrorNone) {
    return kErrorNotSupported;
  }
  // Adjust source height to consider decimation
  src_height /= powf(2.0f, decimation);

  // No need to include common factors in clock calculation of pipe & mixer
  float pipe_clock = MAX(dst_width, (dst_width * src_height / dst_height));
  float mixer_clock = left_mixer_width;

  // Layer cannot qualify for SrcSplit if source or destination width exceeds max pipe width.
  // For perf/power optimization, even if "always_src_split" is enabled, use 2 pipes only if:
  // 1. Source width is greater than split_left (left_mixer_width)
  // 2. Pipe clock exceeds the mixer clock
  if ((src_width > kMaxSourcePipeWidth) || (dst_width > kMaxSourcePipeWidth) ||
      (display_resource_ctx->display_attributes.always_src_split &&
      ((src_width > left_mixer_width) || (pipe_clock > mixer_clock)))) {
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
      RotationConfig(layer, rot_scale, &src_rect, layer_config, rotate_count);
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
    if (z_order > display_resource_ctx->max_mixer_stages) {
      DLOGV_IF(kTagResources, "z_order is over the limit of max_mixer_stages = %d",
               display_resource_ctx->max_mixer_stages);
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
  float src_h = pipe->src_roi.bottom - pipe->src_roi.top;
  float dst_h = pipe->dst_roi.bottom - pipe->dst_roi.top;
  float down_scale_h = src_h / dst_h;

  float src_w = pipe->src_roi.right - pipe->src_roi.left;
  float dst_w = pipe->dst_roi.right - pipe->dst_roi.left;
  float down_scale_w = src_w / dst_w;

  pipe->horizontal_decimation = 0;
  pipe->vertical_decimation = 0;

  if (CalculateDecimation(down_scale_w, &pipe->horizontal_decimation) != kErrorNone) {
    return kErrorNotSupported;
  }

  if (CalculateDecimation(down_scale_h, &pipe->vertical_decimation) != kErrorNone) {
    return kErrorNotSupported;
  }

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
    src_left->left = src_rect.right - src_width;
    src_left->right = src_rect.right;

    src_right->left = src_rect.left;
    src_right->right = src_left->left;
  } else {
    src_left->left = src_rect.left;
    src_left->right = src_rect.left + src_width;

    src_right->left = src_left->right;
    src_right->right = src_rect.right;
  }

  src_left->top = src_rect.top;
  src_left->bottom = src_rect.bottom;
  dst_left->top = dst_rect.top;
  dst_left->bottom = dst_rect.bottom;

  src_right->top = src_rect.top;
  src_right->bottom = src_rect.bottom;
  dst_right->top = dst_rect.top;
  dst_right->bottom = dst_rect.bottom;

  dst_left->left = dst_rect.left;
  dst_left->right = dst_rect.left + dst_width;
  dst_right->left = dst_left->right;
  dst_right->right = dst_rect.right;
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
    if (transform.flip_horizontal) {
      left_pipe->src_roi.left = right_pipe->src_roi.right;
    } else {
      right_pipe->src_roi.left = left_pipe->src_roi.right;
    }
    right_pipe->dst_roi.left = left_pipe->dst_roi.right;
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

DisplayError ResManager::CalculateDecimation(float downscale, uint8_t *decimation) {
  float max_down_scale = FLOAT(hw_res_info_.max_scale_down);

  if (downscale <= max_down_scale) {
    *decimation = 0;
    return kErrorNone;
  } else if (!hw_res_info_.has_decimation) {
    DLOGE("Downscaling exceeds the maximum MDP downscale limit but decimation not enabled");
    return kErrorNotSupported;
  }

  // Decimation is the remaining downscale factor after doing max SDE downscale.
  // In SDE, decimation is supported in powers of 2.
  // For ex: If a pipe needs downscale of 8 but max_down_scale is 4
  // So decimation = powf(2.0, ceilf(log2f(8 / 4))) = powf(2.0, 1.0) = 2
  *decimation = UINT8(ceilf(log2f(downscale / max_down_scale)));
  return kErrorNone;
}

}  // namespace sde
