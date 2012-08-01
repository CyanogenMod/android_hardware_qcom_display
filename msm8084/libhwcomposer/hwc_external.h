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

#include <fb_priv.h>

struct hwc_context_t;

namespace qhwc {

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
    int getExternalDisplay() const;
    void setExternalDisplay(int connected);
    bool commit();
    int enableHDMIVsync(int enable);

    private:
    bool readResolution();
    int parseResolution(char* edidStr, int* edidModes);
    void setResolution(int ID);
    bool openFramebuffer();
    bool closeFrameBuffer();
    bool writeHPDOption(int userOption) const;
    bool isValidMode(int ID);
    void handleUEvent(char* str, int len);
    int getModeOrder(int mode);
    int getBestMode();
    void resetInfo();

    int mFd;
    int mExternalDisplay;
    int mCurrentMode;
    char mEDIDs[128];
    int mEDIDModes[64];
    int mModeCount;
    hwc_context_t *mHwcContext;
    fb_var_screeninfo mVInfo;
};

}; //qhwc
// ---------------------------------------------------------------------------
#endif //HWC_EXTERNAL_DISPLAY_H
