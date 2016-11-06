/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
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
*/

#include <dlfcn.h>
#include <powermanager/IPowerManager.h>
#include <cutils/sockets.h>
#include <cutils/native_handle.h>
#include <utils/String16.h>
#include <binder/Parcel.h>
#include <gralloc_priv.h>
#include <hardware/hwcomposer.h>
#include <hardware/hwcomposer_defs.h>
#include <QService.h>

#include <core/dump_interface.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <core/buffer_allocator.h>
#include <private/color_params.h>
#include "hwc_buffer_allocator.h"
#include "hwc_buffer_sync_handler.h"
#include "hwc_session.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCColorManager"

namespace sdm {

uint32_t HWCColorManager::Get8BitsARGBColorValue(const PPColorFillParams &params) {
  uint32_t argb_color = ((params.color.r << 16) & 0xff0000) | ((params.color.g) & 0xff)
                        | ((params.color.b << 8) & 0xff00);
  return argb_color;
}

int HWCColorManager::CreatePayloadFromParcel(const android::Parcel &in, uint32_t *disp_id,
                                             PPDisplayAPIPayload *sink) {
  int ret = 0;
  uint32_t id(0);
  uint32_t size(0);

  id = in.readInt32();
  size = in.readInt32();
  if (size > 0 && size == in.dataAvail()) {
    const void *data = in.readInplace(size);
    const uint8_t *temp = reinterpret_cast<const uint8_t *>(data);

    sink->size = size;
    sink->payload = const_cast<uint8_t *>(temp);
    *disp_id = id;
  } else {
    DLOGW("Failing size checking, size = %d", size);
    ret = -EINVAL;
  }

  return ret;
}

void HWCColorManager::MarshallStructIntoParcel(const PPDisplayAPIPayload &data,
                                               android::Parcel *out_parcel) {
  out_parcel->writeInt32(data.size);
  if (data.payload)
    out_parcel->write(data.payload, data.size);
}

HWCColorManager *HWCColorManager::CreateColorManager() {
  HWCColorManager *color_mgr = new HWCColorManager();

  if (color_mgr) {
    void *&color_lib = color_mgr->color_apis_lib_;
    // Load display API interface library. And retrieve color API function tables.
    color_lib = ::dlopen(DISPLAY_API_INTERFACE_LIBRARY_NAME, RTLD_NOW);
    if (color_lib) {
      color_mgr->color_apis_ = ::dlsym(color_lib, DISPLAY_API_FUNC_TABLES);
      if (!color_mgr->color_apis_) {
        DLOGE("Fail to retrieve = %s from %s", DISPLAY_API_FUNC_TABLES,
              DISPLAY_API_INTERFACE_LIBRARY_NAME);
        ::dlclose(color_lib);
        delete color_mgr;
        return NULL;
      }
    } else {
      DLOGW("Unable to load = %s", DISPLAY_API_INTERFACE_LIBRARY_NAME);
      delete color_mgr;
      return NULL;
    }
    DLOGI("Successfully loaded %s", DISPLAY_API_INTERFACE_LIBRARY_NAME);

    // Load diagclient library and invokes its entry point to pass in display APIs.
    void *&diag_lib = color_mgr->diag_client_lib_;
    diag_lib = ::dlopen(QDCM_DIAG_CLIENT_LIBRARY_NAME, RTLD_NOW);
    if (diag_lib) {
      *(reinterpret_cast<void **>(&color_mgr->qdcm_diag_init_)) =
          ::dlsym(diag_lib, INIT_QDCM_DIAG_CLIENT_NAME);
      *(reinterpret_cast<void **>(&color_mgr->qdcm_diag_deinit_)) =
          ::dlsym(diag_lib, DEINIT_QDCM_DIAG_CLIENT_NAME);

      if (!color_mgr->qdcm_diag_init_ || !color_mgr->qdcm_diag_deinit_) {
        DLOGE("Fail to retrieve = %s from %s", INIT_QDCM_DIAG_CLIENT_NAME,
              QDCM_DIAG_CLIENT_LIBRARY_NAME);
        ::dlclose(diag_lib);
      } else {
        // invoke Diag Client entry point to initialize.
        color_mgr->qdcm_diag_init_(color_mgr->color_apis_);
        DLOGI("Successfully loaded %s and %s and diag_init'ed", DISPLAY_API_INTERFACE_LIBRARY_NAME,
              QDCM_DIAG_CLIENT_LIBRARY_NAME);
      }
    } else {
      DLOGW("Unable to load = %s", QDCM_DIAG_CLIENT_LIBRARY_NAME);
      // only QDCM Diag client failed to be loaded and system still should function.
    }
  } else {
    DLOGE("Unable to create HWCColorManager");
    return NULL;
  }

  return color_mgr;
}

HWCColorManager::~HWCColorManager() {
}

void HWCColorManager::DestroyColorManager() {
  if (qdcm_mode_mgr_) {
    delete qdcm_mode_mgr_;
  }
  if (qdcm_diag_deinit_) {
    qdcm_diag_deinit_();
  }
  if (diag_client_lib_) {
    ::dlclose(diag_client_lib_);
  }
  if (color_apis_lib_) {
    ::dlclose(color_apis_lib_);
  }
  delete this;
}

int HWCColorManager::EnableQDCMMode(bool enable, HWCDisplay *hwc_display) {
  int ret = 0;

  if (!qdcm_mode_mgr_) {
    qdcm_mode_mgr_ = HWCQDCMModeManager::CreateQDCMModeMgr();
    if (!qdcm_mode_mgr_) {
      DLOGE("Unable to create QDCM operating mode manager.");
      ret = -EFAULT;
    }
  }

  if (qdcm_mode_mgr_) {
    ret = qdcm_mode_mgr_->EnableQDCMMode(enable, hwc_display);
  }

  return ret;
}

bool HWCColorManager::SolidFillLayersPrepare(hwc_display_contents_1_t **displays,
                                             HWCDisplay *hwc_display) {
  SCOPE_LOCK(locker_);

  // Query HWCColorManager if QDCM tool requesting SOLID_FILL mode.
  uint32_t solid_fill_color = Get8BitsARGBColorValue(solid_fill_params_);
  hwc_display_contents_1_t *layer_list = displays[HWC_DISPLAY_PRIMARY];

  if (solid_fill_enable_ && solid_fill_layers_ && layer_list) {
    // 1. shallow copy HWC_FRAMEBUFFER_TARGET layer info solid fill layer list.
    solid_fill_layers_->hwLayers[1] = layer_list->hwLayers[layer_list->numHwLayers - 1];

    // 2. continue the prepare<> on solid_fill_layers.
    hwc_display->Perform(HWCDisplayPrimary::SET_QDCM_SOLID_FILL_INFO, solid_fill_color);
    hwc_display->Prepare(solid_fill_layers_);  // RECT info included.

    // 3. Set HWC_OVERLAY to all SF layers before returning to framework.
    for (size_t i = 0; i < (layer_list->numHwLayers - 1); i++) {
      hwc_layer_1_t *layer = &layer_list->hwLayers[i];
      layer->compositionType = HWC_OVERLAY;
    }

    return true;
  } else if (!solid_fill_enable_) {
    hwc_display->Perform(HWCDisplayPrimary::UNSET_QDCM_SOLID_FILL_INFO, 0);
  }

  return false;
}

bool HWCColorManager::SolidFillLayersSet(hwc_display_contents_1_t **displays,
                                         HWCDisplay *hwc_display) {
  // Query HWCColorManager if QDCM tool requesting SOLID_FILL mode.
  SCOPE_LOCK(locker_);
  hwc_display_contents_1_t *layer_list = displays[HWC_DISPLAY_PRIMARY];
  if (solid_fill_enable_ && solid_fill_layers_ && layer_list) {
    hwc_display->Commit(solid_fill_layers_);

    // SurfaceFlinger layer stack is dropped in solid fill case and replaced with local layer stack
    // Close acquire fence fds associated with SF layer stack
    // Close release/retire fence fds returned along with local layer stack
    for (size_t i = 0; i < (layer_list->numHwLayers - 1); i++) {
      int &fence_fd = layer_list->hwLayers[i].acquireFenceFd;
      if (fence_fd >= 0) {
        close(fence_fd);
        fence_fd = -1;
      }
    }

    for (size_t i = 0; i < (solid_fill_layers_->numHwLayers - 1); i++) {
      int &fence_fd = solid_fill_layers_->hwLayers[i].releaseFenceFd;
      if (fence_fd >= 0) {
        close(fence_fd);
        fence_fd = -1;
      }
    }
    if (solid_fill_layers_->retireFenceFd >= 0) {
      close(solid_fill_layers_->retireFenceFd);
      solid_fill_layers_->retireFenceFd = -1;
    }

    return true;
  }

  return false;
}

int HWCColorManager::CreateSolidFillLayers(HWCDisplay *hwc_display) {
  int ret = 0;

  if (!solid_fill_layers_) {
    uint32_t size = sizeof(hwc_display_contents_1) + kNumSolidFillLayers * sizeof(hwc_layer_1_t);
    uint32_t primary_width = 0;
    uint32_t primary_height = 0;

    hwc_display->GetPanelResolution(&primary_width, &primary_height);
    uint8_t *buf = new uint8_t[size]();
    // handle for solid fill layer with fd = -1.
    private_handle_t *handle =
        new private_handle_t(-1, 0, private_handle_t::PRIV_FLAGS_FRAMEBUFFER, BUFFER_TYPE_UI,
                            HAL_PIXEL_FORMAT_RGBA_8888, primary_width, primary_height,
                            primary_width, primary_height);

    if (!buf || !handle) {
      DLOGE("Failed to allocate memory.");
      if (buf)
        delete[] buf;
      if (handle)
        delete handle;

      return -ENOMEM;
    }

    solid_fill_layers_ = reinterpret_cast<hwc_display_contents_1 *>(buf);
    hwc_layer_1_t &layer = solid_fill_layers_->hwLayers[0];
    layer.handle = handle;
  }

  solid_fill_layers_->flags = HWC_GEOMETRY_CHANGED;
  solid_fill_layers_->numHwLayers = kNumSolidFillLayers;
  solid_fill_layers_->retireFenceFd = -1;
  solid_fill_layers_->outbuf = NULL;
  solid_fill_layers_->outbufAcquireFenceFd = -1;

  hwc_layer_1_t &layer = solid_fill_layers_->hwLayers[0];
  hwc_rect_t solid_fill_rect = {
      INT(solid_fill_params_.rect.x), INT(solid_fill_params_.rect.y),
      INT(solid_fill_params_.rect.x + solid_fill_params_.rect.width),
      INT(solid_fill_params_.rect.y + solid_fill_params_.rect.height),
  };

  layer.compositionType = HWC_FRAMEBUFFER;
  layer.blending = HWC_BLENDING_PREMULT;
  layer.sourceCropf.left = solid_fill_params_.rect.x;
  layer.sourceCropf.top = solid_fill_params_.rect.y;
  layer.sourceCropf.right = solid_fill_params_.rect.x + solid_fill_params_.rect.width;
  layer.sourceCropf.bottom = solid_fill_params_.rect.y + solid_fill_params_.rect.height;
  layer.acquireFenceFd = -1;
  layer.releaseFenceFd = -1;
  layer.flags = 0;
  layer.transform = 0;
  layer.hints = 0;
  layer.planeAlpha = 0xff;
  layer.displayFrame = solid_fill_rect;
  layer.visibleRegionScreen.numRects = 1;
  layer.visibleRegionScreen.rects = &layer.displayFrame;
  layer.surfaceDamage.numRects = 0;

  return ret;
}

void HWCColorManager::DestroySolidFillLayers() {
  if (solid_fill_layers_) {
    hwc_layer_1_t &layer = solid_fill_layers_->hwLayers[0];
    uint8_t *buf = reinterpret_cast<uint8_t *>(solid_fill_layers_);
    private_handle_t const *hnd = reinterpret_cast<private_handle_t const *>(layer.handle);

    if (hnd)
        delete hnd;

    if (buf)
        delete[] buf;

    solid_fill_layers_ = NULL;
  }
}

int HWCColorManager::SetSolidFill(const void *params, bool enable, HWCDisplay *hwc_display) {
  SCOPE_LOCK(locker_);
  int ret = 0;

  if (params) {
    solid_fill_params_ = *reinterpret_cast<const PPColorFillParams *>(params);
  } else {
    solid_fill_params_ = PPColorFillParams();
  }

  if (enable) {
    // will create solid fill layers for rendering if not present.
    ret = CreateSolidFillLayers(hwc_display);
  } else {
    DestroySolidFillLayers();
  }
  solid_fill_enable_ = enable;

  return ret;
}

const HWCQDCMModeManager::ActiveFeatureCMD HWCQDCMModeManager::kActiveFeatureCMD[] = {
    HWCQDCMModeManager::ActiveFeatureCMD("cabl:on", "cabl:off", "cabl:status", "running"),
    HWCQDCMModeManager::ActiveFeatureCMD("ad:on", "ad:off", "ad:query:status", "running"),
    HWCQDCMModeManager::ActiveFeatureCMD("svi:on", "svi:off", "svi:status", "running"),
};

const char *const HWCQDCMModeManager::kSocketName = "pps";
const char *const HWCQDCMModeManager::kTagName = "surfaceflinger";
const char *const HWCQDCMModeManager::kPackageName = "colormanager";

HWCQDCMModeManager *HWCQDCMModeManager::CreateQDCMModeMgr() {
  HWCQDCMModeManager *mode_mgr = new HWCQDCMModeManager();

  if (!mode_mgr) {
    DLOGW("No memory to create HWCQDCMModeManager.");
    return NULL;
  } else {
    mode_mgr->socket_fd_ =
        ::socket_local_client(kSocketName, ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM);
    if (mode_mgr->socket_fd_ < 0) {
      // it should not be disastrous and we still can grab wakelock in QDCM mode.
      DLOGW("Unable to connect to dpps socket!");
    }

    // retrieve system GPU idle timeout value for later to recover.
    mode_mgr->entry_timeout_ = HWCDebugHandler::GetIdleTimeoutMs();

    // acquire the binder handle to Android system PowerManager for later use.
    android::sp<android::IBinder> binder =
        android::defaultServiceManager()->checkService(android::String16("power"));
    if (binder == NULL) {
      DLOGW("Application can't connect to  power manager service");
      delete mode_mgr;
      mode_mgr = NULL;
    } else {
      mode_mgr->power_mgr_ = android::interface_cast<android::IPowerManager>(binder);
    }
  }

  return mode_mgr;
}

HWCQDCMModeManager::~HWCQDCMModeManager() {
  if (socket_fd_ >= 0)
    ::close(socket_fd_);
}

int HWCQDCMModeManager::AcquireAndroidWakeLock(bool enable) {
  int ret = 0;

  if (enable) {
    if (wakelock_token_ == NULL) {
      android::sp<android::IBinder> binder = new android::BBinder();
      android::status_t status = power_mgr_->acquireWakeLock(
          (kFullWakeLock | kAcquireCauseWakeup | kONAfterRelease), binder,
          android::String16(kTagName), android::String16(kPackageName));
      if (status == android::NO_ERROR) {
        wakelock_token_ = binder;
      }
    }
  } else {
    if (wakelock_token_ != NULL && power_mgr_ != NULL) {
      power_mgr_->releaseWakeLock(wakelock_token_, 0);
      wakelock_token_.clear();
      wakelock_token_ = NULL;
    }
  }

  return ret;
}

int HWCQDCMModeManager::EnableActiveFeatures(bool enable,
                                             const HWCQDCMModeManager::ActiveFeatureCMD &cmds,
                                             bool *was_running) {
  int ret = 0;
  ssize_t size = 0;
  char response[kSocketCMDMaxLength] = {
      0,
  };

  if (socket_fd_ < 0) {
    DLOGW("No socket connection available!");
    return -EFAULT;
  }

  if (!enable) {  // if client requesting to disable it.
    // query CABL status, if off, no action. keep the status.
    size = ::write(socket_fd_, cmds.cmd_query_status, strlen(cmds.cmd_query_status));
    if (size < 0) {
      DLOGW("Unable to send data over socket %s", ::strerror(errno));
      ret = -EFAULT;
    } else {
      size = ::read(socket_fd_, response, kSocketCMDMaxLength);
      if (size < 0) {
        DLOGW("Unable to read data over socket %s", ::strerror(errno));
        ret = -EFAULT;
      } else if (!strncmp(response, cmds.running, strlen(cmds.running))) {
        *was_running = true;
      }
    }

    if (*was_running) {  // if was running, it's requested to disable it.
      size = ::write(socket_fd_, cmds.cmd_off, strlen(cmds.cmd_off));
      if (size < 0) {
        DLOGW("Unable to send data over socket %s", ::strerror(errno));
        ret = -EFAULT;
      }
    }
  } else {  // if was running, need enable it back.
    if (*was_running) {
      size = ::write(socket_fd_, cmds.cmd_on, strlen(cmds.cmd_on));
      if (size < 0) {
        DLOGW("Unable to send data over socket %s", ::strerror(errno));
        ret = -EFAULT;
      }
    }
  }

  return ret;
}

int HWCQDCMModeManager::EnableQDCMMode(bool enable, HWCDisplay *hwc_display) {
  int ret = 0;

  ret = EnableActiveFeatures((enable ? false : true), kActiveFeatureCMD[kCABLFeature],
                             &cabl_was_running_);
  ret = AcquireAndroidWakeLock(enable);

  // if enter QDCM mode, disable GPU fallback idle timeout.
  if (hwc_display) {
    uint32_t timeout = enable ? 0 : entry_timeout_;
    hwc_display->SetIdleTimeoutMs(timeout);
  }

  return ret;
}

}  // namespace sdm
