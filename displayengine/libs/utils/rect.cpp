/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
*
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

#include <math.h>
#include <utils/rect.h>
#include <utils/constants.h>

#define __CLASS__ "RectUtils"

namespace sde {

bool IsValidRect(const LayerRect &rect) {
  return ((rect.bottom > rect.top) && (rect.right > rect.left));
}


LayerRect GetIntersection(const LayerRect &rect1, const LayerRect &rect2) {
  LayerRect res;

  if (!IsValidRect(rect1) || !IsValidRect(rect2)) {
    return LayerRect();
  }

  res.left = MAX(rect1.left, rect2.left);
  res.top = MAX(rect1.top, rect2.top);
  res.right = MIN(rect1.right, rect2.right);
  res.bottom = MIN(rect1.bottom, rect2.bottom);

  if (!IsValidRect(res)) {
    return LayerRect();
  }

  return res;
}

void LogRect(DebugTag debug_tag, const char *prefix, const LayerRect &roi) {
  DLOGV_IF(debug_tag, "%s: left = %.0f, top = %.0f, right = %.0f, bottom = %.0f",
           prefix, roi.left, roi.top, roi.right, roi.bottom);
}

void NormalizeRect(const uint32_t &factor, LayerRect *rect) {
  uint32_t left = UINT32(ceilf(rect->left));
  uint32_t top = UINT32(ceilf(rect->top));
  uint32_t right = UINT32(floorf(rect->right));
  uint32_t bottom = UINT32(floorf(rect->bottom));

  rect->left = FLOAT(CeilToMultipleOf(left, factor));
  rect->top = FLOAT(CeilToMultipleOf(top, factor));
  rect->right = FLOAT(FloorToMultipleOf(right, factor));
  rect->bottom = FLOAT(FloorToMultipleOf(bottom, factor));
}

}  // namespace sde

