/* Copyright (c) 2015-2016, The Linux Foundataion. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
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
*
*/

#ifndef __COLOR_PARAMS_H__
#define __COLOR_PARAMS_H__

#include <stdio.h>
#include <string.h>
#include <utils/locker.h>
#include <utils/constants.h>
#include <core/sdm_types.h>
#include <core/display_interface.h>
#include "hw_info_types.h"

namespace sdm {

// Bitmap Pending action to indicate to the caller what's pending to be taken care of.
enum PendingAction {
  kInvalidating = BITMAP(0),
  kApplySolidFill = BITMAP(1),
  kDisableSolidFill = BITMAP(2),
  kEnterQDCMMode = BITMAP(3),
  kExitQDCMMode = BITMAP(4),
  kSetPanelBrightness = BITMAP(5),
  kEnableFrameCapture = BITMAP(6),
  kDisableFrameCapture = BITMAP(7),
  kNoAction = BITMAP(31),
};

// ENUM to identify different Postprocessing feature block to program.
// Note: For each new entry added here, also need update hw_interface::GetPPFeaturesVersion<>
// AND HWPrimary::SetPPFeatures<>.
enum PPGlobalColorFeatureID {
  kGlobalColorFeaturePcc,
  kGlobalColorFeatureIgc,
  kGlobalColorFeaturePgc,
  kMixerColorFeatureGc,
  kGlobalColorFeaturePaV2,
  kGlobalColorFeatureDither,
  kGlobalColorFeatureGamut,
  kGlobalColorFeaturePADither,
  kMaxNumPPFeatures,
};

struct PPPendingParams {
  PendingAction action = kNoAction;
  void *params = NULL;
};

struct PPColorInfo {
  uint32_t r_bitdepth = 0;
  uint32_t r = 0;
  uint32_t g_bitdepth = 0;
  uint32_t g = 0;
  uint32_t b_bitdepth = 0;
  uint32_t b = 0;
};

struct PPColorFillParams {
  uint32_t flags = 0;
  struct {
    uint32_t width = 0;
    uint32_t height = 0;
    int32_t x = 0;
    int32_t y = 0;
  } rect;

  PPColorInfo color;
};

struct PPFeatureVersion {
  // SDE ASIC versioning its PP block at each specific feature level.
  static const uint32_t kSDEIgcV17 = 1;
  static const uint32_t kSDEPgcV17 = 5;
  static const uint32_t kSDEDitherV17 = 7;
  static const uint32_t kSDEGamutV17 = 9;
  static const uint32_t kSDEPaV17 = 11;
  static const uint32_t kSDEPccV17 = 13;
  static const uint32_t kSDEPADitherV17 = 15;
  static const uint32_t kSDELegacyPP = 17;

  uint32_t version[kMaxNumPPFeatures];
  PPFeatureVersion() { memset(version, 0, sizeof(version)); }
};

struct PPHWAttributes : HWResourceInfo, HWPanelInfo, DisplayConfigVariableInfo {
  char panel_name[256] = "generic_panel";
  PPFeatureVersion version;
  int panel_max_brightness = 0;

  void Set(const HWResourceInfo &hw_res, const HWPanelInfo &panel_info,
           const DisplayConfigVariableInfo &attr, const PPFeatureVersion &feature_ver);
};

struct PPDisplayAPIPayload {
  bool own_payload = false;  // to indicate if *payload is owned by this or just a reference.
  uint32_t size = 0;
  uint8_t *payload = NULL;

  PPDisplayAPIPayload() = default;
  PPDisplayAPIPayload(uint32_t size, uint8_t *param)
      : size(size), payload(param) {}

  template <typename T>
  DisplayError CreatePayload(T *&output) {
    DisplayError ret = kErrorNone;

    payload = new uint8_t[sizeof(T)]();
    if (!payload) {
      ret = kErrorMemory;
      output = NULL;
    } else {
      this->size = sizeof(T);
      output = reinterpret_cast<T *>(payload);
      own_payload = true;
    }
    return ret;
  }

  DisplayError CreatePayloadBytes(uint8_t *output, uint32_t size_in_bytes) {
    DisplayError ret = kErrorNone;

    payload = new uint8_t[size_in_bytes]();
    if (!payload) {
      ret = kErrorMemory;
      output = NULL;
    } else {
      this->size = size_in_bytes;
      output = payload;
      own_payload = true;
    }
    return ret;
  }

  inline void DestroyPayload() {
    if (payload && own_payload) {
      delete[] payload;
      payload = NULL;
      size = 0;
    } else {
      payload = NULL;
      size = 0;
    }
  }
};

struct PPRectInfo {
  uint32_t width;
  uint32_t height;
  int32_t x;
  int32_t y;
};

typedef enum {
  PP_PIXEL_FORMAT_NONE = 0,
  PP_PIXEL_FORMAT_RGB_888,
  PP_PIXEL_FORMAT_RGB_2101010,
  PP_PIXEL_FORMAT_MAX,
  PP_PIXEL_FORMAT_FORCE32BIT = 0x7FFFFFFF,
} PPPixelFormats;

struct PPFrameCaptureInputParams {
  PPRectInfo rect;
  PPPixelFormats out_pix_format;
  uint32_t flags;
};

struct PPFrameCaptureData {
  PPFrameCaptureInputParams input_params;
  uint8_t *buffer;
  uint32_t buffer_stride;
  uint32_t buffer_size;
};

struct SDEGamutCfg {
  static const int kGamutTableNum = 4;
  static const int kGamutScaleoffTableNum = 3;
  static const int kGamutTableSize = 1229;
  static const int kGamutTableCoarseSize = 32;
  static const int kGamutScaleoffSize = 16;
  uint32_t mode;
  uint32_t map_en;
  uint32_t tbl_size[kGamutTableNum];
  uint32_t *c0_data[kGamutTableNum];
  uint32_t *c1_c2_data[kGamutTableNum];
  uint32_t tbl_scale_off_sz[kGamutScaleoffTableNum];
  uint32_t *scale_off_data[kGamutScaleoffTableNum];
};

struct SDEPccCoeff {
  uint32_t c = 0;
  uint32_t r = 0;
  uint32_t g = 0;
  uint32_t b = 0;
  uint32_t rg = 0;
  uint32_t gb = 0;
  uint32_t rb = 0;
  uint32_t rgb = 0;
};

struct SDEPccCfg {
  SDEPccCoeff red;
  SDEPccCoeff green;
  SDEPccCoeff blue;

  static SDEPccCfg *Init(uint32_t arg __attribute__((__unused__)));
  SDEPccCfg *GetConfig() { return this; }
};

struct SDEDitherCfg {
  uint32_t g_y_depth;
  uint32_t r_cr_depth;
  uint32_t b_cb_depth;
  uint32_t length;
  uint32_t dither_matrix[16];
  uint32_t temporal_en;

  static SDEDitherCfg *Init(uint32_t arg __attribute__((__unused__)));
  SDEDitherCfg *GetConfig() { return this; }
};

struct SDEPADitherData {
  uint32_t data_flags;
  uint32_t matrix_size;
  uint64_t matrix_data_addr;
  uint32_t strength;
  uint32_t offset_en;
};

class SDEPADitherWrapper : private SDEPADitherData {
 public:
  static SDEPADitherWrapper *Init(uint32_t arg __attribute__((__unused__)));
  ~SDEPADitherWrapper() {
    if (buffer_)
      delete[] buffer_;
  }
  inline SDEPADitherData *GetConfig(void) { return this; }

 private:
  SDEPADitherWrapper() {}
  uint32_t *buffer_ = NULL;
};

struct SDEPaMemColorData {
  uint32_t adjust_p0 = 0;
  uint32_t adjust_p1 = 0;
  uint32_t adjust_p2 = 0;
  uint32_t blend_gain = 0;
  uint8_t sat_hold = 0;
  uint8_t val_hold = 0;
  uint32_t hue_region = 0;
  uint32_t sat_region = 0;
  uint32_t val_region = 0;
};

struct SDEPaData {
  static const int kSixZoneLUTSize = 384;
  uint32_t mode = 0;
  uint32_t hue_adj = 0;
  uint32_t sat_adj = 0;
  uint32_t val_adj = 0;
  uint32_t cont_adj;
  SDEPaMemColorData skin;
  SDEPaMemColorData sky;
  SDEPaMemColorData foliage;
  uint32_t six_zone_thresh = 0;
  uint32_t six_zone_adj_p0 = 0;
  uint32_t six_zone_adj_p1 = 0;
  uint8_t six_zone_sat_hold = 0;
  uint8_t six_zone_val_hold = 0;
  uint32_t six_zone_len = 0;
  uint32_t *six_zone_curve_p0 = NULL;
  uint32_t *six_zone_curve_p1 = NULL;
};

struct SDEIgcLUTData {
  static const int kMaxIgcLUTEntries = 256;
  uint32_t table_fmt = 0;
  uint32_t len = 0;
  uint32_t *c0_c1_data = NULL;
  uint32_t *c2_data = NULL;
};

struct SDEPgcLUTData {
  static const int kPgcLUTEntries = 1024;
  uint32_t len = 0;
  uint32_t *c0_data = NULL;
  uint32_t *c1_data = NULL;
  uint32_t *c2_data = NULL;
};

struct SDEDisplayMode {
  static const int kMaxModeNameSize = 256;
  int32_t id = -1;
  uint32_t type = 0;
  char name[kMaxModeNameSize] = {0};
};

// Wrapper on HW block config data structure to encapsulate the details of allocating
// and destroying from the caller.
class SDEGamutCfgWrapper : private SDEGamutCfg {
 public:
  enum GamutMode {
    GAMUT_FINE_MODE = 0x01,
    GAMUT_COARSE_MODE,
  };

  // This factory method will be used by libsdm-color.so data producer to be populated with
  // converted config values for SDE feature blocks.
  static SDEGamutCfgWrapper *Init(uint32_t arg);

  // Data consumer<Commit thread> will be responsible to destroy it once the feature is commited.
  ~SDEGamutCfgWrapper() {
    if (buffer_)
      delete[] buffer_;
  }

  // Data consumer will use this method to retrieve contained feature configuration.
  inline SDEGamutCfg *GetConfig(void) { return this; }

 private:
  SDEGamutCfgWrapper() {}
  uint32_t *buffer_ = NULL;
};

class SDEPaCfgWrapper : private SDEPaData {
 public:
  static SDEPaCfgWrapper *Init(uint32_t arg = 0);
  ~SDEPaCfgWrapper() {
    if (buffer_)
      delete[] buffer_;
  }
  inline SDEPaData *GetConfig(void) { return this; }

 private:
  SDEPaCfgWrapper() {}
  uint32_t *buffer_ = NULL;
};

class SDEIgcLUTWrapper : private SDEIgcLUTData {
 public:
  static SDEIgcLUTWrapper *Init(uint32_t arg __attribute__((__unused__)));
  ~SDEIgcLUTWrapper() {
    if (buffer_)
      delete[] buffer_;
  }
  inline SDEIgcLUTData *GetConfig(void) { return this; }

 private:
  SDEIgcLUTWrapper() {}
  uint32_t *buffer_ = NULL;
};

class SDEPgcLUTWrapper : private SDEPgcLUTData {
 public:
  static SDEPgcLUTWrapper *Init(uint32_t arg __attribute__((__unused__)));
  ~SDEPgcLUTWrapper() {
    if (buffer_)
      delete[] buffer_;
  }
  inline SDEPgcLUTData *GetConfig(void) { return this; }

 private:
  SDEPgcLUTWrapper() {}
  uint32_t *buffer_ = NULL;
};

// Base Postprocessing features information.
class PPFeatureInfo {
 public:
  uint32_t enable_flags_ = 0;  // bitmap to indicate subset of parameters enabling or not.
  uint32_t feature_version_ = 0;
  uint32_t feature_id_ = 0;
  uint32_t disp_id_ = 0;
  uint32_t pipe_id_ = 0;

  virtual ~PPFeatureInfo() {}
  virtual void *GetConfigData(void) const = 0;
};

// Individual Postprocessing feature representing physical attributes and information
// This template class wrapping around abstract data type representing different
// post-processing features. It will take output from ColorManager converting from raw metadata.
// The configuration will directly pass into HWInterface to program the hardware accordingly.
template <typename T>
class TPPFeatureInfo : public PPFeatureInfo {
 public:
  virtual ~TPPFeatureInfo() {
    if (params_)
      delete params_;
  }

  // API for data consumer to get underlying data configs to program into pp hardware block.
  virtual void *GetConfigData(void) const { return params_->GetConfig(); }

  // API for data producer to get access to underlying data configs to populate it.
  T *GetParamsReference(void) { return params_; }

  // API for create this template object.
  static TPPFeatureInfo *Init(uint32_t arg = 0) {
    TPPFeatureInfo *info = new TPPFeatureInfo();
    if (info) {
      info->params_ = T::Init(arg);
      if (!info->params_) {
        delete info;
        info = NULL;
      }
    }

    return info;
  }

 protected:
  TPPFeatureInfo() = default;

 private:
  T *params_ = NULL;
};

// This singleton class serves as data exchanging central between data producer
// <libsdm-color.so> and data consumer<SDM and HWC.>
// This class defines PP pending features to be programmed, which generated from
// ColorManager. Dirty flag indicates some features are available to be programmed.
// () Lock is needed since the object wil be accessed from 2 tasks.
// All API exposed are not threadsafe, it's caller's responsiblity to acquire the locker.
class PPFeaturesConfig {
 public:
  PPFeaturesConfig() { memset(feature_, 0, sizeof(feature_)); }
  ~PPFeaturesConfig() { Reset(); }

  // ColorManager installs one TFeatureInfo<T> to take the output configs computed
  // from ColorManager, containing all physical features to be programmed and also compute
  // metadata/populate into T.
  inline DisplayError AddFeature(uint32_t feature_id, PPFeatureInfo *feature) {
    if (feature_id < kMaxNumPPFeatures)
      feature_[feature_id] = feature;

    return kErrorNone;
  }

  inline Locker &GetLocker(void) { return locker_; }
  inline PPFrameCaptureData *GetFrameCaptureData(void) { return &frame_capture_data; }
  // Once all features are consumed, destroy/release all TFeatureInfo<T> on the list,
  // then clear dirty_ flag and return the lock to the TFeatureInfo<T> producer.
  void Reset();

  // Consumer to call this to retrieve all the TFeatureInfo<T> on the list to be programmed.
  DisplayError RetrieveNextFeature(PPFeatureInfo **feature);

  inline bool IsDirty() { return dirty_; }
  inline void MarkAsDirty() { dirty_ = true; }

 private:
  bool dirty_ = 0;
  Locker locker_;
  PPFeatureInfo *feature_[kMaxNumPPFeatures];  // reference to TFeatureInfo<T>.
  uint32_t next_idx_ = 0;
  PPFrameCaptureData frame_capture_data;
};

}  // namespace sdm

#endif  // __COLOR_PARAMS_H__
