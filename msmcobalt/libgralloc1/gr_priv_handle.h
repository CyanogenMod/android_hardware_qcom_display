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

#ifndef __GR_PRIV_HANDLE_H__
#define __GR_PRIV_HANDLE_H__

#include <cutils/log.h>
#include <hardware/gralloc1.h>

#define GRALLOC1_FUNCTION_PERFORM 0x00001000

#define DBG_HANDLE false

typedef gralloc1_error_t (*GRALLOC1_PFN_PERFORM)(gralloc1_device_t *device, int operation, ...);

typedef int BackStoreFd;

#define PRIV_HANDLE_CONST(exp) static_cast<const private_handle_t *>(exp)

struct private_handle_t : public native_handle_t {
  // TODO(user): Moving PRIV_FLAGS to #defs & check for each PRIV_FLAG and remove unused.
  enum {
    PRIV_FLAGS_FRAMEBUFFER = 0x00000001,
    PRIV_FLAGS_USES_ION = 0x00000008,
    PRIV_FLAGS_USES_ASHMEM = 0x00000010,
    PRIV_FLAGS_NEEDS_FLUSH = 0x00000020,
    PRIV_FLAGS_INTERNAL_ONLY = 0x00000040,
    PRIV_FLAGS_NON_CPU_WRITER = 0x00000080,
    PRIV_FLAGS_NONCONTIGUOUS_MEM = 0x00000100,
    PRIV_FLAGS_CACHED = 0x00000200,
    PRIV_FLAGS_SECURE_BUFFER = 0x00000400,
    PRIV_FLAGS_EXTERNAL_ONLY = 0x00002000,
    PRIV_FLAGS_PROTECTED_BUFFER = 0x00004000,
    PRIV_FLAGS_VIDEO_ENCODER = 0x00010000,
    PRIV_FLAGS_CAMERA_WRITE = 0x00020000,
    PRIV_FLAGS_CAMERA_READ = 0x00040000,
    PRIV_FLAGS_HW_COMPOSER = 0x00080000,
    PRIV_FLAGS_HW_TEXTURE = 0x00100000,
    PRIV_FLAGS_ITU_R_601 = 0x00200000,     // Unused from display
    PRIV_FLAGS_ITU_R_601_FR = 0x00400000,  // Unused from display
    PRIV_FLAGS_ITU_R_709 = 0x00800000,     // Unused from display
    PRIV_FLAGS_SECURE_DISPLAY = 0x01000000,
    PRIV_FLAGS_TILE_RENDERED = 0x02000000,
    PRIV_FLAGS_CPU_RENDERED = 0x04000000,
    PRIV_FLAGS_UBWC_ALIGNED = 0x08000000,
    PRIV_FLAGS_DISP_CONSUMER = 0x10000000
  };

  // file-descriptors
  int fd;
  int fd_metadata;

  // ints
  int magic;
  int flags;
  unsigned int size;
  unsigned int offset;
  int buffer_type;
  uint64_t base __attribute__((aligned(8)));
  unsigned int offset_metadata;

  // The gpu address mapped into the mmu.
  uint64_t gpuaddr __attribute__((aligned(8)));

  int format;
  int width;   // holds width of the actual buffer allocated
  int height;  // holds height of the  actual buffer allocated

  int stride;
  uint64_t base_metadata __attribute__((aligned(8)));

  // added for gralloc1
  int unaligned_width;   // holds width client asked to allocate
  int unaligned_height;  // holds height client asked to allocate
  gralloc1_producer_usage_t producer_usage __attribute__((aligned(8)));
  gralloc1_consumer_usage_t consumer_usage __attribute__((aligned(8)));

  static const int kNumFds = 2;
  static const int kMagic = 'gmsm';

  static inline int NumInts() {
    return ((sizeof(private_handle_t) - sizeof(native_handle_t)) / sizeof(int)) - kNumFds;
  }

  private_handle_t(int fd, unsigned int size, int flags, int buf_type, int format, int width,
                   int height)
      : fd(fd),
        fd_metadata(-1),
        magic(kMagic),
        flags(flags),
        size(size),
        offset(0),
        buffer_type(buf_type),
        base(0),
        offset_metadata(0),
        gpuaddr(0),
        format(format),
        width(width),
        height(height),
        base_metadata(0),
        unaligned_width(width),
        unaligned_height(height),
        producer_usage(GRALLOC1_PRODUCER_USAGE_NONE),
        consumer_usage(GRALLOC1_CONSUMER_USAGE_NONE) {
    version = static_cast<int>(sizeof(native_handle));
    numInts = NumInts();
    numFds = kNumFds;
  }

  private_handle_t(int fd, unsigned int size, int flags, int buf_type, int format, int width,
                   int height, int meta_fd, unsigned int meta_offset, uint64_t meta_base)
      : private_handle_t(fd, size, flags, buf_type, format, width, height) {
    fd_metadata = meta_fd;
    offset_metadata = meta_offset;
    base_metadata = meta_base;
  }

  private_handle_t(int fd, unsigned int size, int flags, int buf_type, int format, int width,
                   int height, int meta_fd, unsigned int meta_offset, uint64_t meta_base,
                   int unaligned_w , int unaligned_h,
                   gralloc1_producer_usage_t prod_usage, gralloc1_consumer_usage_t cons_usage)
      : private_handle_t(fd, size, flags, buf_type, format, width, height, meta_fd, meta_offset
                         meta_base) {
    unaligned_width = unaligned_w;
    unaligned_height = unaligned_h;
    producer_usage = prod_usage;
    consumer_usage = cons_usage;
  }

  ~private_handle_t() {
    magic = 0;
    ALOGE_IF(DBG_HANDLE, "deleting buffer handle %p", this);
  }

  static int validate(const native_handle *h) {
    const private_handle_t *hnd = (const private_handle_t *)h;
    if (!h || h->version != sizeof(native_handle) || h->numInts != NumInts() ||
        h->numFds != kNumFds || hnd->magic != kMagic) {
      ALOGE(
          "Invalid gralloc handle (at %p): ver(%d/%zu) ints(%d/%d) fds(%d/%d) "
          "magic(%c%c%c%c/%c%c%c%c)",
          h, h ? h->version : -1, sizeof(native_handle), h ? h->numInts : -1, NumInts(),
          h ? h->numFds : -1, kNumFds,
          hnd ? (((hnd->magic >> 24) & 0xFF) ? ((hnd->magic >> 24) & 0xFF) : '-') : '?',
          hnd ? (((hnd->magic >> 16) & 0xFF) ? ((hnd->magic >> 16) & 0xFF) : '-') : '?',
          hnd ? (((hnd->magic >> 8) & 0xFF) ? ((hnd->magic >> 8) & 0xFF) : '-') : '?',
          hnd ? (((hnd->magic >> 0) & 0xFF) ? ((hnd->magic >> 0) & 0xFF) : '-') : '?',
          (kMagic >> 24) & 0xFF, (kMagic >> 16) & 0xFF, (kMagic >> 8) & 0xFF, (kMagic >> 0) & 0xFF);
      return -EINVAL;
    }

    return 0;
  }

  int GetUnalignedWidth() const { return unaligned_width; }

  int GetUnalignedHeight() const { return unaligned_height; }

  int GetColorFormat() const { return format; }

  int GetStride() const {
    // In handle we are storing aligned width after allocation.
    // Why GetWidth & GetStride?? Are we supposed to maintain unaligned values??
    return width;
  }

  gralloc1_consumer_usage_t GetConsumerUsage() const { return consumer_usage; }

  gralloc1_producer_usage_t GetProducerUsage() const { return producer_usage; }

  BackStoreFd GetBackingstore() const { return fd; }
};

#endif  // __GR_PRIV_HANDLE_H__
