/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2014, The Linux Foundation. All rights reserved.
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

#ifndef HWC_HDMI_DISPLAY_H
#define HWC_HDMI_DISPLAY_H

#include <linux/fb.h>

struct msm_hdmi_mode_timing_info;

namespace qhwc {

//Type of scanning of EDID(Video Capability Data Block)
enum hdmi_scansupport_type {
    HDMI_SCAN_NOT_SUPPORTED      = 0,
    HDMI_SCAN_ALWAYS_OVERSCANED  = 1,
    HDMI_SCAN_ALWAYS_UNDERSCANED = 2,
    HDMI_SCAN_BOTH_SUPPORTED     = 3
};

// Structure to store EDID related data
struct EDIDData {
    int mMode, mWidth, mHeight, mFps;
    // Predetermined ordering for each mode
    int mModeOrder;
    EDIDData(int mode, int width, int height, int fps, int order)
    : mMode(mode), mWidth(width), mHeight(height), mFps(fps), mModeOrder(order)
    { }
};

class HDMIDisplay
{
public:
    HDMIDisplay();
    ~HDMIDisplay();
    void setHPD(uint32_t startEnd);
    void setActionSafeDimension(int w, int h);
    bool isCEUnderscanSupported() { return mUnderscanSupported; }
    int configure();
    void getAttributes(uint32_t& width, uint32_t& height);
    int teardown();
    uint32_t getWidth() const { return mXres; };
    uint32_t getHeight() const { return mYres; };
    uint32_t getVsyncPeriod() const { return mVsyncPeriod; };
    int getFd() const { return mFd; };
    bool getMDPScalingMode() const { return mMDPScalingMode; }
    void activateDisplay();
    /* Returns true if HDMI is the PRIMARY display device*/
    bool isHDMIPrimaryDisplay();
    int getConnectedState();
    /* when HDMI is an EXTERNAL display, PRIMARY display attributes are needed
       for scaling mode */
    void setPrimaryAttributes(uint32_t primaryWidth, uint32_t primaryHeight);

private:
    int getModeCount() const;
    void setSPDInfo(const char* node, const char* property);
    void readCEUnderscanInfo();
    bool readResolution();
    int  parseResolution(char* edidMode);
    bool openFrameBuffer();
    bool closeFrameBuffer();
    bool writeHPDOption(int userOption) const;
    bool isValidMode(int mode);
    int  getModeOrder(int mode);
    int  getUserMode();
    int  getBestMode();
    bool isInterlacedMode(int mode);
    void resetInfo();
    void setAttributes();
    void getAttrForMode(uint32_t& width, uint32_t& height, uint32_t& fps);
    int openDeviceNode(const char* node, int fileMode) const;

    int mFd;
    int mFbNum;
    int mCurrentMode;
    int mEDIDModes[64];
    int mModeCount;
    fb_var_screeninfo mVInfo;
    // Holds all the HDMI modes and timing info supported by driver
    msm_hdmi_mode_timing_info* supported_video_mode_lut;
    uint32_t mXres, mYres, mVsyncPeriod, mPrimaryWidth, mPrimaryHeight;
    bool mMDPScalingMode;
    bool mUnderscanSupported;
    // Downscale feature switch, set via system property
    // sys.hwc.mdp_downscale_enabled
    bool mMDPDownscaleEnabled;
    int mDisplayId;
};

}; //qhwc
// ---------------------------------------------------------------------------
#endif //HWC_HDMI_DISPLAY_H
