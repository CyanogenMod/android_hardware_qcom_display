/*
* Copyright (c) 2013-2014 The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <display_config.h>
#include <QServiceUtils.h>
#include <qd_utils.h>

using namespace android;
using namespace qService;

namespace qdutils {

//=============================================================================
// The functions below run in the client process and wherever necessary
// do a binder call to HWC to get/set data.

int isExternalConnected(void) {
    int ret;
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    if(binder != NULL) {
        err = binder->dispatch(IQService::CHECK_EXTERNAL_STATUS,
                &inParcel , &outParcel);
    }
    if(err) {
        ALOGE("%s: Failed to get external status err=%d", __FUNCTION__, err);
        ret = err;
    } else {
        ret = outParcel.readInt32();
    }
    return ret;
}

int getDisplayAttributes(int dpy, DisplayAttributes_t& dpyattr) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    inParcel.writeInt32(dpy);
    if(binder != NULL) {
        err = binder->dispatch(IQService::GET_DISPLAY_ATTRIBUTES,
                &inParcel, &outParcel);
    }
    if(!err) {
        dpyattr.vsync_period = outParcel.readInt32();
        dpyattr.xres = outParcel.readInt32();
        dpyattr.yres = outParcel.readInt32();
        dpyattr.xdpi = outParcel.readFloat();
        dpyattr.ydpi = outParcel.readFloat();
        dpyattr.panel_type = (char) outParcel.readInt32();
    } else {
        ALOGE("%s() failed with err %d", __FUNCTION__, err);
    }
    return err;
}

int setHSIC(int dpy, const HSICData_t& hsic_data) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    inParcel.writeInt32(dpy);
    inParcel.writeInt32(hsic_data.hue);
    inParcel.writeFloat(hsic_data.saturation);
    inParcel.writeInt32(hsic_data.intensity);
    inParcel.writeFloat(hsic_data.contrast);
    if(binder != NULL) {
        err = binder->dispatch(IQService::SET_HSIC_DATA, &inParcel, &outParcel);
    }
    if(err)
        ALOGE("%s: Failed to get external status err=%d", __FUNCTION__, err);
    return err;
}

int getDisplayVisibleRegion(int dpy, hwc_rect_t &rect) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    inParcel.writeInt32(dpy);
    if(binder != NULL) {
        err = binder->dispatch(IQService::GET_DISPLAY_VISIBLE_REGION,
                &inParcel, &outParcel);
    }
    if(!err) {
        rect.left = outParcel.readInt32();
        rect.top = outParcel.readInt32();
        rect.right = outParcel.readInt32();
        rect.bottom = outParcel.readInt32();
    } else {
        ALOGE("%s: Failed to getVisibleRegion for dpy =%d: err = %d",
              __FUNCTION__, dpy, err);
    }
    return err;
}

int setViewFrame(int dpy, int l, int t, int r, int b) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    inParcel.writeInt32(dpy);
    inParcel.writeInt32(l);
    inParcel.writeInt32(t);
    inParcel.writeInt32(r);
    inParcel.writeInt32(b);

    if(binder != NULL) {
        err = binder->dispatch(IQService::SET_VIEW_FRAME,
                &inParcel, &outParcel);
    }
    if(err)
        ALOGE("%s: Failed to set view frame for dpy %d err=%d",
                            __FUNCTION__, dpy, err);

    return err;
}

int setSecondaryDisplayStatus(int dpy, uint32_t status) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    inParcel.writeInt32(dpy);
    inParcel.writeInt32(status);

    if(binder != NULL) {
        err = binder->dispatch(IQService::SET_SECONDARY_DISPLAY_STATUS,
                &inParcel, &outParcel);
    }
    if(err)
        ALOGE("%s: Failed for dpy %d status = %d err=%d", __FUNCTION__, dpy,
                                                        status, err);

    return err;
}

int configureDynRefreshRate(uint32_t op, uint32_t refreshRate) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    inParcel.writeInt32(op);
    inParcel.writeInt32(refreshRate);

    if(binder != NULL) {
        err = binder->dispatch(IQService::CONFIGURE_DYN_REFRESH_RATE,
                               &inParcel, &outParcel);
    }

    if(err)
        ALOGE("%s: Failed setting op %d err=%d", __FUNCTION__, op, err);

    return err;
}

int getConfigCount(int /*dpy*/) {
    int numConfigs = -1;
    sp<IQService> binder = getBinder();
    if(binder != NULL) {
        Parcel inParcel, outParcel;
        inParcel.writeInt32(DISPLAY_PRIMARY);
        status_t err = binder->dispatch(IQService::GET_CONFIG_COUNT,
                &inParcel, &outParcel);
        if(!err) {
            numConfigs = outParcel.readInt32();
            ALOGI("%s() Received num configs %d", __FUNCTION__, numConfigs);
        } else {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return numConfigs;
}

int getActiveConfig(int /*dpy*/) {
    int configIndex = -1;
    sp<IQService> binder = getBinder();
    if(binder != NULL) {
        Parcel inParcel, outParcel;
        inParcel.writeInt32(DISPLAY_PRIMARY);
        status_t err = binder->dispatch(IQService::GET_ACTIVE_CONFIG,
                &inParcel, &outParcel);
        if(!err) {
            configIndex = outParcel.readInt32();
            ALOGI("%s() Received active config index %d", __FUNCTION__,
                    configIndex);
        } else {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return configIndex;
}

int setActiveConfig(int configIndex, int /*dpy*/) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    if(binder != NULL) {
        Parcel inParcel, outParcel;
        inParcel.writeInt32(configIndex);
        inParcel.writeInt32(DISPLAY_PRIMARY);
        err = binder->dispatch(IQService::SET_ACTIVE_CONFIG,
                &inParcel, &outParcel);
        if(!err) {
            ALOGI("%s() Successfully set active config index %d", __FUNCTION__,
                    configIndex);
        } else {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return err;
}

DisplayAttributes getDisplayAttributes(int configIndex, int /*dpy*/) {
    DisplayAttributes dpyattr;
    sp<IQService> binder = getBinder();
    if(binder != NULL) {
        Parcel inParcel, outParcel;
        inParcel.writeInt32(configIndex);
        inParcel.writeInt32(DISPLAY_PRIMARY);
        status_t err = binder->dispatch(
                IQService::GET_DISPLAY_ATTRIBUTES_FOR_CONFIG, &inParcel,
                &outParcel);
        if(!err) {
            dpyattr.vsync_period = outParcel.readInt32();
            dpyattr.xres = outParcel.readInt32();
            dpyattr.yres = outParcel.readInt32();
            dpyattr.xdpi = outParcel.readFloat();
            dpyattr.ydpi = outParcel.readFloat();
            dpyattr.panel_type = (char) outParcel.readInt32();
            ALOGI("%s() Received attrs for index %d: xres %d, yres %d",
                    __FUNCTION__, configIndex, dpyattr.xres, dpyattr.yres);
        } else {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return dpyattr;
}

//=============================================================================
// The functions/methods below run in the context of HWC and
// are called in response to binder calls from clients

Configs* Configs::getInstance() {
    if(sConfigs == NULL) {
        sConfigs = new Configs();
        if(sConfigs->init() == false) {
            ALOGE("%s(): Configs initialization failed", __FUNCTION__);
            delete sConfigs;
            sConfigs = NULL;
        }
    }
    return sConfigs;
}

Configs::Configs() : mActiveConfig(0), mConfigsSupported(0) {}

bool Configs::init() {
    DisplayAttributes dpyAttr;
    if(not getCurrentMode(dpyAttr)) {
        ALOGE("%s(): Mode switch is disabled", __FUNCTION__);
        return false;
    }

    FILE *fHnd;
    size_t len = PAGE_SIZE;
    ssize_t read = 0;
    uint32_t configCount = 0;
    char sysfsPath[MAX_SYSFS_FILE_PATH];

    memset(sysfsPath, '\0', sizeof(sysfsPath));
    snprintf(sysfsPath , sizeof(sysfsPath),
            "/sys/class/graphics/fb0/modes");

    fHnd = fopen(sysfsPath, "r");
    if (fHnd == NULL) {
        ALOGE("%s(): Opening file %s failed with error %s", __FUNCTION__,
                sysfsPath, strerror(errno));
        return false;
    }

    memset(mModeStr, 0, sizeof(mModeStr));
    while((configCount < CONFIGS_MAX) and
            ((read = getline(&mModeStr[configCount], &len, fHnd)) > 0)) {
        //String is of form "U:1600x2560p-0". Documentation/fb/modedb.txt in the
        //kernel has more info on the format.
        char *xptr = strcasestr(mModeStr[configCount], ":");
        char *yptr = strcasestr(mModeStr[configCount], "x");
        if(xptr && yptr) {
            mConfigs[configCount].xres = atoi(xptr + 1);
            mConfigs[configCount].yres = atoi(yptr + 1);
            ALOGI("%s(): Parsed Config %s", __FUNCTION__,
                    mModeStr[configCount]);
            ALOGI("%s(): Config %u: %u x %u", __FUNCTION__, configCount,
                    mConfigs[configCount].xres, mConfigs[configCount].yres);
            if(mConfigs[configCount].xres == dpyAttr.xres and
                    mConfigs[configCount].yres == dpyAttr.yres) {
                mActiveConfig = configCount;
            }
        } else {
            ALOGE("%s(): Tokenizing str %s failed", __FUNCTION__,
                    mModeStr[configCount]);
            //Free memory allocated internally by getline()
            for(uint32_t i = 0; i <= configCount; i++) {
                free(mModeStr[i]);
            }
            fclose(fHnd);
            return false;
        }
        configCount++;
    }

    fclose(fHnd);

    if(configCount == 0) {
        ALOGE("%s No configs found", __FUNCTION__);
        return false;
    }
    mConfigsSupported = configCount;
    return true;
}

bool Configs::getCurrentMode(DisplayAttributes& dpyAttr) {
    bool ret = false;
    FILE *fHnd = fopen("/sys/class/graphics/fb0/mode", "r");
    if(fHnd) {
        char *buffer = NULL; //getline will allocate
        size_t len = PAGE_SIZE;
        if(getline(&buffer, &len, fHnd) > 0) {
            //String is of form "U:1600x2560p-0". Documentation/fb/modedb.txt in
            //kernel has more info on the format.
            char *xptr = strcasestr(buffer, ":");
            char *yptr = strcasestr(buffer, "x");
            if(xptr && yptr) {
                dpyAttr.xres = atoi(xptr + 1);
                dpyAttr.yres = atoi(yptr + 1);
                ALOGI("%s(): Parsed Current Config Str %s", __FUNCTION__,
                        buffer);
                ALOGI("%s(): Current Config: %u x %u", __FUNCTION__,
                        dpyAttr.xres, dpyAttr.yres);
                ret = true;
            }
        }

        if(buffer)
            free(buffer);

        fclose(fHnd);
    }
    return ret;
}

bool Configs::setActiveConfig(const uint32_t& index) {
    if(index >= mConfigsSupported) {
        ALOGE("%s(): Invalid Index %u", __FUNCTION__, index);
        return false;
    }

    bool ret = true;
    int fd = -1;
    size_t len = PAGE_SIZE;
    char sysfsPath[MAX_SYSFS_FILE_PATH];
    memset(sysfsPath, '\0', sizeof(sysfsPath));
    snprintf(sysfsPath , sizeof(sysfsPath),
            "/sys/class/graphics/fb0/mode");

    fd = open(sysfsPath, O_WRONLY);
    if (fd < 0) {
        ALOGE("%s(): Opening file %s failed", __FUNCTION__, sysfsPath);
        return false;
    }

    ssize_t written = pwrite(fd, mModeStr[index], strlen(mModeStr[index]), 0);
    if(written <= 0) {
        ALOGE("%s(): Writing config %s to %s failed with error: %s",
                __FUNCTION__, mModeStr[index], sysfsPath, strerror(errno));
        close(fd);
        return false;
    }

    ALOGI("%s(): Successfully set config %u", __FUNCTION__, index);
    mActiveConfig = index;
    MDPVersion::getInstance().updateSplitInfo();
    close(fd);
    return true;
}

Configs* Configs::sConfigs = NULL;

}; //namespace

// ----------------------------------------------------------------------------
// Screen refresh for native daemons linking dynamically to libqdutils
// ----------------------------------------------------------------------------
extern "C" int refreshScreen() {
    int ret = 0;
    ret = screenRefresh();
    return ret;
}
