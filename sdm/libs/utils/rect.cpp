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

namespace sdm {

bool IsValid(const LayerRect &rect) {
  return ((rect.bottom > rect.top) && (rect.right > rect.left));
}

bool IsCongruent(const LayerRect &rect1, const LayerRect &rect2) {
  return ((rect1.left == rect2.left) &&
          (rect1.top == rect2.top) &&
          (rect1.right == rect2.right) &&
          (rect1.bottom == rect2.bottom));
}

void Log(DebugTag debug_tag, const char *prefix, const LayerRect &roi) {
  DLOGV_IF(debug_tag, "%s: left = %.0f, top = %.0f, right = %.0f, bottom = %.0f",
           prefix, roi.left, roi.top, roi.right, roi.bottom);
}

void Normalize(const uint32_t &align_x, const uint32_t &align_y, LayerRect *rect) {
    rect->left = ROUND_UP_ALIGN_UP(rect->left, align_x);
    rect->right = ROUND_UP_ALIGN_DOWN(rect->right, align_x);
    rect->top = ROUND_UP_ALIGN_UP(rect->top, align_y);
    rect->bottom = ROUND_UP_ALIGN_DOWN(rect->bottom, align_y);
}

LayerRect Intersection(const LayerRect &rect1, const LayerRect &rect2) {
  LayerRect res;

  if (!IsValid(rect1) || !IsValid(rect2)) {
    return LayerRect();
  }

  res.left = MAX(rect1.left, rect2.left);
  res.top = MAX(rect1.top, rect2.top);
  res.right = MIN(rect1.right, rect2.right);
  res.bottom = MIN(rect1.bottom, rect2.bottom);

  if (!IsValid(res)) {
    return LayerRect();
  }

  return res;
}

LayerRect Reposition(const LayerRect &rect, const int &x_offset, const int &y_offset) {
  LayerRect res;

  if (!IsValid(rect)) {
    return LayerRect();
  }

  res.left = rect.left + FLOAT(x_offset);
  res.top = rect.top + FLOAT(y_offset);
  res.right = rect.right + FLOAT(x_offset);
  res.bottom = rect.bottom + FLOAT(y_offset);

  return res;
}

// Not a geometrical rect deduction. Deducts rect2 from rect1 only if it results a single rect
LayerRect Subtract(const LayerRect &rect1, const LayerRect &rect2) {
  LayerRect res;

  res = rect1;

  if ((rect1.left == rect2.left) && (rect1.right == rect2.right)) {
    if ((rect1.top == rect2.top) && (rect2.bottom <= rect1.bottom)) {
      res.top = rect2.bottom;
    } else if ((rect1.bottom == rect2.bottom) && (rect2.top >= rect1.top)) {
      res.bottom = rect2.top;
    }
  } else if ((rect1.top == rect2.top) && (rect1.bottom == rect2.bottom)) {
    if ((rect1.left == rect2.left) && (rect2.right <= rect1.right)) {
      res.left = rect2.right;
    } else if ((rect1.right == rect2.right) && (rect2.left >= rect1.left)) {
      res.right = rect2.left;
    }
  }

  return res;
}

LayerRect Union(const LayerRect &rect1, const LayerRect &rect2) {
  LayerRect res;

  if (!IsValid(rect1) && !IsValid(rect2)) {
    return LayerRect();
  }

  if (!IsValid(rect1)) {
    return rect2;
  }

  if (!IsValid(rect2)) {
    return rect1;
  }

  res.left = MIN(rect1.left, rect2.left);
  res.top = MIN(rect1.top, rect2.top);
  res.right = MAX(rect1.right, rect2.right);
  res.bottom = MAX(rect1.bottom, rect2.bottom);

  return res;
}

}  // namespace sdm

