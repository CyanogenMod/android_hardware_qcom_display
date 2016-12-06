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

#ifndef __GR_ALLOCATOR_H__
#define __GR_ALLOCATOR_H__

#ifdef MASTER_SIDE_CP
#define SECURE_ALIGN SZ_4K
#else
#define SECURE_ALIGN SZ_1M
#endif

#include "gralloc_priv.h"
#include "gr_buf_descriptor.h"
#include "gr_adreno_info.h"
#include "gr_ion_alloc.h"

namespace gralloc1 {

class Allocator {
 public:
  Allocator();
  ~Allocator();
  bool Init();
  int AllocateBuffer(const BufferDescriptor &descriptor, private_handle_t **pHnd);
  int MapBuffer(void **base, unsigned int size, unsigned int offset, int fd);
  int FreeBuffer(void *base, unsigned int size, unsigned int offset, int fd);
  int CleanBuffer(void *base, unsigned int size, unsigned int offset, int fd, int op);
  int AllocateMem(AllocData *data, gralloc1_producer_usage_t prod_usage,
                  gralloc1_consumer_usage_t cons_usage);
  bool IsMacroTileEnabled(int format, gralloc1_producer_usage_t prod_usage,
                          gralloc1_consumer_usage_t cons_usage);
  // @return : index of the descriptor with maximum buffer size req
  bool CheckForBufferSharing(uint32_t num_descriptors, const BufferDescriptor *descriptors,
                             int *max_index);
  int GetImplDefinedFormat(gralloc1_producer_usage_t prod_usage,
                           gralloc1_consumer_usage_t cons_usage, int format);
  unsigned int GetSize(const BufferDescriptor &d, unsigned int alignedw, unsigned int alignedh);
  void GetBufferSizeAndDimensions(const BufferDescriptor &d, unsigned int *size,
                                  unsigned int *alignedw, unsigned int *alignedh);
  void GetBufferSizeAndDimensions(int width, int height, int format, unsigned int *size,
                                  unsigned int *alignedw, unsigned int *alignedh);
  void GetAlignedWidthAndHeight(const BufferDescriptor &d, unsigned int *aligned_w,
                                unsigned int *aligned_h);
  void GetBufferAttributes(const BufferDescriptor &d, unsigned int *alignedw,
                           unsigned int *alignedh, int *tiled, unsigned int *size);
  int GetYUVPlaneInfo(const private_handle_t *hnd, struct android_ycbcr *ycbcr);
  int GetRgbDataAddress(private_handle_t *hnd, void **rgb_data);
  bool UseUncached(gralloc1_producer_usage_t usage);
  bool IsUBwcFormat(int format);
  bool IsUBwcSupported(int format);
  bool IsUBwcEnabled(int format, gralloc1_producer_usage_t prod_usage,
                     gralloc1_consumer_usage_t cons_usage);

 private:
  void GetYuvUBwcWidthAndHeight(int width, int height, int format, unsigned int *aligned_w,
                                unsigned int *aligned_h);
  void GetYuvSPPlaneInfo(uint64_t base, uint32_t width, uint32_t height, uint32_t bpp,
                         struct android_ycbcr *ycbcr);
  void GetYuvUbwcSPPlaneInfo(uint64_t base, uint32_t width, uint32_t height, int color_format,
                             struct android_ycbcr *ycbcr);
  void GetRgbUBwcBlockSize(uint32_t bpp, int *block_width, int *block_height);
  unsigned int GetRgbUBwcMetaBufferSize(int width, int height, uint32_t bpp);
  unsigned int GetUBwcSize(int width, int height, int format, unsigned int alignedw,
                           unsigned int alignedh);
  void GetIonHeapInfo(gralloc1_producer_usage_t prod_usage, gralloc1_consumer_usage_t cons_usage,
                      unsigned int *ion_heap_id, unsigned int *alloc_type, unsigned int *ion_flags);

  bool gpu_support_macrotile = false;
  bool display_support_macrotile = false;
  IonAlloc *ion_allocator_ = NULL;
  AdrenoMemInfo *adreno_helper_ = NULL;
};

}  // namespace gralloc1

#endif  // __GR_ALLOCATOR_H__
