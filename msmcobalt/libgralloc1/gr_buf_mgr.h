/*
 * Copyright (c) 2011-2016, The Linux Foundation. All rights reserved.
 * Not a Contribution
 *
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef __GR_BUF_MGR_H__
#define __GR_BUF_MGR_H__

#include <pthread.h>
#include <unordered_map>
#include <mutex>

#include "gralloc_priv.h"
#include "gr_allocator.h"

namespace gralloc1 {

class BufferManager {
 public:
  BufferManager();
  ~BufferManager();
  bool Init();
  gralloc1_error_t AllocateBuffers(uint32_t numDescriptors, const BufferDescriptor *descriptors,
                                   buffer_handle_t *outBuffers);
  gralloc1_error_t RetainBuffer(private_handle_t const *hnd);
  gralloc1_error_t ReleaseBuffer(private_handle_t const *hnd);
  gralloc1_error_t LockBuffer(const private_handle_t *hnd, gralloc1_producer_usage_t prod_usage,
                              gralloc1_consumer_usage_t cons_usage);
  gralloc1_error_t UnlockBuffer(const private_handle_t *hnd);
  gralloc1_error_t Perform(int operation, va_list args);

 private:
  gralloc1_error_t MapBuffer(private_handle_t const *hnd);
  gralloc1_error_t FreeBuffer(private_handle_t const *hnd);
  int GetBufferType(int format);
  int AllocateBuffer(const BufferDescriptor &descriptor, buffer_handle_t *handle,
                     unsigned int bufferSize = 0);
  int AllocateBuffer(unsigned int size, int aligned_w, int aligned_h, int unaligned_w,
                     int unaligned_h, int format, int bufferType,
                     gralloc1_producer_usage_t prod_usage, gralloc1_consumer_usage_t cons_usage,
                     buffer_handle_t *handle);
  int GetDataAlignment(int format, gralloc1_producer_usage_t prod_usage,
                       gralloc1_consumer_usage_t cons_usage);
  int GetHandleFlags(int format, gralloc1_producer_usage_t prod_usage,
                     gralloc1_consumer_usage_t cons_usage);
  void CreateSharedHandle(buffer_handle_t inbuffer, const BufferDescriptor &descriptor,
                          buffer_handle_t *out_buffer);

  bool map_fb_mem_ = false;
  bool ubwc_for_fb_ = false;
  Allocator *allocator_ = NULL;
  std::mutex locker_;
  std::unordered_map<private_handle_t const *, int> handles_map_ = {};
};

}  // namespace gralloc1

#endif  // __GR_BUF_MGR_H__
