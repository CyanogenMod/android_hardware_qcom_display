/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*  * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
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

#include <gralloc_priv.h>
#include <memalloc.h>
#include <gr.h>
#include <alloc_controller.h>
#include <utils/constants.h>
#include <core/buffer_allocator.h>

#include "hwc_debugger.h"
#include "hwc_buffer_allocator.h"

#define __CLASS__ "HWCBufferAllocator"

namespace sde {

HWCBufferAllocator::HWCBufferAllocator() {
  alloc_controller_ = gralloc::IAllocController::getInstance();
}

DisplayError HWCBufferAllocator::AllocateBuffer(BufferInfo *buffer_info) {
  gralloc::alloc_data data;

  const BufferConfig &buffer_config = buffer_info->buffer_config;
  AllocatedBufferInfo *alloc_buffer_info = &buffer_info->alloc_buffer_info;
  MetaBufferInfo *meta_buffer_info = new MetaBufferInfo();

  if (!meta_buffer_info) {
    return kErrorMemory;
  }

  int alloc_flags = GRALLOC_USAGE_PRIVATE_IOMMU_HEAP;
  int error = 0;

  int width = INT(buffer_config.width);
  int height = INT(buffer_config.height);
  int format;

  error = SetHALFormat(buffer_config.format, &format);
  if (error != 0) {
    return kErrorParameters;
  }

  if (buffer_config.secure) {
    alloc_flags = GRALLOC_USAGE_PRIVATE_MM_HEAP;
    alloc_flags |= GRALLOC_USAGE_PROTECTED;
    data.align = kSecureBufferAlignment;
  } else {
    data.align = getpagesize();
  }

  if (buffer_config.cache == false) {
    // Allocate uncached buffers
    alloc_flags |= GRALLOC_USAGE_PRIVATE_UNCACHED;
  }

  int aligned_width = 0, aligned_height = 0;
  uint32_t buffer_size = getBufferSizeAndDimensions(width, height, format, alloc_flags,
                                                    aligned_width, aligned_height);

  buffer_size = ROUND_UP((buffer_size * buffer_config.buffer_count), data.align);

  data.base = 0;
  data.fd = -1;
  data.offset = 0;
  data.size = buffer_size;
  data.uncached = !buffer_config.cache;

  error = alloc_controller_->allocate(data, alloc_flags);
  if (error != 0) {
    DLOGE("Error allocating memory size %d uncached %d", data.size, data.uncached);
    return kErrorMemory;
  }

  alloc_buffer_info->fd = data.fd;
  alloc_buffer_info->stride = aligned_width;
  alloc_buffer_info->size = buffer_size;

  meta_buffer_info->base_addr = data.base;
  meta_buffer_info->alloc_type = data.allocType;

  buffer_info->private_data = meta_buffer_info;

  return kErrorNone;
}

DisplayError HWCBufferAllocator::FreeBuffer(BufferInfo *buffer_info) {
  int ret = 0;

  AllocatedBufferInfo *alloc_buffer_info = &buffer_info->alloc_buffer_info;
  MetaBufferInfo *meta_buffer_info = static_cast<MetaBufferInfo *> (buffer_info->private_data);
  if ((alloc_buffer_info->fd < 0) || (meta_buffer_info->base_addr == NULL)) {
    return kErrorNone;
  }

  gralloc::IMemAlloc *memalloc = alloc_controller_->getAllocator(meta_buffer_info->alloc_type);
  if (memalloc == NULL) {
    DLOGE("Memalloc handle is NULL, alloc type %d", meta_buffer_info->alloc_type);
    return kErrorResources;
  }

  ret = memalloc->free_buffer(meta_buffer_info->base_addr, alloc_buffer_info->size, 0,
                              alloc_buffer_info->fd);
  if (ret != 0) {
    DLOGE("Error freeing buffer base_addr %p size %d fd %d", meta_buffer_info->base_addr,
          alloc_buffer_info->size, alloc_buffer_info->fd);
    return kErrorMemory;
  }

  alloc_buffer_info->fd = -1;
  alloc_buffer_info->stride = 0;
  alloc_buffer_info->size = 0;

  meta_buffer_info->base_addr = NULL;
  meta_buffer_info->alloc_type = 0;

  delete meta_buffer_info;
  meta_buffer_info = NULL;

  return kErrorNone;
}

int HWCBufferAllocator::SetHALFormat(LayerBufferFormat format, int *target) {
  switch (format) {
  case kFormatRGBA8888:                 *target = HAL_PIXEL_FORMAT_RGBA_8888;             break;
  case kFormatRGBX8888:                 *target = HAL_PIXEL_FORMAT_RGBX_8888;             break;
  case kFormatRGB888:                   *target = HAL_PIXEL_FORMAT_RGB_888;               break;
  case kFormatRGB565:                   *target = HAL_PIXEL_FORMAT_RGB_565;               break;
  case kFormatBGRA8888:                 *target = HAL_PIXEL_FORMAT_BGRA_8888;             break;
  case kFormatYCrCb420Planar:           *target = HAL_PIXEL_FORMAT_YV12;                  break;
  case kFormatYCrCb420SemiPlanar:       *target = HAL_PIXEL_FORMAT_YCrCb_420_SP;          break;
  case kFormatYCbCr420SemiPlanar:       *target = HAL_PIXEL_FORMAT_YCbCr_420_SP;          break;
  case kFormatYCbCr422Packed:           *target = HAL_PIXEL_FORMAT_YCbCr_422_I;           break;
  case kFormatYCbCr420SemiPlanarVenus:  *target = HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS;    break;

  default:
    DLOGE("Unsupported format = 0x%x", format);
    return -EINVAL;
  }

  return 0;
}

}  // namespace sde
