/*
* Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#include <utils/formats.h>

#define __CLASS__ "FormatsUtils"

namespace sdm {

bool IsUBWCFormat(LayerBufferFormat format) {
  switch (format) {
  case kFormatRGBA8888Ubwc:
  case kFormatRGBX8888Ubwc:
  case kFormatBGR565Ubwc:
  case kFormatYCbCr420SPVenusUbwc:
  case kFormatRGBA1010102Ubwc:
  case kFormatRGBX1010102Ubwc:
  case kFormatYCbCr420TP10Ubwc:
    return true;
  default:
    return false;
  }
}

bool Is10BitFormat(LayerBufferFormat format) {
  switch (format) {
  case kFormatRGBA1010102:
  case kFormatARGB2101010:
  case kFormatRGBX1010102:
  case kFormatXRGB2101010:
  case kFormatBGRA1010102:
  case kFormatABGR2101010:
  case kFormatBGRX1010102:
  case kFormatXBGR2101010:
  case kFormatRGBA1010102Ubwc:
  case kFormatRGBX1010102Ubwc:
  case kFormatYCbCr420P010:
  case kFormatYCbCr420TP10Ubwc:
    return true;
  default:
    return false;
  }
}

}  // namespace sdm

