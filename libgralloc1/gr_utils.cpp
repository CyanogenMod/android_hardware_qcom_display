/*
 * Copyright (c) 2011-2016, The Linux Foundation. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gr_utils.h"

namespace gralloc1 {

bool IsUncompressedRGBFormat(int format) {
  switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_RGB_888:
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_BGR_565:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_RGBA_5551:
    case HAL_PIXEL_FORMAT_RGBA_4444:
    case HAL_PIXEL_FORMAT_R_8:
    case HAL_PIXEL_FORMAT_RG_88:
    case HAL_PIXEL_FORMAT_BGRX_8888:
    case HAL_PIXEL_FORMAT_RGBA_1010102:
    case HAL_PIXEL_FORMAT_ARGB_2101010:
    case HAL_PIXEL_FORMAT_RGBX_1010102:
    case HAL_PIXEL_FORMAT_XRGB_2101010:
    case HAL_PIXEL_FORMAT_BGRA_1010102:
    case HAL_PIXEL_FORMAT_ABGR_2101010:
    case HAL_PIXEL_FORMAT_BGRX_1010102:
    case HAL_PIXEL_FORMAT_XBGR_2101010:
      return true;
    default:
      break;
  }

  return false;
}

bool IsCompressedRGBFormat(int format) {
  switch (format) {
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_4x4_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_5x4_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_5x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_6x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_6x6_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x6_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x8_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x6_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x8_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x10_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_12x10_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_12x12_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR:
      return true;
    default:
      break;
  }

  return false;
}

uint32_t GetBppForUncompressedRGB(int format) {
  uint32_t bpp = 0;
  switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_BGRX_8888:
      bpp = 4;
      break;
    case HAL_PIXEL_FORMAT_RGB_888:
      bpp = 3;
      break;
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_BGR_565:
    case HAL_PIXEL_FORMAT_RGBA_5551:
    case HAL_PIXEL_FORMAT_RGBA_4444:
      bpp = 2;
      break;
    default:
      ALOGE("Error : %s New format request", __FUNCTION__);
      break;
  }

  return bpp;
}

bool CpuCanAccess(gralloc1_producer_usage_t prod_usage, gralloc1_consumer_usage_t cons_usage) {
  return CpuCanRead(prod_usage, cons_usage) || CpuCanWrite(prod_usage);
}

bool CpuCanRead(gralloc1_producer_usage_t prod_usage, gralloc1_consumer_usage_t cons_usage) {
  if (prod_usage & GRALLOC1_PRODUCER_USAGE_CPU_READ) {
    return true;
  }

  if (cons_usage & GRALLOC1_CONSUMER_USAGE_CPU_READ) {
    return true;
  }

  return false;
}

bool CpuCanWrite(gralloc1_producer_usage_t prod_usage) {
  if (prod_usage & GRALLOC1_PRODUCER_USAGE_CPU_WRITE) {
    // Application intends to use CPU for rendering
    return true;
  }

  return false;
}

}  // namespace gralloc1
