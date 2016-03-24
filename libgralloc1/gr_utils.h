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

#ifndef __GR_UTILS_H__
#define __GR_UTILS_H__

#include "gralloc_priv.h"

#define SZ_2M 0x200000
#define SZ_1M 0x100000
#define SZ_4K 0x1000

#define SIZE_4K 4096
#define SIZE_8K 4096

#define INT(exp) static_cast<int>(exp)
#define UINT(exp) static_cast<unsigned int>(exp)

namespace gralloc1 {

template <class Type1, class Type2>
inline Type1 ALIGN(Type1 x, Type2 align) {
  return (Type1)((x + (Type1)align - 1) & ~((Type1)align - 1));
}

bool IsCompressedRGBFormat(int format);
bool IsUncompressedRGBFormat(int format);
uint32_t GetBppForUncompressedRGB(int format);
bool CpuCanAccess(gralloc1_producer_usage_t prod_usage, gralloc1_consumer_usage_t cons_usage);
bool CpuCanRead(gralloc1_producer_usage_t prod_usage, gralloc1_consumer_usage_t cons_usage);
bool CpuCanWrite(gralloc1_producer_usage_t prod_usage);

}  // namespace gralloc1

#endif  // __GR_UTILS_H__
