/*
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hwc_layers.h"
#include <gr.h>
#include <utils/debug.h>
#include <cmath>

#define __CLASS__ "HWCLayer"

namespace sdm {

std::atomic<hwc2_layer_t> HWCLayer::next_id_(1);

// Layer operations
HWCLayer::HWCLayer(hwc2_display_t display_id) : id_(next_id_++), display_id_(display_id) {
  layer_ = new Layer();
  layer_->input_buffer = new LayerBuffer();
  // Fences are deferred, so the first time this layer is presented, return -1
  // TODO(user): Verify that fences are properly obtained on suspend/resume
  release_fences_.push(-1);
}

HWCLayer::~HWCLayer() {
  // Close any fences left for this layer
  while (!release_fences_.empty()) {
    close(release_fences_.front());
    release_fences_.pop();
  }

  if (layer_) {
    if (layer_->input_buffer) {
      delete (layer_->input_buffer);
    }
    delete layer_;
  }
}

HWC2::Error HWCLayer::SetLayerBuffer(buffer_handle_t buffer, int32_t acquire_fence) {
  if (!buffer) {
    DLOGE("Invalid buffer handle: %p on layer: %d", buffer, id_);
    return HWC2::Error::BadParameter;
  }

  if (acquire_fence == 0) {
    DLOGE("acquire_fence is zero");
    return HWC2::Error::BadParameter;
  }

  const private_handle_t *handle = static_cast<const private_handle_t *>(buffer);
  LayerBuffer *layer_buffer = layer_->input_buffer;
  layer_buffer->width = UINT32(handle->width);
  layer_buffer->height = UINT32(handle->height);
  layer_buffer->format = GetSDMFormat(handle->format, handle->flags);
  if (SetMetaData(handle, layer_) != kErrorNone) {
    return HWC2::Error::BadLayer;
  }

  if (handle->bufferType == BUFFER_TYPE_VIDEO) {
    layer_buffer->flags.video = true;
  }
  // TZ Protected Buffer - L1
  if (handle->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
    layer_buffer->flags.secure = true;
  }
  if (handle->flags & private_handle_t::PRIV_FLAGS_SECURE_DISPLAY) {
    layer_buffer->flags.secure_display = true;
  }

  layer_buffer->planes[0].fd = handle->fd;
  layer_buffer->planes[0].offset = handle->offset;
  layer_buffer->planes[0].stride = UINT32(handle->width);
  layer_buffer->acquire_fence_fd = acquire_fence;
  layer_buffer->buffer_id = reinterpret_cast<uint64_t>(handle);

  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerSurfaceDamage(hwc_region_t damage) {
  layer_->dirty_regions.clear();
  for (uint32_t i = 0; i < damage.numRects; i++) {
    LayerRect rect;
    SetRect(damage.rects[i], &rect);
    layer_->dirty_regions.push_back(rect);
  }
  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerBlendMode(HWC2::BlendMode mode) {
  LayerBlending blending = kBlendingPremultiplied;
  switch (mode) {
    case HWC2::BlendMode::Coverage:
      blending = kBlendingCoverage;
      break;
    case HWC2::BlendMode::Premultiplied:
      blending = kBlendingPremultiplied;
      break;
    case HWC2::BlendMode::None:
      blending = kBlendingOpaque;
      break;
    default:
      return HWC2::Error::BadParameter;
  }

  if (layer_->blending != blending) {
    geometry_changes_ |= kBlendMode;
    layer_->blending = blending;
  }
  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerColor(hwc_color_t color) {
  layer_->solid_fill_color = GetUint32Color(color);
  layer_->input_buffer->format = kFormatARGB8888;
  DLOGV_IF(kTagCompManager, "[%" PRIu64 "][%" PRIu64 "] Layer color set to %x", display_id_, id_,
           layer_->solid_fill_color);
  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerCompositionType(HWC2::Composition type) {
  client_requested_ = type;
  switch (type) {
    case HWC2::Composition::Client:
      break;
    case HWC2::Composition::Device:
      // We try and default to this in SDM
      break;
    case HWC2::Composition::SolidColor:
      break;
    case HWC2::Composition::Cursor:
      break;
    case HWC2::Composition::Invalid:
      return HWC2::Error::BadParameter;
    default:
      return HWC2::Error::Unsupported;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerDataspace(int32_t dataspace) {
  // TODO(user): Implement later
  geometry_changes_ |= kDataspace;
  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerDisplayFrame(hwc_rect_t frame) {
  LayerRect dst_rect = {};
  SetRect(frame, &dst_rect);
  if (layer_->dst_rect != dst_rect) {
    geometry_changes_ |= kDisplayFrame;
    layer_->dst_rect = dst_rect;
  }
  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerPlaneAlpha(float alpha) {
  // Conversion of float alpha in range 0.0 to 1.0 similar to the HWC Adapter
  uint8_t plane_alpha = static_cast<uint8_t>(std::round(255.0f * alpha));
  if (layer_->plane_alpha != plane_alpha) {
    geometry_changes_ |= kPlaneAlpha;
    layer_->plane_alpha = plane_alpha;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerSourceCrop(hwc_frect_t crop) {
  LayerRect src_rect = {};
  SetRect(crop, &src_rect);
  if (layer_->src_rect != src_rect) {
    geometry_changes_ |= kSourceCrop;
    layer_->src_rect = src_rect;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerTransform(HWC2::Transform transform) {
  LayerTransform layer_transform = {};
  switch (transform) {
    case HWC2::Transform::FlipH:
      layer_transform.flip_horizontal = true;
      break;
    case HWC2::Transform::FlipV:
      layer_transform.flip_vertical = true;
      break;
    case HWC2::Transform::Rotate90:
      layer_transform.rotation = 90.0f;
      break;
    case HWC2::Transform::Rotate180:
      layer_transform.flip_horizontal = true;
      layer_transform.flip_vertical = true;
      break;
    case HWC2::Transform::Rotate270:
      layer_transform.rotation = 90.0f;
      layer_transform.flip_horizontal = true;
      layer_transform.flip_vertical = true;
      break;
    case HWC2::Transform::FlipHRotate90:
      layer_transform.rotation = 90.0f;
      layer_transform.flip_horizontal = true;
      break;
    case HWC2::Transform::FlipVRotate90:
      layer_transform.rotation = 90.0f;
      layer_transform.flip_vertical = true;
      break;
    case HWC2::Transform::None:
      // do nothing
      break;
  }

  if (layer_->transform != layer_transform) {
    geometry_changes_ |= kTransform;
    layer_->transform = layer_transform;
  }
  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerVisibleRegion(hwc_region_t visible) {
  layer_->visible_regions.clear();
  for (uint32_t i = 0; i < visible.numRects; i++) {
    LayerRect rect;
    SetRect(visible.rects[i], &rect);
    layer_->visible_regions.push_back(rect);
  }

  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerZOrder(uint32_t z) {
  if (z_ != z) {
    geometry_changes_ |= kZOrder;
    z_ = z;
  }
  return HWC2::Error::None;
}

void HWCLayer::SetRect(const hwc_rect_t &source, LayerRect *target) {
  target->left = FLOAT(source.left);
  target->top = FLOAT(source.top);
  target->right = FLOAT(source.right);
  target->bottom = FLOAT(source.bottom);
}

void HWCLayer::SetRect(const hwc_frect_t &source, LayerRect *target) {
  // Recommended way of rounding as in hwcomposer2.h - SetLayerSourceCrop
  target->left = std::ceil(source.left);
  target->top = std::ceil(source.top);
  target->right = std::floor(source.right);
  target->bottom = std::floor(source.bottom);
}

uint32_t HWCLayer::GetUint32Color(const hwc_color_t &source) {
  // Returns 32 bit ARGB
  uint32_t a = UINT32(source.a) << 24;
  uint32_t r = UINT32(source.r) << 16;
  uint32_t g = UINT32(source.g) << 8;
  uint32_t b = UINT32(source.b);
  uint32_t color = a | r | g | b;
  return color;
}

LayerBufferFormat HWCLayer::GetSDMFormat(const int32_t &source, const int flags) {
  LayerBufferFormat format = kFormatInvalid;
  if (flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
    switch (source) {
      case HAL_PIXEL_FORMAT_RGBA_8888:
        format = kFormatRGBA8888Ubwc;
        break;
      case HAL_PIXEL_FORMAT_RGBX_8888:
        format = kFormatRGBX8888Ubwc;
        break;
      case HAL_PIXEL_FORMAT_BGR_565:
        format = kFormatBGR565Ubwc;
        break;
      case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
      case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
      case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
        format = kFormatYCbCr420SPVenusUbwc;
        break;
      default:
        DLOGE("Unsupported format type for UBWC %d", source);
        return kFormatInvalid;
    }
    return format;
  }

  switch (source) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
      format = kFormatRGBA8888;
      break;
    case HAL_PIXEL_FORMAT_RGBA_5551:
      format = kFormatRGBA5551;
      break;
    case HAL_PIXEL_FORMAT_RGBA_4444:
      format = kFormatRGBA4444;
      break;
    case HAL_PIXEL_FORMAT_BGRA_8888:
      format = kFormatBGRA8888;
      break;
    case HAL_PIXEL_FORMAT_RGBX_8888:
      format = kFormatRGBX8888;
      break;
    case HAL_PIXEL_FORMAT_BGRX_8888:
      format = kFormatBGRX8888;
      break;
    case HAL_PIXEL_FORMAT_RGB_888:
      format = kFormatRGB888;
      break;
    case HAL_PIXEL_FORMAT_RGB_565:
      format = kFormatRGB565;
      break;
    case HAL_PIXEL_FORMAT_BGR_565:
      format = kFormatBGR565;
      break;
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
      format = kFormatYCbCr420SemiPlanarVenus;
      break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
      format = kFormatYCrCb420SemiPlanarVenus;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
      format = kFormatYCbCr420SPVenusUbwc;
      break;
    case HAL_PIXEL_FORMAT_YV12:
      format = kFormatYCrCb420PlanarStride16;
      break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
      format = kFormatYCrCb420SemiPlanar;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
      format = kFormatYCbCr420SemiPlanar;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
      format = kFormatYCbCr422H2V1SemiPlanar;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
      format = kFormatYCbCr422H2V1Packed;
      break;
    case HAL_PIXEL_FORMAT_RGBA_1010102:
      format = kFormatRGBA1010102;
      break;
    case HAL_PIXEL_FORMAT_ARGB_2101010:
      format = kFormatARGB2101010;
      break;
    case HAL_PIXEL_FORMAT_RGBX_1010102:
      format = kFormatRGBX1010102;
      break;
    case HAL_PIXEL_FORMAT_XRGB_2101010:
      format = kFormatXRGB2101010;
      break;
    case HAL_PIXEL_FORMAT_BGRA_1010102:
      format = kFormatBGRA1010102;
      break;
    case HAL_PIXEL_FORMAT_ABGR_2101010:
      format = kFormatABGR2101010;
      break;
    case HAL_PIXEL_FORMAT_BGRX_1010102:
      format = kFormatBGRX1010102;
      break;
    case HAL_PIXEL_FORMAT_XBGR_2101010:
      format = kFormatXBGR2101010;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_P010:
      format = kFormatYCbCr420P010;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
      format = kFormatYCbCr420TP10Ubwc;
      break;
    default:
      DLOGW("Unsupported format type = %d", source);
      return kFormatInvalid;
  }

  return format;
}

LayerBufferS3DFormat HWCLayer::GetS3DFormat(uint32_t s3d_format) {
  LayerBufferS3DFormat sdm_s3d_format = kS3dFormatNone;
  switch (s3d_format) {
    case HAL_NO_3D:
      sdm_s3d_format = kS3dFormatNone;
      break;
    case HAL_3D_SIDE_BY_SIDE_L_R:
      sdm_s3d_format = kS3dFormatLeftRight;
      break;
    case HAL_3D_SIDE_BY_SIDE_R_L:
      sdm_s3d_format = kS3dFormatRightLeft;
      break;
    case HAL_3D_TOP_BOTTOM:
      sdm_s3d_format = kS3dFormatTopBottom;
      break;
    default:
      DLOGW("Invalid S3D format %d", s3d_format);
  }
  return sdm_s3d_format;
}

DisplayError HWCLayer::SetMetaData(const private_handle_t *pvt_handle, Layer *layer) {
  const MetaData_t *meta_data = reinterpret_cast<MetaData_t *>(pvt_handle->base_metadata);
  LayerBuffer *layer_buffer = layer->input_buffer;

  if (!meta_data) {
    return kErrorNone;
  }

  if (meta_data->operation & UPDATE_COLOR_SPACE) {
    if (SetCSC(meta_data->colorSpace, &layer_buffer->csc) != kErrorNone) {
      return kErrorNotSupported;
    }
  }

  if (meta_data->operation & SET_IGC) {
    if (SetIGC(meta_data->igc, &layer_buffer->igc) != kErrorNone) {
      return kErrorNotSupported;
    }
  }

  if (meta_data->operation & UPDATE_REFRESH_RATE) {
    layer->frame_rate = RoundToStandardFPS(meta_data->refreshrate);
  }

  if ((meta_data->operation & PP_PARAM_INTERLACED) && meta_data->interlaced) {
    layer_buffer->flags.interlace = true;
  }

  if (meta_data->operation & LINEAR_FORMAT) {
    layer_buffer->format = GetSDMFormat(INT32(meta_data->linearFormat), 0);
  }

  if (meta_data->operation & UPDATE_BUFFER_GEOMETRY) {
    int actual_width = pvt_handle->width;
    int actual_height = pvt_handle->height;
    AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(pvt_handle, actual_width, actual_height);
    layer_buffer->width = UINT32(actual_width);
    layer_buffer->height = UINT32(actual_height);
  }

  if (meta_data->operation & S3D_FORMAT) {
    layer_buffer->s3d_format = GetS3DFormat(meta_data->s3dFormat);
  }

  return kErrorNone;
}

DisplayError HWCLayer::SetCSC(ColorSpace_t source, LayerCSC *target) {
  switch (source) {
    case ITU_R_601:
      *target = kCSCLimitedRange601;
      break;
    case ITU_R_601_FR:
      *target = kCSCFullRange601;
      break;
    case ITU_R_709:
      *target = kCSCLimitedRange709;
      break;
    default:
      DLOGE("Unsupported CSC: %d", source);
      return kErrorNotSupported;
  }

  return kErrorNone;
}

DisplayError HWCLayer::SetIGC(IGC_t source, LayerIGC *target) {
  switch (source) {
    case IGC_NotSpecified:
      *target = kIGCNotSpecified;
      break;
    case IGC_sRGB:
      *target = kIGCsRGB;
      break;
    default:
      DLOGE("Unsupported IGC: %d", source);
      return kErrorNotSupported;
  }

  return kErrorNone;
}

uint32_t HWCLayer::RoundToStandardFPS(float fps) {
  static const uint32_t standard_fps[4] = {24, 30, 48, 60};
  uint32_t frame_rate = (uint32_t)(fps);

  int count = INT(sizeof(standard_fps) / sizeof(standard_fps[0]));
  for (int i = 0; i < count; i++) {
    if ((standard_fps[i] - frame_rate) < 2) {
      // Most likely used for video, the fps can fluctuate
      // Ex: b/w 29 and 30 for 30 fps clip
      return standard_fps[i];
    }
  }

  return frame_rate;
}

void HWCLayer::SetComposition(const LayerComposition &sdm_composition) {
  auto hwc_composition = HWC2::Composition::Invalid;
  switch (sdm_composition) {
    case kCompositionGPU:
      hwc_composition = HWC2::Composition::Client;
      break;
    case kCompositionHWCursor:
      hwc_composition = HWC2::Composition::Cursor;
      break;
    default:
      hwc_composition = HWC2::Composition::Device;
      break;
  }
  // Update solid fill composition
  if (sdm_composition == kCompositionSDE && layer_->flags.solid_fill != 0) {
    hwc_composition = HWC2::Composition::SolidColor;
  }
  device_selected_ = hwc_composition;

  return;
}
void HWCLayer::PushReleaseFence(int32_t fence) {
  release_fences_.push(fence);
}
int32_t HWCLayer::PopReleaseFence(void) {
  if (release_fences_.empty())
    return -1;
  auto fence = release_fences_.front();
  release_fences_.pop();
  return fence;
}

}  // namespace sdm
