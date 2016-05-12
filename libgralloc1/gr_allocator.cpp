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

#include <cutils/log.h>
#include <algorithm>

#include "gr_utils.h"
#include "gr_allocator.h"
#include "gr_adreno_info.h"
#include "gralloc_priv.h"

#include "qd_utils.h"
#include "qdMetaData.h"

#define ASTC_BLOCK_SIZE 16

#ifndef ION_FLAG_CP_PIXEL
#define ION_FLAG_CP_PIXEL 0
#endif

#ifndef ION_FLAG_ALLOW_NON_CONTIG
#define ION_FLAG_ALLOW_NON_CONTIG 0
#endif

#ifdef MASTER_SIDE_CP
#define CP_HEAP_ID ION_SECURE_HEAP_ID
#define SD_HEAP_ID ION_SECURE_DISPLAY_HEAP_ID
#define ION_CP_FLAGS (ION_SECURE | ION_FLAG_CP_PIXEL)
#define ION_SD_FLAGS (ION_SECURE | ION_FLAG_CP_SEC_DISPLAY)
#else  // SLAVE_SIDE_CP
#define CP_HEAP_ID ION_CP_MM_HEAP_ID
#define SD_HEAP_ID CP_HEAP_ID
#define ION_CP_FLAGS (ION_SECURE | ION_FLAG_ALLOW_NON_CONTIG)
#define ION_SD_FLAGS ION_SECURE
#endif

namespace gralloc1 {

Allocator::Allocator() : ion_allocator_(NULL), adreno_helper_(NULL) {
}

bool Allocator::Init() {
  ion_allocator_ = new IonAlloc();
  if (!ion_allocator_->Init()) {
    return false;
  }

  adreno_helper_ = new AdrenoMemInfo();
  if (!adreno_helper_->Init()) {
    return false;
  }

  gpu_support_macrotile = adreno_helper_->IsMacroTilingSupportedByGPU();
  int supports_macrotile = 0;
  qdutils::querySDEInfo(qdutils::HAS_MACRO_TILE, &supports_macrotile);
  display_support_macrotile = !!supports_macrotile;

  return true;
}

Allocator::~Allocator() {
  if (ion_allocator_) {
    delete ion_allocator_;
  }

  if (adreno_helper_) {
    delete adreno_helper_;
  }
}

int Allocator::AllocateMem(AllocData *alloc_data, gralloc1_producer_usage_t prod_usage,
                           gralloc1_consumer_usage_t cons_usage) {
  int ret;
  alloc_data->uncached = UseUncached(prod_usage);

  // After this point we should have the right heap set, there is no fallback
  GetIonHeapInfo(prod_usage, cons_usage, &alloc_data->heap_id, &alloc_data->alloc_type,
                 &alloc_data->flags);

  ret = ion_allocator_->AllocBuffer(alloc_data);
  if (ret >= 0) {
    alloc_data->alloc_type |= private_handle_t::PRIV_FLAGS_USES_ION;
  } else {
    ALOGE("%s: Failed to allocate buffer - heap: 0x%x flags: 0x%x", __FUNCTION__,
          alloc_data->heap_id, alloc_data->flags);
  }

  return ret;
}

// Allocates buffer from width, height and format into a
// private_handle_t. It is the responsibility of the caller
// to free the buffer using the FreeBuffer function
int Allocator::AllocateBuffer(const BufferDescriptor &descriptor, private_handle_t **pHnd) {
  AllocData data;
  unsigned int aligned_w, aligned_h;
  data.base = 0;
  data.fd = -1;
  data.offset = 0;
  data.align = (unsigned int)getpagesize();
  int format = descriptor.GetFormat();
  gralloc1_producer_usage_t prod_usage = descriptor.GetProducerUsage();
  gralloc1_consumer_usage_t cons_usage = descriptor.GetConsumerUsage();
  GetBufferSizeAndDimensions(descriptor, &data.size, &aligned_w, &aligned_h);

  int err = AllocateMem(&data, prod_usage, cons_usage);
  if (0 != err) {
    ALOGE("%s: allocate failed", __FUNCTION__);
    return -ENOMEM;
  }

  if (IsUBwcEnabled(format, prod_usage, cons_usage)) {
    data.alloc_type |= private_handle_t::PRIV_FLAGS_UBWC_ALIGNED;
  }

  // Metadata is not allocated. would be empty
  private_handle_t *hnd = new private_handle_t(
      data.fd, data.size, INT(data.alloc_type), 0, INT(format), INT(aligned_w), INT(aligned_h), -1,
      0, 0, descriptor.GetWidth(), descriptor.GetHeight(), prod_usage, cons_usage);
  hnd->base = (uint64_t)data.base;
  hnd->offset = data.offset;
  hnd->gpuaddr = 0;
  *pHnd = hnd;

  return 0;
}

int Allocator::MapBuffer(void **base, unsigned int size, unsigned int offset, int fd) {
  if (ion_allocator_) {
    return ion_allocator_->MapBuffer(base, size, offset, fd);
  }

  return -EINVAL;
}

int Allocator::FreeBuffer(void *base, unsigned int size, unsigned int offset, int fd) {
  if (ion_allocator_) {
    return ion_allocator_->FreeBuffer(base, size, offset, fd);
  }

  return -EINVAL;
}

int Allocator::CleanBuffer(void *base, unsigned int size, unsigned int offset, int fd, int op) {
  if (ion_allocator_) {
    return ion_allocator_->CleanBuffer(base, size, offset, fd, op);
  }

  return -EINVAL;
}

bool Allocator::CheckForBufferSharing(uint32_t num_descriptors, const BufferDescriptor *descriptors,
                                      int *max_index) {
  unsigned int cur_heap_id = 0, prev_heap_id = 0;
  unsigned int cur_alloc_type = 0, prev_alloc_type = 0;
  unsigned int cur_ion_flags = 0, prev_ion_flags = 0;
  bool cur_uncached = false, prev_uncached = false;
  unsigned int alignedw, alignedh;
  unsigned int max_size = 0;

  *max_index = -1;
  for (uint32_t i = 0; i < num_descriptors; i++) {
    // Check Cached vs non-cached and all the ION flags
    cur_uncached = UseUncached(descriptors[i].GetProducerUsage());
    GetIonHeapInfo(descriptors[i].GetProducerUsage(), descriptors[i].GetConsumerUsage(),
                   &cur_heap_id, &cur_alloc_type, &cur_ion_flags);

    if (i > 0 && (cur_heap_id != prev_heap_id || cur_alloc_type != prev_alloc_type ||
                  cur_ion_flags != prev_ion_flags)) {
      return false;
    }

    // For same format type, find the descriptor with bigger size
    GetAlignedWidthAndHeight(descriptors[i], &alignedw, &alignedh);
    unsigned int size = GetSize(descriptors[i], alignedw, alignedh);
    if (max_size < size) {
      *max_index = INT(i);
      max_size = size;
    }

    prev_heap_id = cur_heap_id;
    prev_uncached = cur_uncached;
    prev_ion_flags = cur_ion_flags;
    prev_alloc_type = cur_alloc_type;
  }

  return true;
}

bool Allocator::IsMacroTileEnabled(int format, gralloc1_producer_usage_t prod_usage,
                                   gralloc1_consumer_usage_t cons_usage) {
  bool tile_enabled = false;

  // Check whether GPU & MDSS supports MacroTiling feature
  if (!adreno_helper_->IsMacroTilingSupportedByGPU() || !display_support_macrotile) {
    return tile_enabled;
  }

  // check the format
  switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_BGR_565:
      if (!CpuCanAccess(prod_usage, cons_usage)) {
        // not touched by CPU
        tile_enabled = true;
      }
      break;
    default:
      break;
  }

  return tile_enabled;
}

// helper function
unsigned int Allocator::GetSize(const BufferDescriptor &descriptor, unsigned int alignedw,
                                unsigned int alignedh) {
  unsigned int size = 0;
  int format = descriptor.GetFormat();
  int width = descriptor.GetWidth();
  int height = descriptor.GetHeight();
  gralloc1_producer_usage_t prod_usage = descriptor.GetProducerUsage();
  gralloc1_consumer_usage_t cons_usage = descriptor.GetConsumerUsage();

  if (IsUBwcEnabled(format, prod_usage, cons_usage)) {
    return GetUBwcSize(width, height, format, alignedw, alignedh);
  }

  if (IsUncompressedRGBFormat(format)) {
    uint32_t bpp = GetBppForUncompressedRGB(format);
    size = alignedw * alignedh * bpp;
    return size;
  }

  if (IsCompressedRGBFormat(format)) {
    size = alignedw * alignedh * ASTC_BLOCK_SIZE;
    return size;
  }

  // Below switch should be for only YUV/custom formats
  switch (format) {
    case HAL_PIXEL_FORMAT_RAW16:
      size = alignedw * alignedh * 2;
      break;
    case HAL_PIXEL_FORMAT_RAW10:
      size = ALIGN(alignedw * alignedh, SIZE_4K);
      break;

    // adreno formats
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO:  // NV21
      size = ALIGN(alignedw * alignedh, SIZE_4K);
      size += (unsigned int)ALIGN(2 * ALIGN(width / 2, 32) * ALIGN(height / 2, 32), SIZE_4K);
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:  // NV12
      // The chroma plane is subsampled,
      // but the pitch in bytes is unchanged
      // The GPU needs 4K alignment, but the video decoder needs 8K
      size = ALIGN(alignedw * alignedh, SIZE_8K);
      size += ALIGN(alignedw * (unsigned int)ALIGN(height / 2, 32), SIZE_8K);
      break;
    case HAL_PIXEL_FORMAT_YV12:
      if ((format == HAL_PIXEL_FORMAT_YV12) && ((width & 1) || (height & 1))) {
        ALOGE("w or h is odd for the YV12 format");
        return 0;
      }
      size = alignedw * alignedh + (ALIGN(alignedw / 2, 16) * (alignedh / 2)) * 2;
      size = ALIGN(size, (unsigned int)SIZE_4K);
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
      size = ALIGN((alignedw * alignedh) + (alignedw * alignedh) / 2 + 1, SIZE_4K);
      break;
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
    case HAL_PIXEL_FORMAT_YCrCb_422_SP:
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
    case HAL_PIXEL_FORMAT_YCrCb_422_I:
      if (width & 1) {
        ALOGE("width is odd for the YUV422_SP format");
        return 0;
      }
      size = ALIGN(alignedw * alignedh * 2, SIZE_4K);
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
      size = VENUS_BUFFER_SIZE(COLOR_FMT_NV12, width, height);
      break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
      size = VENUS_BUFFER_SIZE(COLOR_FMT_NV21, width, height);
      break;
    case HAL_PIXEL_FORMAT_BLOB:
    case HAL_PIXEL_FORMAT_RAW_OPAQUE:
      if (height != 1) {
        ALOGE("%s: Buffers with HAL_PIXEL_FORMAT_BLOB must have height 1 ", __FUNCTION__);
        return 0;
      }
      size = (unsigned int)width;
      break;
    case HAL_PIXEL_FORMAT_NV21_ZSL:
      size = ALIGN((alignedw * alignedh) + (alignedw * alignedh) / 2, SIZE_4K);
      break;
    default:
      ALOGE("%s: Unrecognized pixel format: 0x%x", __FUNCTION__, format);
      return 0;
  }

  return size;
}

void Allocator::GetBufferSizeAndDimensions(int width, int height, int format, unsigned int *size,
                                           unsigned int *alignedw, unsigned int *alignedh) {
  BufferDescriptor descriptor = BufferDescriptor(width, height, format);
  GetAlignedWidthAndHeight(descriptor, alignedw, alignedh);

  *size = GetSize(descriptor, *alignedw, *alignedh);
}

void Allocator::GetBufferSizeAndDimensions(const BufferDescriptor &descriptor, unsigned int *size,
                                           unsigned int *alignedw, unsigned int *alignedh) {
  GetAlignedWidthAndHeight(descriptor, alignedw, alignedh);

  *size = GetSize(descriptor, *alignedw, *alignedh);
}

void Allocator::GetBufferAttributes(const BufferDescriptor &descriptor, unsigned int *alignedw,
                                    unsigned int *alignedh, int *tiled, unsigned int *size) {
  int format = descriptor.GetFormat();
  gralloc1_producer_usage_t prod_usage = descriptor.GetProducerUsage();
  gralloc1_consumer_usage_t cons_usage = descriptor.GetConsumerUsage();

  *tiled = false;
  if (IsUBwcEnabled(format, prod_usage, cons_usage) ||
      IsMacroTileEnabled(format, prod_usage, cons_usage)) {
    *tiled = true;
  }

  GetAlignedWidthAndHeight(descriptor, alignedw, alignedh);
  *size = GetSize(descriptor, *alignedw, *alignedh);
}

void Allocator::GetYuvUbwcSPPlaneInfo(uint64_t base, uint32_t width, uint32_t height,
                                      int color_format, struct android_ycbcr *ycbcr) {
  // UBWC buffer has these 4 planes in the following sequence:
  // Y_Meta_Plane, Y_Plane, UV_Meta_Plane, UV_Plane
  unsigned int y_meta_stride, y_meta_height, y_meta_size;
  unsigned int y_stride, y_height, y_size;
  unsigned int c_meta_stride, c_meta_height, c_meta_size;
  unsigned int alignment = 4096;

  y_meta_stride = VENUS_Y_META_STRIDE(color_format, INT(width));
  y_meta_height = VENUS_Y_META_SCANLINES(color_format, INT(height));
  y_meta_size = ALIGN((y_meta_stride * y_meta_height), alignment);

  y_stride = VENUS_Y_STRIDE(color_format, INT(width));
  y_height = VENUS_Y_SCANLINES(color_format, INT(height));
  y_size = ALIGN((y_stride * y_height), alignment);

  c_meta_stride = VENUS_UV_META_STRIDE(color_format, INT(width));
  c_meta_height = VENUS_UV_META_SCANLINES(color_format, INT(height));
  c_meta_size = ALIGN((c_meta_stride * c_meta_height), alignment);

  ycbcr->y = reinterpret_cast<void *>(base + y_meta_size);
  ycbcr->cb = reinterpret_cast<void *>(base + y_meta_size + y_size + c_meta_size);
  ycbcr->cr = reinterpret_cast<void *>(base + y_meta_size + y_size + c_meta_size + 1);
  ycbcr->ystride = y_stride;
  ycbcr->cstride = VENUS_UV_STRIDE(color_format, INT(width));
}

void Allocator::GetYuvSPPlaneInfo(uint64_t base, uint32_t width, uint32_t height, uint32_t bpp,
                                  struct android_ycbcr *ycbcr) {
  unsigned int ystride, cstride;

  ystride = cstride = UINT(width) * bpp;
  ycbcr->y = reinterpret_cast<void *>(base);
  ycbcr->cb = reinterpret_cast<void *>(base + ystride * UINT(height));
  ycbcr->cr = reinterpret_cast<void *>(base + ystride * UINT(height) + 1);
  ycbcr->ystride = ystride;
  ycbcr->cstride = cstride;
  ycbcr->chroma_step = 2 * bpp;
}

int Allocator::GetYUVPlaneInfo(const private_handle_t *hnd, struct android_ycbcr *ycbcr) {
  int err = 0;
  uint32_t width = UINT(hnd->width);
  uint32_t height = UINT(hnd->height);
  int format = hnd->format;
  gralloc1_producer_usage_t prod_usage = hnd->GetProducerUsage();
  gralloc1_consumer_usage_t cons_usage = hnd->GetConsumerUsage();
  unsigned int ystride, cstride;

  memset(ycbcr->reserved, 0, sizeof(ycbcr->reserved));
  MetaData_t *metadata = reinterpret_cast<MetaData_t *>(hnd->base_metadata);

  // Check if UBWC buffer has been rendered in linear format.
  if (metadata && (metadata->operation & LINEAR_FORMAT)) {
    format = INT(metadata->linearFormat);
  }

  // Check metadata if the geometry has been updated.
  if (metadata && metadata->operation & UPDATE_BUFFER_GEOMETRY) {
    int usage = 0;

    if (hnd->flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
      usage = GRALLOC1_PRODUCER_USAGE_PRIVATE_ALLOC_UBWC;
    }

    BufferDescriptor descriptor =
        BufferDescriptor(metadata->bufferDim.sliceWidth, metadata->bufferDim.sliceHeight, format,
                         prod_usage, cons_usage);
    GetAlignedWidthAndHeight(descriptor, &width, &height);
  }

  // Get the chroma offsets from the handle width/height. We take advantage
  // of the fact the width _is_ the stride
  switch (format) {
    // Semiplanar
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
      // Same as YCbCr_420_SP_VENUS
      GetYuvSPPlaneInfo(hnd->base, width, height, 1, ycbcr);
      break;

    case HAL_PIXEL_FORMAT_YCbCr_420_P010:
      GetYuvSPPlaneInfo(hnd->base, width, height, 2, ycbcr);
      break;

    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
      GetYuvUbwcSPPlaneInfo(hnd->base, width, height, COLOR_FMT_NV12_UBWC, ycbcr);
      ycbcr->chroma_step = 2;
      break;

    case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
      GetYuvUbwcSPPlaneInfo(hnd->base, width, height, COLOR_FMT_NV12_BPP10_UBWC, ycbcr);
      ycbcr->chroma_step = 3;
      break;

    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_YCrCb_422_SP:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
    case HAL_PIXEL_FORMAT_NV21_ZSL:
    case HAL_PIXEL_FORMAT_RAW16:
    case HAL_PIXEL_FORMAT_RAW10:
      GetYuvSPPlaneInfo(hnd->base, width, height, 1, ycbcr);
      std::swap(ycbcr->cb, ycbcr->cr);
      break;

    // Planar
    case HAL_PIXEL_FORMAT_YV12:
      ystride = width;
      cstride = ALIGN(width / 2, 16);
      ycbcr->y = reinterpret_cast<void *>(hnd->base);
      ycbcr->cr = reinterpret_cast<void *>(hnd->base + ystride * height);
      ycbcr->cb = reinterpret_cast<void *>(hnd->base + ystride * height + cstride * height / 2);
      ycbcr->ystride = ystride;
      ycbcr->cstride = cstride;
      ycbcr->chroma_step = 1;
      break;

    // Unsupported formats
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
    case HAL_PIXEL_FORMAT_YCrCb_422_I:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
    default:
      ALOGD("%s: Invalid format passed: 0x%x", __FUNCTION__, format);
      err = -EINVAL;
  }

  return err;
}

int Allocator::GetImplDefinedFormat(gralloc1_producer_usage_t prod_usage,
                                    gralloc1_consumer_usage_t cons_usage, int format) {
  int gr_format = format;

  // If input format is HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED then based on
  // the usage bits, gralloc assigns a format.
  if (format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED ||
      format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
    if (prod_usage & GRALLOC1_PRODUCER_USAGE_PRIVATE_ALLOC_UBWC) {
      gr_format = HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC;
    } else if (cons_usage & GRALLOC1_CONSUMER_USAGE_VIDEO_ENCODER) {
      gr_format = HAL_PIXEL_FORMAT_NV12_ENCODEABLE;  // NV12
    } else if (prod_usage & GRALLOC1_PRODUCER_USAGE_PRIVATE_CAMERA_ZSL) {
      gr_format = HAL_PIXEL_FORMAT_NV21_ZSL;  // NV21 ZSL
    } else if (cons_usage & GRALLOC1_CONSUMER_USAGE_CAMERA) {
      gr_format = HAL_PIXEL_FORMAT_YCrCb_420_SP;  // NV21
    } else if (prod_usage & GRALLOC1_PRODUCER_USAGE_CAMERA) {
      if (format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
        gr_format = HAL_PIXEL_FORMAT_NV21_ZSL;  // NV21
      } else {
        gr_format = HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS;  // NV12 preview
      }
    } else if (cons_usage & GRALLOC1_CONSUMER_USAGE_HWCOMPOSER) {
      // XXX: If we still haven't set a format, default to RGBA8888
      gr_format = HAL_PIXEL_FORMAT_RGBA_8888;
    } else if (format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
      // If no other usage flags are detected, default the
      // flexible YUV format to NV21_ZSL
      gr_format = HAL_PIXEL_FORMAT_NV21_ZSL;
    }
  }

  return gr_format;
}

// Explicitly defined UBWC formats
bool Allocator::IsUBwcFormat(int format) {
  switch (format) {
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
      return true;
    default:
      return false;
  }
}

bool Allocator::IsUBwcSupported(int format) {
  // Existing HAL formats with UBWC support
  switch (format) {
    case HAL_PIXEL_FORMAT_BGR_565:
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
      return true;
    default:
      break;
  }

  return false;
}

/* The default policy is to return cached buffers unless the client explicity
 * sets the PRIVATE_UNCACHED flag or indicates that the buffer will be rarely
 * read or written in software. */
// TODO(user) : As of now relying only on producer usage
bool Allocator::UseUncached(gralloc1_producer_usage_t usage) {
  if ((usage & GRALLOC1_PRODUCER_USAGE_PRIVATE_UNCACHED) ||
      (usage & GRALLOC1_PRODUCER_USAGE_PROTECTED)) {
    return true;
  }

  // CPU read rarely
  if ((usage & GRALLOC1_PRODUCER_USAGE_CPU_READ) &&
      !(usage & GRALLOC1_PRODUCER_USAGE_CPU_READ_OFTEN)) {
    return true;
  }

  // CPU  write rarely
  if ((usage & GRALLOC1_PRODUCER_USAGE_CPU_WRITE) &&
      !(usage & GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN)) {
    return true;
  }

  return false;
}

void Allocator::GetIonHeapInfo(gralloc1_producer_usage_t prod_usage,
                               gralloc1_consumer_usage_t cons_usage, unsigned int *ion_heap_id,
                               unsigned int *alloc_type, unsigned int *ion_flags) {
  unsigned int heap_id = 0;
  unsigned int type = 0;
  int flags = 0;
  if (prod_usage & GRALLOC1_PRODUCER_USAGE_PROTECTED) {
    if (cons_usage & GRALLOC1_CONSUMER_USAGE_PRIVATE_SECURE_DISPLAY) {
      heap_id = ION_HEAP(SD_HEAP_ID);
      /*
       * There is currently no flag in ION for Secure Display
       * VM. Please add it to the define once available.
       */
      flags |= ION_SD_FLAGS;
    } else {
      heap_id = ION_HEAP(CP_HEAP_ID);
      flags |= ION_CP_FLAGS;
    }
  } else if (prod_usage & GRALLOC1_PRODUCER_USAGE_PRIVATE_MM_HEAP) {
    // MM Heap is exclusively a secure heap.
    // If it is used for non secure cases, fallback to IOMMU heap
    ALOGW("MM_HEAP cannot be used as an insecure heap. Using system heap instead!!");
    heap_id |= ION_HEAP(ION_SYSTEM_HEAP_ID);
  }

  if (prod_usage & GRALLOC1_PRODUCER_USAGE_PRIVATE_CAMERA_HEAP) {
    heap_id |= ION_HEAP(ION_CAMERA_HEAP_ID);
  }

  if (prod_usage & GRALLOC1_PRODUCER_USAGE_PRIVATE_ADSP_HEAP) {
    heap_id |= ION_HEAP(ION_ADSP_HEAP_ID);
  }

  if (flags & ION_SECURE) {
    type |= private_handle_t::PRIV_FLAGS_SECURE_BUFFER;
  }

  // if no ion heap flags are set, default to system heap
  if (!heap_id) {
    heap_id = ION_HEAP(ION_SYSTEM_HEAP_ID);
  }

  *alloc_type = type;
  *ion_flags = (unsigned int)flags;
  *ion_heap_id = heap_id;

  return;
}

bool Allocator::IsUBwcEnabled(int format, gralloc1_producer_usage_t prod_usage,
                              gralloc1_consumer_usage_t cons_usage) {
  // Allow UBWC, if client is using an explicitly defined UBWC pixel format.
  if (IsUBwcFormat(format)) {
    return true;
  }

  // Allow UBWC, if an OpenGL client sets UBWC usage flag and GPU plus MDP
  // support the format. OR if a non-OpenGL client like Rotator, sets UBWC
  // usage flag and MDP supports the format.
  if ((prod_usage & GRALLOC1_PRODUCER_USAGE_PRIVATE_ALLOC_UBWC) && IsUBwcSupported(format)) {
    bool enable = true;
    // Query GPU for UBWC only if buffer is intended to be used by GPU.
    if ((cons_usage & GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE) ||
        (prod_usage & GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET)) {
      enable = adreno_helper_->IsUBWCSupportedByGPU(format);
    }

    // Allow UBWC, only if CPU usage flags are not set
    if (enable && !(CpuCanAccess(prod_usage, cons_usage))) {
      return true;
    }
  }

  return false;
}

void Allocator::GetYuvUBwcWidthAndHeight(int width, int height, int format, unsigned int *aligned_w,
                                         unsigned int *aligned_h) {
  switch (format) {
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
      *aligned_w = VENUS_Y_STRIDE(COLOR_FMT_NV12_UBWC, width);
      *aligned_h = VENUS_Y_SCANLINES(COLOR_FMT_NV12_UBWC, height);
      break;
    default:
      ALOGE("%s: Unsupported pixel format: 0x%x", __FUNCTION__, format);
      *aligned_w = 0;
      *aligned_h = 0;
      break;
  }
}

void Allocator::GetRgbUBwcBlockSize(uint32_t bpp, int *block_width, int *block_height) {
  *block_width = 0;
  *block_height = 0;

  switch (bpp) {
    case 2:
    case 4:
      *block_width = 16;
      *block_height = 4;
      break;
    case 8:
      *block_width = 8;
      *block_height = 4;
      break;
    case 16:
      *block_width = 4;
      *block_height = 4;
      break;
    default:
      ALOGE("%s: Unsupported bpp: %d", __FUNCTION__, bpp);
      break;
  }
}

unsigned int Allocator::GetRgbUBwcMetaBufferSize(int width, int height, uint32_t bpp) {
  unsigned int size = 0;
  int meta_width, meta_height;
  int block_width, block_height;

  GetRgbUBwcBlockSize(bpp, &block_width, &block_height);
  if (!block_width || !block_height) {
    ALOGE("%s: Unsupported bpp: %d", __FUNCTION__, bpp);
    return size;
  }

  // Align meta buffer height to 16 blocks
  meta_height = ALIGN(((height + block_height - 1) / block_height), 16);

  // Align meta buffer width to 64 blocks
  meta_width = ALIGN(((width + block_width - 1) / block_width), 64);

  // Align meta buffer size to 4K
  size = (unsigned int)ALIGN((meta_width * meta_height), 4096);

  return size;
}

unsigned int Allocator::GetUBwcSize(int width, int height, int format, unsigned int alignedw,
                                    unsigned int alignedh) {
  unsigned int size = 0;
  uint32_t bpp = 0;
  switch (format) {
    case HAL_PIXEL_FORMAT_BGR_565:
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_RGBA_1010102:
    case HAL_PIXEL_FORMAT_RGBX_1010102:
      bpp = GetBppForUncompressedRGB(format);
      size = alignedw * alignedh * bpp;
      size += GetRgbUBwcMetaBufferSize(width, height, bpp);
      break;
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
      size = VENUS_BUFFER_SIZE(COLOR_FMT_NV12_UBWC, width, height);
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
      size = VENUS_BUFFER_SIZE(COLOR_FMT_NV12_BPP10_UBWC, width, height);
      break;
    default:
      ALOGE("%s: Unsupported pixel format: 0x%x", __FUNCTION__, format);
      break;
  }

  return size;
}

int Allocator::GetRgbDataAddress(private_handle_t *hnd, void **rgb_data) {
  int err = 0;

  // This api is for RGB* formats
  if (!gralloc1::IsUncompressedRGBFormat(hnd->format)) {
    return -EINVAL;
  }

  // linear buffer, nothing to do further
  if (!(hnd->flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED)) {
    *rgb_data = reinterpret_cast<void *>(hnd->base);
    return err;
  }

  unsigned int meta_size = 0;
  uint32_t bpp = GetBppForUncompressedRGB(hnd->format);
  switch (hnd->format) {
    case HAL_PIXEL_FORMAT_BGR_565:
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
      meta_size = GetRgbUBwcMetaBufferSize(hnd->width, hnd->height, bpp);
      break;
    default:
      ALOGE("%s:Unsupported RGB format: 0x%x", __FUNCTION__, hnd->format);
      err = -EINVAL;
      break;
  }
  *rgb_data = reinterpret_cast<void *>(hnd->base + meta_size);

  return err;
}

void Allocator::GetAlignedWidthAndHeight(const BufferDescriptor &descriptor, unsigned int *alignedw,
                                         unsigned int *alignedh) {
  int width = descriptor.GetWidth();
  int height = descriptor.GetHeight();
  int format = descriptor.GetFormat();
  gralloc1_producer_usage_t prod_usage = descriptor.GetProducerUsage();
  gralloc1_consumer_usage_t cons_usage = descriptor.GetConsumerUsage();

  // Currently surface padding is only computed for RGB* surfaces.
  bool ubwc_enabled = IsUBwcEnabled(format, prod_usage, cons_usage);
  int tile = ubwc_enabled || IsMacroTileEnabled(format, prod_usage, cons_usage);

  if (IsUncompressedRGBFormat(format)) {
    adreno_helper_->AlignUnCompressedRGB(width, height, format, tile, alignedw, alignedh);
    return;
  }

  if (ubwc_enabled) {
    GetYuvUBwcWidthAndHeight(width, height, format, alignedw, alignedh);
    return;
  }

  if (IsCompressedRGBFormat(format)) {
    adreno_helper_->AlignCompressedRGB(width, height, format, alignedw, alignedh);
    return;
  }

  int aligned_w = width;
  int aligned_h = height;
  unsigned int alignment = 32;

  // Below should be only YUV family
  switch (format) {
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
      alignment = adreno_helper_->GetGpuPixelAlignment();
      aligned_w = ALIGN(width, alignment);
      break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO:
      aligned_w = ALIGN(width, alignment);
      break;
    case HAL_PIXEL_FORMAT_RAW16:
      aligned_w = ALIGN(width, 16);
      break;
    case HAL_PIXEL_FORMAT_RAW10:
      aligned_w = ALIGN(width * 10 / 8, 16);
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
      aligned_w = ALIGN(width, 128);
      break;
    case HAL_PIXEL_FORMAT_YV12:
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
    case HAL_PIXEL_FORMAT_YCrCb_422_SP:
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
    case HAL_PIXEL_FORMAT_YCrCb_422_I:
      aligned_w = ALIGN(width, 16);
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
      aligned_w = INT(VENUS_Y_STRIDE(COLOR_FMT_NV12, width));
      aligned_h = INT(VENUS_Y_SCANLINES(COLOR_FMT_NV12, height));
      break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
      aligned_w = INT(VENUS_Y_STRIDE(COLOR_FMT_NV21, width));
      aligned_h = INT(VENUS_Y_SCANLINES(COLOR_FMT_NV21, height));
      break;
    case HAL_PIXEL_FORMAT_BLOB:
    case HAL_PIXEL_FORMAT_RAW_OPAQUE:
      break;
    case HAL_PIXEL_FORMAT_NV21_ZSL:
      aligned_w = ALIGN(width, 64);
      aligned_h = ALIGN(height, 64);
      break;
    default:
      break;
  }

  *alignedw = (unsigned int)aligned_w;
  *alignedh = (unsigned int)aligned_h;
}

}  // namespace gralloc1
