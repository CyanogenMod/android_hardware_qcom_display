/* Copyright (c) 2015, The Linux Foundataion. All rights reserved.
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
  kNoAction = BITMAP(31),
};

// ENUM to identify different Postprocessing feature block to program.
// Note: For each new entry added here, also need update hw_interface::GetPPFeaturesVersion<>
// AND HWPrimary::SetPPFeatures<>.
enum PPGlobalColorFeatureID {
  kGlobalColorFeaturePcc,
  kGlobalColorFeatureIgc,
  kGlobalColorFeaturePgc,
  kGlobalColorFeaturePaV2,
  kGlobalColorFeatureDither,
  kGlobalColorFeatureGamut,
  kMaxNumPPFeatures,
};

struct PPPendingParams {
  PendingAction action;
  void *params;
  PPPendingParams() : action(kNoAction), params(NULL) {}
};

struct PPFeatureVersion {
  // SDE ASIC versioning its PP block at each specific feature level.
  static const uint32_t kSDEIgcV17 = 1;
  static const uint32_t kSDEPgcV17 = 5;
  static const uint32_t kSDEDitherV17 = 7;
  static const uint32_t kSDEGamutV17 = 9;
  static const uint32_t kSDEPaV17 = 11;
  static const uint32_t kSDEPccV17 = 13;
  static const uint32_t kSDELegacyPP = 15;

  uint32_t version[kMaxNumPPFeatures];
  PPFeatureVersion() { memset(version, 0, sizeof(version)); }
};

struct PPHWAttributes : HWResourceInfo, HWPanelInfo, DisplayConfigVariableInfo {
  const char *panel_name;  // TODO:  Add into HWPanelInfo to retrieve panel_name from HW.
  PPFeatureVersion version;

  PPHWAttributes() : panel_name("generic_panel"), version() {}
  inline void Set(const HWResourceInfo &hw_res, const HWPanelInfo &panel_info,
                  const DisplayConfigVariableInfo &attr, const PPFeatureVersion &feature_ver) {
    HWResourceInfo &res = *this;
    res = hw_res;
    HWPanelInfo &panel = *this;
    panel = panel_info;
    DisplayConfigVariableInfo &attributes = *this;
    attributes = attr;
    version = feature_ver;
  }
};

struct PPDisplayAPIPayload {
  bool own_payload;  // to indicate if *payload is owned by this or just a reference.
  uint32_t size;
  uint8_t *payload;

  PPDisplayAPIPayload() : own_payload(false), size(0), payload(NULL) {}
  PPDisplayAPIPayload(uint32_t size, uint8_t *param)
      : own_payload(false), size(size), payload(param) {}

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
  uint32_t c;
  uint32_t r;
  uint32_t g;
  uint32_t b;
  uint32_t rg;
  uint32_t gb;
  uint32_t rb;
  uint32_t rgb;

  SDEPccCoeff() : c(0), r(0), g(0), b(0), rg(0), gb(0), rb(0), rgb(0) {}
};

struct SDEPccCfg {
  SDEPccCoeff red;
  SDEPccCoeff green;
  SDEPccCoeff blue;
  SDEPccCfg() : red(), green(), blue() {}

  static SDEPccCfg *Init(uint32_t arg __attribute__((__unused__)));
  SDEPccCfg *GetConfig() { return this; }
};

struct SDEPaMemColorData {
  uint32_t adjust_p0;
  uint32_t adjust_p1;
  uint32_t adjust_p2;
  uint32_t blend_gain;
  uint8_t sat_hold;
  uint8_t val_hold;
  uint32_t hue_region;
  uint32_t sat_region;
  uint32_t val_region;
};

struct SDEPaData {
  static const int kSixZoneLUTSize = 384;
  uint32_t mode;
  uint32_t hue_adj;
  uint32_t sat_adj;
  uint32_t val_adj;
  uint32_t cont_adj;
  SDEPaMemColorData skin;
  SDEPaMemColorData sky;
  SDEPaMemColorData foliage;
  uint32_t six_zone_thresh;
  uint32_t six_zone_adj_p0;
  uint32_t six_zone_adj_p1;
  uint8_t six_zone_sat_hold;
  uint8_t six_zone_val_hold;
  uint32_t six_zone_len;
  uint32_t *six_zone_curve_p0;
  uint32_t *six_zone_curve_p1;
};

struct SDEIgcLUTData {
  static const int kMaxIgcLUTEntries = 256;
  uint32_t table_fmt;
  uint32_t len;
  uint32_t *c0_c1_data;
  uint32_t *c2_data;

  SDEIgcLUTData() : table_fmt(0), len(0), c0_c1_data(NULL), c2_data(NULL) {}
};

struct SDEPgcLUTData {
  static const int kPgcLUTEntries = 1024;
  uint32_t len;
  uint32_t *c0_data;
  uint32_t *c1_data;
  uint32_t *c2_data;

  SDEPgcLUTData() : len(0), c0_data(NULL), c1_data(NULL), c2_data(NULL) {}
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
  SDEGamutCfgWrapper() : buffer_(NULL) {}
  uint32_t *buffer_;
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
  SDEPaCfgWrapper() : buffer_(NULL) {}
  uint32_t *buffer_;
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
  SDEIgcLUTWrapper() : buffer_(NULL) {}
  uint32_t *buffer_;
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
  SDEPgcLUTWrapper() : buffer_(NULL) {}
  uint32_t *buffer_;
};

// Base Postprocessing features information.
class PPFeatureInfo {
 public:
  uint32_t enable_flags_;  // bitmap to indicate subset of parameters enabling or not.
  uint32_t feature_version_;
  uint32_t feature_id_;
  uint32_t disp_id_;
  uint32_t pipe_id_;

  virtual ~PPFeatureInfo() {}
  virtual void *GetConfigData(void) const = 0;

  PPFeatureInfo()
      : enable_flags_(0), feature_version_(0), feature_id_(0), disp_id_(0), pipe_id_(0) {}
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
  TPPFeatureInfo() : params_(NULL) {}

 private:
  T *params_;
};

// This singleton class serves as data exchanging central between data producer
// <libsdm-color.so> and data consumer<SDM and HWC.>
// This class defines PP pending features to be programmed, which generated from
// ColorManager. Dirty flag indicates some features are available to be programmed.
// () Lock is needed since the object wil be accessed from 2 tasks.
// All API exposed are not threadsafe, it's caller's responsiblity to acquire the locker.
class PPFeaturesConfig {
 public:
  PPFeaturesConfig() : dirty_(0), next_idx_(0) { memset(feature_, 0, sizeof(feature_)); }
  ~PPFeaturesConfig() { Reset(); }

  // ColorManager installs one TFeatureInfo<T> to take the output configs computed
  // from ColorManager, containing all physical features to be programmed and also compute
  // metadata/populate into T.
  inline DisplayError AddFeature(uint32_t feature_id, PPFeatureInfo &feature) {
    if (feature_id < kMaxNumPPFeatures)
      feature_[feature_id] = &feature;

    return kErrorNone;
  }

  inline Locker &GetLocker(void) { return locker_; }

  // Once all features are consumed, destroy/release all TFeatureInfo<T> on the list,
  // then clear dirty_ flag and return the lock to the TFeatureInfo<T> producer.
  inline void Reset() {
    for (int i = 0; i < kMaxNumPPFeatures; i++) {
      if (feature_[i]) {
        delete feature_[i];
        feature_[i] = NULL;
      }
    }
    dirty_ = false;
    next_idx_ = 0;
  }

  // Consumer to call this to retrieve all the TFeatureInfo<T> on the list to be programmed.
  inline DisplayError RetrieveNextFeature(PPFeatureInfo **feature) {
    DisplayError ret = kErrorNone;
    int i(0);

    for (i = next_idx_; i < kMaxNumPPFeatures; i++) {
      if (feature_[i]) {
        *feature = feature_[i];
        next_idx_ = i + 1;
        break;
      }
    }
    if (i == kMaxNumPPFeatures) {
      ret = kErrorParameters;
      next_idx_ = 0;
    }

    return ret;
  }

  inline bool IsDirty() { return dirty_; }
  inline void MarkAsDirty() { dirty_ = true; }

 private:
  bool dirty_;
  Locker locker_;
  PPFeatureInfo *feature_[kMaxNumPPFeatures];  // reference to TFeatureInfo<T>.
  uint32_t next_idx_;
};

}  // namespace sdm

#endif  // __COLOR_PARAMS_H__
