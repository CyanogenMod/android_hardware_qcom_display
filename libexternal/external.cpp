
/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are
 * retained for attribution purposes only.
 *
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

#define DEBUG 0
#include <ctype.h>
#include <fcntl.h>
#include <media/IAudioPolicyService.h>
#include <media/AudioSystem.h>
#include <utils/threads.h>
#include <utils/Errors.h>
#include <utils/Log.h>

#include <linux/msm_mdp.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/resource.h>
#include <cutils/properties.h>
#include "hwc_utils.h"
#include "external.h"
#include "overlayUtils.h"

using namespace android;

namespace qhwc {


#define DEVICE_ROOT "/sys/devices/virtual/graphics"
#define DEVICE_NODE "fb1"

#define SYSFS_EDID_MODES        DEVICE_ROOT "/" DEVICE_NODE "/edid_modes"
#define SYSFS_HPD               DEVICE_ROOT "/" DEVICE_NODE "/hpd"


ExternalDisplay::ExternalDisplay(hwc_context_t* ctx):mFd(-1),
    mCurrentMode(-1), mExternalDisplay(0), mModeCount(0), mHwcContext(ctx)
{
    memset(&mVInfo, 0, sizeof(mVInfo));
    //Enable HPD for HDMI
    writeHPDOption(1);
}

void ExternalDisplay::setEDIDMode(int resMode) {
    ALOGD_IF(DEBUG,"resMode=%d ", resMode);
    int extDispType;
    {
        Mutex::Autolock lock(mExtDispLock);
        extDispType = mExternalDisplay;
        setExternalDisplay(0);
        setResolution(resMode);
    }
    setExternalDisplay(extDispType);
}

void ExternalDisplay::setHPD(uint32_t startEnd) {
    ALOGD_IF(DEBUG,"HPD enabled=%d", startEnd);
    writeHPDOption(startEnd);
}

void ExternalDisplay::setActionSafeDimension(int w, int h) {
    ALOGD_IF(DEBUG,"ActionSafe w=%d h=%d", w, h);
    Mutex::Autolock lock(mExtDispLock);
    overlay::utils::ActionSafe::getInstance()->setDimension(w, h);
    setExternalDisplay(mExternalDisplay);
}

int ExternalDisplay::getModeCount() const {
    ALOGD_IF(DEBUG,"HPD mModeCount=%d", mModeCount);
    Mutex::Autolock lock(mExtDispLock);
    return mModeCount;
}

void ExternalDisplay::getEDIDModes(int *out) const {
    Mutex::Autolock lock(mExtDispLock);
    for(int i = 0;i < mModeCount;i++) {
        out[i] = mEDIDModes[i];
    }
}

ExternalDisplay::~ExternalDisplay()
{
    closeFrameBuffer();
}

struct disp_mode_timing_type {
    int  video_format;

    int  active_h;
    int  active_v;

    int  front_porch_h;
    int  pulse_width_h;
    int  back_porch_h;

    int  front_porch_v;
    int  pulse_width_v;
    int  back_porch_v;

    int  pixel_freq;
    bool interlaced;

    void set_info(struct fb_var_screeninfo &info) const;
};

void disp_mode_timing_type::set_info(struct fb_var_screeninfo &info) const
{
    info.reserved[0] = 0;
    info.reserved[1] = 0;
    info.reserved[2] = 0;
    info.reserved[3] = (info.reserved[3] & 0xFFFF) | (video_format << 16);

    info.xoffset = 0;
    info.yoffset = 0;
    info.xres = active_h;
    info.yres = active_v;

    info.pixclock = pixel_freq*1000;
    info.vmode = interlaced ? FB_VMODE_INTERLACED : FB_VMODE_NONINTERLACED;

    info.right_margin = front_porch_h;
    info.hsync_len = pulse_width_h;
    info.left_margin = back_porch_h;
    info.lower_margin = front_porch_v;
    info.vsync_len = pulse_width_v;
    info.upper_margin = back_porch_v;
}

/* Video formates supported by the HDMI Standard */
/* Indicates the resolution, pix clock and the aspect ratio */
#define m640x480p60_4_3         1
#define m720x480p60_4_3         2
#define m720x480p60_16_9        3
#define m1280x720p60_16_9       4
#define m1920x1080i60_16_9      5
#define m1440x480i60_4_3        6
#define m1440x480i60_16_9       7
#define m1920x1080p60_16_9      16
#define m720x576p50_4_3         17
#define m720x576p50_16_9        18
#define m1280x720p50_16_9       19
#define m1440x576i50_4_3        21
#define m1440x576i50_16_9       22
#define m1920x1080p50_16_9      31
#define m1920x1080p24_16_9      32
#define m1920x1080p25_16_9      33
#define m1920x1080p30_16_9      34

static struct disp_mode_timing_type supported_video_mode_lut[] = {
    {m640x480p60_4_3,     640,  480,  16,  96,  48, 10, 2, 33,  25200, false},
    {m720x480p60_4_3,     720,  480,  16,  62,  60,  9, 6, 30,  27030, false},
    {m720x480p60_16_9,    720,  480,  16,  62,  60,  9, 6, 30,  27030, false},
    {m1280x720p60_16_9,  1280,  720, 110,  40, 220,  5, 5, 20,  74250, false},
    {m1920x1080i60_16_9, 1920,  540,  88,  44, 148,  2, 5,  5,  74250, false},
    {m1440x480i60_4_3,   1440,  240,  38, 124, 114,  4, 3, 15,  27000, true},
    {m1440x480i60_16_9,  1440,  240,  38, 124, 114,  4, 3, 15,  27000, true},
    {m1920x1080p60_16_9, 1920, 1080,  88,  44, 148,  4, 5, 36, 148500, false},
    {m720x576p50_4_3,     720,  576,  12,  64,  68,  5, 5, 39,  27000, false},
    {m720x576p50_16_9,    720,  576,  12,  64,  68,  5, 5, 39,  27000, false},
    {m1280x720p50_16_9,  1280,  720, 440,  40, 220,  5, 5, 20,  74250, false},
    {m1440x576i50_4_3,   1440,  288,  24, 126, 138,  2, 3, 19,  27000, true},
    {m1440x576i50_16_9,  1440,  288,  24, 126, 138,  2, 3, 19,  27000, true},
    {m1920x1080p50_16_9, 1920, 1080, 528,  44, 148,  4, 5, 36, 148500, false},
    {m1920x1080p24_16_9, 1920, 1080, 638,  44, 148,  4, 5, 36,  74250, false},
    {m1920x1080p25_16_9, 1920, 1080, 528,  44, 148,  4, 5, 36,  74250, false},
    {m1920x1080p30_16_9, 1920, 1080,  88,  44, 148,  4, 5, 36,  74250, false},
};

int ExternalDisplay::parseResolution(char* edidStr, int* edidModes)
{
    char delim = ',';
    int count = 0;
    char *start, *end;
    // EDIDs are string delimited by ','
    // Ex: 16,4,5,3,32,34,1
    // Parse this string to get mode(int)
    start = (char*) edidStr;
    end = &delim;
    while(*end == delim) {
        edidModes[count] = (int) strtol(start, &end, 10);
        start = end+1;
        count++;
    }
    ALOGD_IF(DEBUG, "In %s: count = %d", __FUNCTION__, count);
    for (int i = 0; i < count; i++)
        ALOGD_IF(DEBUG, "Mode[%d] = %d", i, edidModes[i]);
    return count;
}

bool ExternalDisplay::readResolution()
{
    int hdmiEDIDFile = open(SYSFS_EDID_MODES, O_RDONLY, 0);
    int len = -1;

    if (hdmiEDIDFile < 0) {
        ALOGE("%s: edid_modes file '%s' not found",
                 __FUNCTION__, SYSFS_EDID_MODES);
        return false;
    } else {
        len = read(hdmiEDIDFile, mEDIDs, sizeof(mEDIDs)-1);
        ALOGD_IF(DEBUG, "%s: EDID string: %s length = %d",
                 __FUNCTION__, mEDIDs, len);
        if ( len <= 0) {
            ALOGE("%s: edid_modes file empty '%s'",
                     __FUNCTION__, SYSFS_EDID_MODES);
        }
        else {
            while (len > 1 && isspace(mEDIDs[len-1]))
                --len;
            mEDIDs[len] = 0;
        }
    }
    close(hdmiEDIDFile);
    if(len > 0) {
        // GEt EDID modes from the EDID strings
        mModeCount = parseResolution(mEDIDs, mEDIDModes);
        ALOGD_IF(DEBUG, "%s: mModeCount = %d", __FUNCTION__,
                 mModeCount);
    }

    return (strlen(mEDIDs) > 0);
}

bool ExternalDisplay::openFramebuffer()
{
    if (mFd == -1) {
        mFd = open("/dev/graphics/fb1", O_RDWR);
        if (mFd < 0)
            ALOGE("%s: /dev/graphics/fb1 not available", __FUNCTION__);
    }
    if(mHwcContext) {
        mHwcContext->dpyAttr[HWC_DISPLAY_EXTERNAL].fd = mFd;
    }
    return (mFd > 0);
}

bool ExternalDisplay::closeFrameBuffer()
{
    int ret = 0;
    if(mFd > 0) {
        ret = close(mFd);
        mFd = -1;
    }
    if(mHwcContext) {
        mHwcContext->dpyAttr[HWC_DISPLAY_EXTERNAL].fd = mFd;
    }
    return (ret == 0);
}

// clears the vinfo, edid, best modes
void ExternalDisplay::resetInfo()
{
    memset(&mVInfo, 0, sizeof(mVInfo));
    memset(mEDIDs, 0, sizeof(mEDIDs));
    memset(mEDIDModes, 0, sizeof(mEDIDModes));
    mModeCount = 0;
    mCurrentMode = -1;
}

int ExternalDisplay::getModeOrder(int mode)
{
    switch (mode) {
        default:
        case m1440x480i60_4_3:
            return 1; // 480i 4:3
        case m1440x480i60_16_9:
            return 2; // 480i 16:9
        case m1440x576i50_4_3:
            return 3; // i576i 4:3
        case m1440x576i50_16_9:
            return 4; // 576i 16:9
        case m640x480p60_4_3:
            return 5; // 640x480 4:3
        case m720x480p60_4_3:
            return 6; // 480p 4:3
        case m720x480p60_16_9:
            return 7; // 480p 16:9
        case m720x576p50_4_3:
            return 8; // 576p 4:3
        case m720x576p50_16_9:
            return 9; // 576p 16:9
        case m1920x1080i60_16_9:
            return 10; // 1080i 16:9
        case m1280x720p50_16_9:
            return 11; // 720p@50Hz
        case m1280x720p60_16_9:
            return 12; // 720p@60Hz
        case m1920x1080p24_16_9:
            return 13; //1080p@24Hz
        case m1920x1080p25_16_9:
            return 14; //108-p@25Hz
        case m1920x1080p30_16_9:
            return 15; //1080p@30Hz
        case m1920x1080p50_16_9:
            return 16; //1080p@50Hz
        case m1920x1080p60_16_9:
            return 17; //1080p@60Hz
    }
}

// Get the best mode for the current HD TV
int ExternalDisplay::getBestMode() {
    int bestOrder = 0;
    int bestMode = m640x480p60_4_3;
    Mutex::Autolock lock(mExtDispLock);
    // for all the edid read, get the best mode
    for(int i = 0; i < mModeCount; i++) {
        int mode = mEDIDModes[i];
        int order = getModeOrder(mode);
        if (order > bestOrder) {
            bestOrder = order;
            bestMode = mode;
        }
    }
    return bestMode;
}

inline bool ExternalDisplay::isValidMode(int ID)
{
    return ((ID >= m640x480p60_4_3) && (ID <= m1920x1080p30_16_9));
}

void ExternalDisplay::setResolution(int ID)
{
    struct fb_var_screeninfo info;
    int ret = 0;
    if (!openFramebuffer())
        return;
    ret = ioctl(mFd, FBIOGET_VSCREENINFO, &mVInfo);
    if(ret < 0) {
        ALOGD("In %s: FBIOGET_VSCREENINFO failed Err Str = %s", __FUNCTION__,
                                                            strerror(errno));
    }

    ALOGD_IF(DEBUG, "%s: GET Info<ID=%d %dx%d (%d,%d,%d),"
            "(%d,%d,%d) %dMHz>", __FUNCTION__,
            mVInfo.reserved[3], mVInfo.xres, mVInfo.yres,
            mVInfo.right_margin, mVInfo.hsync_len, mVInfo.left_margin,
            mVInfo.lower_margin, mVInfo.vsync_len, mVInfo.upper_margin,
            mVInfo.pixclock/1000/1000);
    //If its a valid mode and its a new ID - update var_screeninfo
    if ((isValidMode(ID)) && mCurrentMode != ID) {
        const struct disp_mode_timing_type *mode =
            &supported_video_mode_lut[0];
        unsigned count =  sizeof(supported_video_mode_lut)/sizeof
            (*supported_video_mode_lut);
        for (unsigned int i = 0; i < count; ++i) {
            const struct disp_mode_timing_type *cur =
                &supported_video_mode_lut[i];
            if (cur->video_format == ID)
                mode = cur;
        }
        mode->set_info(mVInfo);
        ALOGD_IF(DEBUG, "%s: SET Info<ID=%d => Info<ID=%d %dx %d"
                 "(%d,%d,%d), (%d,%d,%d) %dMHz>", __FUNCTION__, ID,
                 mVInfo.reserved[3], mVInfo.xres, mVInfo.yres,
                 mVInfo.right_margin, mVInfo.hsync_len, mVInfo.left_margin,
                 mVInfo.lower_margin, mVInfo.vsync_len, mVInfo.upper_margin,
                 mVInfo.pixclock/1000/1000);
        mVInfo.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_ALL | FB_ACTIVATE_FORCE;
        ret = ioctl(mFd, FBIOPUT_VSCREENINFO, &mVInfo);
        if(ret < 0) {
            ALOGD("In %s: FBIOPUT_VSCREENINFO failed Err Str = %s",
                                                 __FUNCTION__, strerror(errno));
        }
        mCurrentMode = ID;
    }
}

void ExternalDisplay::setExternalDisplay(int connected)
{

    hwc_context_t* ctx = mHwcContext;
    if(ctx) {
        ALOGD_IF(DEBUG, "%s: status = %d", __FUNCTION__,
                 connected);
        if(connected) {
            readResolution();
            //Get the best mode and set
            // TODO: Move this to activate
            setResolution(getBestMode());
            setDpyAttr();
            //enable hdmi vsync
        } else {
            // Disable the hdmi vsync
            closeFrameBuffer();
            resetInfo();
        }
        // Store the external display
        mExternalDisplay = connected;
        const char* prop = (connected) ? "1" : "0";
        // set system property
        property_set("hw.hdmiON", prop);
    }
    return;
}

bool ExternalDisplay::writeHPDOption(int userOption) const
{
    bool ret = true;
    int hdmiHPDFile = open(SYSFS_HPD,O_RDWR, 0);
    if (hdmiHPDFile < 0) {
        ALOGE("%s: state file '%s' not found : ret%d"
                           "err str: %s",  __FUNCTION__, SYSFS_HPD, hdmiHPDFile,
                           strerror(errno));
        ret = false;
    } else {
        int err = -1;
        ALOGD_IF(DEBUG, "%s: option = %d", __FUNCTION__,
                 userOption);
        if(userOption)
            err = write(hdmiHPDFile, "1", 2);
        else
            err = write(hdmiHPDFile, "0" , 2);
        if (err <= 0) {
            ALOGE("%s: file write failed '%s'",
                     __FUNCTION__, SYSFS_HPD);
            ret = false;
        }
        close(hdmiHPDFile);
    }
    return ret;
}

/*
 * commits the changes to the external display
 * mExternalDisplay has the mixer number(1-> HDMI 2-> WFD)
 */
bool ExternalDisplay::post()
{
    if(mFd == -1) {
        return false;
    } else if(ioctl(mFd, MSMFB_OVERLAY_COMMIT, &mExternalDisplay) == -1) {
         ALOGE("%s: MSMFB_OVERLAY_COMMIT failed, str: %s", __FUNCTION__,
                                                          strerror(errno));
         return false;
    }
    return true;
}

void ExternalDisplay::setDpyAttr() {
    int width = 0, height = 0, fps = 0;
    getAttrForMode(width, height, fps);
    if(mHwcContext) {
        ALOGD("ExtDisplay setting xres = %d, yres = %d", width, height);
        mHwcContext->dpyAttr[HWC_DISPLAY_EXTERNAL].xres = width;
        mHwcContext->dpyAttr[HWC_DISPLAY_EXTERNAL].yres = height;
        mHwcContext->dpyAttr[HWC_DISPLAY_EXTERNAL].vsync_period =
            1000000000l / fps;
    }
}

void ExternalDisplay::getAttrForMode(int& width, int& height,
int& fps) {
    switch (mCurrentMode) {
        case m640x480p60_4_3:
            width = 640;
            height = 480;
            fps = 60;
            break;
        case m720x480p60_4_3:
        case m720x480p60_16_9:
            width = 720;
            height = 480;
            fps = 60;
            break;
        case m720x576p50_4_3:
        case m720x576p50_16_9:
            width = 720;
            height = 576;
            fps = 50;
            break;
        case m1280x720p50_16_9:
            width = 1280;
            height = 720;
            fps = 50;
            break;
        case m1280x720p60_16_9:
            width = 1280;
            height = 720;
            fps = 60;
            break;
        case m1920x1080p24_16_9:
            width = 1920;
            height = 1080;
            fps = 24;
            break;
        case m1920x1080p25_16_9:
            width = 1920;
            height = 1080;
            fps = 25;
            break;
        case m1920x1080p30_16_9:
            width = 1920;
            height = 1080;
            fps = 30;
            break;
        case m1920x1080p50_16_9:
            width = 1920;
            height = 1080;
            fps = 50;
            break;
        case m1920x1080p60_16_9:
            width = 1920;
            height = 1080;
            fps = 60;
            break;
    }
}

};

