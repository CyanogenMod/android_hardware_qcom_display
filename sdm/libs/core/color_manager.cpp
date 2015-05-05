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

#include <dlfcn.h>
#include <private/color_interface.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include "color_manager.h"

#define __CLASS__ "ColorManager"

namespace sdm {

void *ColorManagerProxy::color_lib_ = NULL;
CreateColorInterface ColorManagerProxy::create_intf_ = NULL;
DestroyColorInterface ColorManagerProxy::destroy_intf_ = NULL;
HWResourceInfo ColorManagerProxy::hw_res_info_;

DisplayError ColorManagerProxy::Init(const HWResourceInfo &hw_res_info) {
  DisplayError error = kErrorNone;

  // Load color service library and retrieve its entry points.
  color_lib_ = ::dlopen(COLORMGR_LIBRARY_NAME, RTLD_NOW);
  if (color_lib_) {
    *(reinterpret_cast<void **>(&create_intf_)) = ::dlsym(color_lib_, CREATE_COLOR_INTERFACE_NAME);
    *(reinterpret_cast<void **>(&destroy_intf_)) =
        ::dlsym(color_lib_, DESTROY_COLOR_INTERFACE_NAME);
    if (!create_intf_ || !destroy_intf_) {
      DLOGW("Fail to retrieve = %s from %s", CREATE_COLOR_INTERFACE_NAME, COLORMGR_LIBRARY_NAME);
      ::dlclose(color_lib_);
      error = kErrorResources;
    }
  } else {
    DLOGW("Fail to load = %s", COLORMGR_LIBRARY_NAME);
    error = kErrorResources;
  }

  hw_res_info_ = hw_res_info;

  return error;
}

void ColorManagerProxy::Deinit() {
  if (color_lib_)
    ::dlclose(color_lib_);
}

ColorManagerProxy::ColorManagerProxy(DisplayType type, HWInterface *intf, HWDisplayAttributes &attr,
                                     HWPanelInfo &info)
    : device_type_(type), pp_hw_attributes_(), hw_intf_(intf), color_intf_(NULL), pp_features_() {}

ColorManagerProxy *ColorManagerProxy::CreateColorManagerProxy(DisplayType type,
                                                              HWInterface *hw_intf,
                                                              HWDisplayAttributes &attribute,
                                                              HWPanelInfo &panel_info) {
  DisplayError error = kErrorNone;
  PPFeatureVersion versions;

  // check if all resources are available before invoking factory method from libsdm-color.so.
  if (!color_lib_ || !create_intf_ || !destroy_intf_) {
    DLOGW("Information for %s isn't available!", COLORMGR_LIBRARY_NAME);
    return NULL;
  }

  ColorManagerProxy *color_manager_proxy =
      new ColorManagerProxy(type, hw_intf, attribute, panel_info);
  if (color_manager_proxy) {
    // 1. need query post-processing feature version from HWInterface.
    error = color_manager_proxy->hw_intf_->GetPPFeaturesVersion(&versions);
    PPHWAttributes &hw_attr = color_manager_proxy->pp_hw_attributes_;
    if (error != kErrorNone) {
      DLOGW("Fail to get DSPP feature versions");
    } else {
      hw_attr.Set(hw_res_info_, panel_info, attribute, versions);
      DLOGW("PAV2 version is versions = %d, version = %d ",
            hw_attr.version.version[kGlobalColorFeaturePaV2],
            versions.version[kGlobalColorFeaturePaV2]);
    }

    // 2. instantiate concrete ColorInterface from libsdm-color.so, pass all hardware info in.
    error = create_intf_(COLOR_VERSION_TAG, color_manager_proxy->device_type_, hw_attr,
                         &color_manager_proxy->color_intf_);
    if (error != kErrorNone) {
      DLOGW("Unable to instantiate concrete ColorInterface from %s", COLORMGR_LIBRARY_NAME);
      delete color_manager_proxy;
      color_manager_proxy = NULL;
    }
  }

  return color_manager_proxy;
}

ColorManagerProxy::~ColorManagerProxy() {
  if (destroy_intf_)
    destroy_intf_(device_type_);
  color_intf_ = NULL;
}

DisplayError ColorManagerProxy::ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload,
                                                     PPDisplayAPIPayload *out_payload,
                                                     PPPendingParams *pending_action) {
  DisplayError ret = kErrorNone;

  // On completion, dspp_features_ will be populated and mark dirty with all resolved dspp
  // feature list with paramaters being transformed into target requirement.
  ret = color_intf_->ColorSVCRequestRoute(in_payload, out_payload, &pp_features_, pending_action);

  return ret;
}

DisplayError ColorManagerProxy::ApplyDefaultDisplayMode(void) {
  DisplayError ret = kErrorNone;

  // On POR, will be invoked from prepare<> request once bootanimation is done.
  ret = color_intf_->ApplyDefaultDisplayMode(&pp_features_);

  return ret;
}

DisplayError ColorManagerProxy::Commit() {
  Locker &locker(pp_features_.GetLocker());
  SCOPE_LOCK(locker);

  DisplayError ret = kErrorNone;
  if (pp_features_.IsDirty()) {
    ret = hw_intf_->SetPPFeatures(pp_features_);
  }

  return ret;
}

}  // namespace sdm
