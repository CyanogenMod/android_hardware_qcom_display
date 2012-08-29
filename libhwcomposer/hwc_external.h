/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are
 * retained for attribution purposes only.

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HWC_EXTERNAL_DISPLAY_H
#define HWC_EXTERNAL_DISPLAY_H

#include <utils/threads.h>
#include <linux/fb.h>

struct hwc_context_t;

namespace qhwc {

#define DEVICE_ROOT "/sys/devices/virtual/graphics"
#define DEVICE_NODE_FB1                 "fb1"
#define DEVICE_NODE_FB2                 "fb2"
#define HDMI_PANEL                      "dtv panel"
#define WFD_PANEL                       "writeback panel"
#define EXTERN_DISPLAY_NONE             0
#define EXTERN_DISPLAY_FB1              1
#define EXTERN_DISPLAY_FB2              2
#define MAX_FRAME_BUFFER_NAME_SIZE      80
#define MAX_DISPLAY_EXTERNAL_DEVICES    2
#define HPD_ENABLE                      1
#define HPD_DISABLE                     0
#define DEVICE_ONLINE                   true
#define DEVICE_OFFLINE                  false


#define SYSFS_EDID_MODES        DEVICE_ROOT "/" DEVICE_NODE_FB1 "/edid_modes"
#define SYSFS_HPD               DEVICE_ROOT "/" DEVICE_NODE_FB1 "/hpd"

class ExternalDisplay
{
    //Type of external display -  OFF, HDMI, WFD
    enum external_display_type {
        EXT_TYPE_NONE,
        EXT_TYPE_HDMI,
        EXT_TYPE_WIFI
    };

    // Mirroring state
    enum external_mirroring_state {
        EXT_MIRRORING_OFF,
        EXT_MIRRORING_ON,
    };
    public:
    ExternalDisplay(hwc_context_t* ctx);
    ~ExternalDisplay();
    int getModeCount() const;
    void getEDIDModes(int *out) const;
    int getExternalDisplay() const;
    void setExternalDisplay(int connected);
    bool commit();
    int enableHDMIVsync(int enable);
    void setHPDStatus(int enabled);
    void setEDIDMode(int resMode);
    void setActionSafeDimension(int w, int h);
    void processUEventOnline(const char *str);
    void processUEventOffline(const char *str);
    bool isHDMIConfigured();

    private:
    bool readResolution();
    int parseResolution(char* edidStr, int* edidModes);
    void setResolution(int ID);
    bool openFrameBuffer(int fbNum);
    bool closeFrameBuffer();
    bool writeHPDOption(int userOption) const;
    bool isValidMode(int ID);
    void handleUEvent(char* str, int len);
    int getModeOrder(int mode);
    int getBestMode();
    void resetInfo();
    void configureWFDDisplay(int fbIndex);

    mutable android::Mutex mExtDispLock;
    int mFd;
    int mCurrentMode;
    int mExternalDisplay;
    int mResolutionMode;
    char mEDIDs[128];
    int mEDIDModes[64];
    int mModeCount;
    hwc_context_t *mHwcContext;
    fb_var_screeninfo mVInfo;
};

}; //qhwc
// ---------------------------------------------------------------------------
#endif //HWC_EXTERNAL_DISPLAY_H
