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
#include <utils/String16.h>
#include <binder/Parcel.h>
#include <QService.h>

#include <core/dump_interface.h>
#include <utils/constants.h>
#include <core/buffer_allocator.h>
#include <private/color_params.h>
#include "hwc_buffer_allocator.h"
#include "hwc_buffer_sync_handler.h"
#include "hwc_session.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCColorManager"

namespace sdm {

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
                                               android::Parcel &out_parcel) {
  out_parcel.writeInt32(data.size);
  if (data.payload)
    out_parcel.write(data.payload, data.size);
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
        DLOGW("Fail to retrieve = %s from %s", DISPLAY_API_FUNC_TABLES,
              DISPLAY_API_INTERFACE_LIBRARY_NAME);
        ::dlclose(color_lib);
        delete color_mgr;
        return NULL;
      }
    } else {
      DLOGW("Fail to load = %s", DISPLAY_API_INTERFACE_LIBRARY_NAME);
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
        DLOGW("Fail to retrieve = %s from %s", INIT_QDCM_DIAG_CLIENT_NAME,
              QDCM_DIAG_CLIENT_LIBRARY_NAME);
        ::dlclose(diag_lib);
      } else {
        // invoke Diag Client entry point to initialize.
        color_mgr->qdcm_diag_init_(color_mgr->color_apis_);
        DLOGI("Successfully loaded %s and %s and diag_init'ed", DISPLAY_API_INTERFACE_LIBRARY_NAME,
              QDCM_DIAG_CLIENT_LIBRARY_NAME);
      }
    } else {
      DLOGW("Fail to load = %s", QDCM_DIAG_CLIENT_LIBRARY_NAME);
      // only QDCM Diag client failed to be loaded and system still should function.
    }
  } else {
    DLOGW("Unable to create HWCColorManager");
    return NULL;
  }

  return color_mgr;
}

HWCColorManager::~HWCColorManager() {}

void HWCColorManager::DestroyColorManager() {
  if (diag_client_lib_) {
    ::dlclose(diag_client_lib_);
  }
  if (color_apis_lib_) {
    ::dlclose(color_apis_lib_);
  }
  delete this;
}

int HWCColorManager::EnableQDCMMode(bool enable) {
  int ret = 0;

  if (enable) {  // entering QDCM mode, disable all active features and acquire Android wakelock
    qdcm_mode_mgr_ = HWCQDCMModeManager::CreateQDCMModeMgr();
    if (!qdcm_mode_mgr_) {
      DLOGW("failing to create QDCM operating mode manager.");
      ret = -EFAULT;
    } else {
      ret = qdcm_mode_mgr_->EnableQDCMMode(enable);
    }
  } else {  // exiting QDCM mode, reverse the effect of entering.
    if (!qdcm_mode_mgr_) {
      DLOGW("failing to disable QDCM operating mode manager.");
      ret = -EFAULT;
    } else {  // once exiting from QDCM operating mode, destroy QDCMModeMgr and release the
              // resources
      ret = qdcm_mode_mgr_->EnableQDCMMode(enable);
      delete qdcm_mode_mgr_;
      qdcm_mode_mgr_ = NULL;
    }
  }

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
  char response[kSocketCMDMaxLength] = {
      0,
  };

  if (socket_fd_ < 0) {
    DLOGW("No socket connection available!");
    return -EFAULT;
  }

  if (!enable) {  // if client requesting to disable it.
    // query CABL status, if off, no action. keep the status.
    if (::write(socket_fd_, cmds.cmd_query_status, strlen(cmds.cmd_query_status)) < 0) {
      DLOGW("Unable to send data over socket %s", ::strerror(errno));
      ret = -EFAULT;
    } else if (::read(socket_fd_, response, kSocketCMDMaxLength) < 0) {
      DLOGW("Unable to read data over socket %s", ::strerror(errno));
      ret = -EFAULT;
    } else if (!strncmp(response, cmds.running, strlen(cmds.running))) {
      *was_running = true;
    }

    if (*was_running) {  // if was running, it's requested to disable it.
      if (::write(socket_fd_, cmds.cmd_off, strlen(cmds.cmd_off)) < 0) {
        DLOGW("Unable to send data over socket %s", ::strerror(errno));
        ret = -EFAULT;
      }
    }
  } else {  // if was running, need enable it back.
    if (*was_running) {
      if (::write(socket_fd_, cmds.cmd_on, strlen(cmds.cmd_on)) < 0) {
        DLOGW("Unable to send data over socket %s", ::strerror(errno));
        ret = -EFAULT;
      }
    }
  }

  return ret;
}

int HWCQDCMModeManager::EnableQDCMMode(bool enable) {
  int ret = 0;

  ret = EnableActiveFeatures((enable ? false : true), kActiveFeatureCMD[kCABLFeature],
                             &cabl_was_running_);
  ret = AcquireAndroidWakeLock(enable);

  return ret;
}

}  // namespace sdm
