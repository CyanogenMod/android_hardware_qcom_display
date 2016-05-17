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

#ifndef __GR_ADRENO_INFO_H__
#define __GR_ADRENO_INFO_H__

#ifdef VENUS_COLOR_FORMAT
#include <media/msm_media_info.h>
#else
#define VENUS_Y_STRIDE(args...) 0
#define VENUS_Y_SCANLINES(args...) 0
#define VENUS_BUFFER_SIZE(args...) 0
#endif

namespace gralloc1 {

// Adreno Pixel Formats
typedef enum {
  ADRENO_PIXELFORMAT_UNKNOWN = 0,
  ADRENO_PIXELFORMAT_R8G8B8A8 = 28,
  ADRENO_PIXELFORMAT_R8G8B8A8_SRGB = 29,
  ADRENO_PIXELFORMAT_B5G6R5 = 85,
  ADRENO_PIXELFORMAT_B5G5R5A1 = 86,
  ADRENO_PIXELFORMAT_B8G8R8A8 = 90,
  ADRENO_PIXELFORMAT_B8G8R8A8_SRGB = 91,
  ADRENO_PIXELFORMAT_B8G8R8X8_SRGB = 93,
  ADRENO_PIXELFORMAT_NV12 = 103,
  ADRENO_PIXELFORMAT_YUY2 = 107,
  ADRENO_PIXELFORMAT_B4G4R4A4 = 115,
  ADRENO_PIXELFORMAT_NV12_EXT = 506,       // NV12 with non-std alignment and offsets
  ADRENO_PIXELFORMAT_R8G8B8X8 = 507,       //  GL_RGB8 (Internal)
  ADRENO_PIXELFORMAT_R8G8B8 = 508,         //  GL_RGB8
  ADRENO_PIXELFORMAT_A1B5G5R5 = 519,       //  GL_RGB5_A1
  ADRENO_PIXELFORMAT_R8G8B8X8_SRGB = 520,  //  GL_SRGB8
  ADRENO_PIXELFORMAT_R8G8B8_SRGB = 521,    //  GL_SRGB8
  ADRENO_PIXELFORMAT_R5G6B5 = 610,         //  RGBA version of B5G6R5
  ADRENO_PIXELFORMAT_R5G5B5A1 = 611,       //  RGBA version of B5G5R5A1
  ADRENO_PIXELFORMAT_R4G4B4A4 = 612,       //  RGBA version of B4G4R4A4
  ADRENO_PIXELFORMAT_UYVY = 614,           //  YUV 4:2:2 packed progressive (1 plane)
  ADRENO_PIXELFORMAT_NV21 = 619,
  ADRENO_PIXELFORMAT_Y8U8V8A8 = 620,  // YUV 4:4:4 packed (1 plane)
  ADRENO_PIXELFORMAT_Y8 = 625,        //  Single 8-bit luma only channel YUV format
} ADRENOPIXELFORMAT;

class AdrenoMemInfo {
 public:
  AdrenoMemInfo();

  ~AdrenoMemInfo();

  bool Init();

  /*
   * Function to compute aligned width and aligned height based on
   * width, height, format and usage flags.
   *
   * @return aligned width, aligned height
   */
  void GetAlignedWidthAndHeight(int width, int height, int format, int usage,
                                unsigned int *aligned_w, unsigned int *aligned_h, bool ubwc_enabled,
                                bool tile_enabled);

  /*
   * Function to compute the adreno aligned width and aligned height
   * based on the width and format.
   *
   * @return aligned width, aligned height
   */
  void AlignUnCompressedRGB(int width, int height, int format, int tileEnabled,
                            unsigned int *aligned_w, unsigned int *aligned_h);

  /*
   * Function to compute the adreno aligned width and aligned height
   * based on the width and format.
   *
   * @return aligned width, aligned height
   */
  void AlignCompressedRGB(int width, int height, int format, unsigned int *aligned_w,
                          unsigned int *aligned_h);

  /*
   * Function to compute the pixel alignment requirement.
   *
   * @return alignment
   */
  uint32_t GetGpuPixelAlignment();

  /*
   * Function to return whether GPU support MacroTile feature
   *
   * @return >0 : supported
   *          0 : not supported
   */
  bool IsMacroTilingSupportedByGPU();

  /*
   * Function to query whether GPU supports UBWC for given HAL format
   * @return > 0 : supported
   *           0 : not supported
   */
  bool IsUBWCSupportedByGPU(int format);

  /*
   * Function to get the corresponding Adreno format for given HAL format
   */
  ADRENOPIXELFORMAT GetGpuPixelFormat(int hal_format);

 private:
  // link(s)to adreno surface padding library.
  int (*LINK_adreno_compute_padding)(int width, int bpp, int surface_tile_height,
                                     int screen_tile_height, int padding_threshold) = NULL;
  void (*LINK_adreno_compute_aligned_width_and_height)(int width, int height, int bpp,
                                                       int tile_mode, int raster_mode,
                                                       int padding_threshold, int *aligned_w,
                                                       int *aligned_h) = NULL;
  int (*LINK_adreno_isMacroTilingSupportedByGpu)(void) = NULL;
  void (*LINK_adreno_compute_compressedfmt_aligned_width_and_height)(
      int width, int height, int format, int tile_mode, int raster_mode, int padding_threshold,
      int *aligned_w, int *aligned_h, int *bpp) = NULL;
  int (*LINK_adreno_isUBWCSupportedByGpu)(ADRENOPIXELFORMAT format) = NULL;
  unsigned int (*LINK_adreno_get_gpu_pixel_alignment)() = NULL;

  bool gfx_ubwc_disable_ = false;
  bool map_fb_ = false;
  void *libadreno_utils_ = NULL;
};

}  // namespace gralloc1

#endif  // __GR_ADRENO_INFO_H__
