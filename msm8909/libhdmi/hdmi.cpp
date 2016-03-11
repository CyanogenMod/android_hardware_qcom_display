/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2014, The Linux Foundation. All rights reserved.
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
#include <fcntl.h>
#include <linux/msm_mdp.h>
#include <video/msm_hdmi_modes.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <cutils/properties.h>
#include "hwc_utils.h"
#include "hdmi.h"
#include "overlayUtils.h"
#include "overlay.h"
#include "qd_utils.h"

using namespace android;
using namespace qdutils;

namespace qhwc {
#define UNKNOWN_STRING                  "unknown"
#define SPD_NAME_LENGTH                 16

/* The array gEDIDData contains a list of modes currently
 * supported by HDMI and display, and modes that are not
 * supported i.e. interlaced modes.

 * In order to add support for a new mode, the mode must be
 * appended to the end of the array.
 *
 * Each new entry must contain the following:
 * -Mode: a video format defined in msm_hdmi_modes.h
 * -Width: x resolution for the mode
 * -Height: y resolution for the mode
 * -FPS: the frame rate for the mode
 * -Mode Order: the priority for the new mode that is used when determining
 *  the best mode when the HDMI display is connected.
 */
EDIDData gEDIDData [] = {
    EDIDData(HDMI_VFRMT_1440x480i60_4_3, 1440, 480, 60, 1),
    EDIDData(HDMI_VFRMT_1440x480i60_16_9, 1440, 480, 60, 2),
    EDIDData(HDMI_VFRMT_1440x576i50_4_3, 1440, 576, 50, 3),
    EDIDData(HDMI_VFRMT_1440x576i50_16_9, 1440, 576, 50, 4),
    EDIDData(HDMI_VFRMT_1920x1080i60_16_9, 1920, 1080, 60, 5),
    EDIDData(HDMI_VFRMT_640x480p60_4_3, 640, 480, 60, 6),
    EDIDData(HDMI_VFRMT_720x480p60_4_3, 720, 480, 60, 7),
    EDIDData(HDMI_VFRMT_720x480p60_16_9, 720, 480, 60, 8),
    EDIDData(HDMI_VFRMT_720x576p50_4_3, 720, 576, 50, 9),
    EDIDData(HDMI_VFRMT_720x576p50_16_9, 720, 576, 50, 10),
    EDIDData(HDMI_VFRMT_800x600p60_4_3, 800, 600, 60, 11),
    EDIDData(HDMI_VFRMT_848x480p60_16_9, 848, 480, 60, 12),
    EDIDData(HDMI_VFRMT_1024x768p60_4_3, 1024, 768, 60, 13),
    EDIDData(HDMI_VFRMT_1280x1024p60_5_4, 1280, 1024, 60, 14),
    EDIDData(HDMI_VFRMT_1280x720p50_16_9, 1280, 720, 50, 15),
    EDIDData(HDMI_VFRMT_1280x720p60_16_9, 1280, 720, 60, 16),
    EDIDData(HDMI_VFRMT_1280x800p60_16_10, 1280, 800, 60, 17),
    EDIDData(HDMI_VFRMT_1280x960p60_4_3, 1280, 960, 60, 18),
    EDIDData(HDMI_VFRMT_1360x768p60_16_9, 1360, 768, 60, 19),
    EDIDData(HDMI_VFRMT_1366x768p60_16_10, 1366, 768, 60, 20),
    EDIDData(HDMI_VFRMT_1440x900p60_16_10, 1440, 900, 60, 21),
    EDIDData(HDMI_VFRMT_1400x1050p60_4_3, 1400, 1050, 60, 22),
    EDIDData(HDMI_VFRMT_1680x1050p60_16_10, 1680, 1050, 60, 23),
    EDIDData(HDMI_VFRMT_1600x1200p60_4_3, 1600, 1200, 60, 24),
    EDIDData(HDMI_VFRMT_1920x1080p24_16_9, 1920, 1080, 24, 25),
    EDIDData(HDMI_VFRMT_1920x1080p25_16_9, 1920, 1080, 25, 26),
    EDIDData(HDMI_VFRMT_1920x1080p30_16_9, 1920, 1080, 30, 27),
    EDIDData(HDMI_VFRMT_1920x1080p50_16_9, 1920, 1080, 50, 28),
    EDIDData(HDMI_VFRMT_1920x1080p60_16_9, 1920, 1080, 60, 29),
    EDIDData(HDMI_VFRMT_1920x1200p60_16_10, 1920, 1200, 60, 30),
    EDIDData(HDMI_VFRMT_2560x1600p60_16_9, 2560, 1600, 60, 31),
    EDIDData(HDMI_VFRMT_3840x2160p24_16_9, 3840, 2160, 24, 32),
    EDIDData(HDMI_VFRMT_3840x2160p25_16_9, 3840, 2160, 25, 33),
    EDIDData(HDMI_VFRMT_3840x2160p30_16_9, 3840, 2160, 30, 34),
    EDIDData(HDMI_VFRMT_4096x2160p24_16_9, 4096, 2160, 24, 35),
};

// Number of modes in gEDIDData
const int gEDIDCount = (sizeof(gEDIDData)/sizeof(gEDIDData)[0]);

int HDMIDisplay::configure() {
    if(!openFrameBuffer()) {
        ALOGE("%s: Failed to open FB: %d", __FUNCTION__, mFbNum);
        return -1;
    }
    readCEUnderscanInfo();
    readResolution();
    // TODO: Move this to activate
    /* Used for changing the resolution
     * getUserMode will get the preferred
     * mode set thru adb shell */
    mCurrentMode = getUserMode();
    if (mCurrentMode == -1) {
        //Get the best mode and set
        mCurrentMode = getBestMode();
    }
    setAttributes();
    // set system property
    property_set("hw.hdmiON", "1");

    // Read the system property to determine if downscale feature is enabled.
    char value[PROPERTY_VALUE_MAX];
    mMDPDownscaleEnabled = false;
    if(property_get("sys.hwc.mdp_downscale_enabled", value, "false")
            && !strcmp(value, "true")) {
        mMDPDownscaleEnabled = true;
    }
    return 0;
}

void HDMIDisplay::getAttributes(uint32_t& width, uint32_t& height) {
    uint32_t fps = 0;
    getAttrForMode(width, height, fps);
}

int HDMIDisplay::teardown() {
    closeFrameBuffer();
    resetInfo();
    // unset system property
    property_set("hw.hdmiON", "0");
    return 0;
}

HDMIDisplay::HDMIDisplay():mFd(-1),
    mCurrentMode(-1), mModeCount(0), mPrimaryWidth(0), mPrimaryHeight(0),
    mUnderscanSupported(false)
{
    memset(&mVInfo, 0, sizeof(mVInfo));

    mDisplayId = HWC_DISPLAY_EXTERNAL;
    // Update the display if HDMI is connected as primary
    if (isHDMIPrimaryDisplay()) {
        mDisplayId = HWC_DISPLAY_PRIMARY;
    }

    mFbNum = overlay::Overlay::getInstance()->getFbForDpy(mDisplayId);
    // disable HPD at start, it will be enabled later
    // when the display powers on
    // This helps for framework reboot or adb shell stop/start
    writeHPDOption(0);

    // for HDMI - retreive all the modes supported by the driver
    if(mFbNum != -1) {
        supported_video_mode_lut =
                        new msm_hdmi_mode_timing_info[HDMI_VFRMT_MAX];
        // Populate the mode table for supported modes
        MSM_HDMI_MODES_INIT_TIMINGS(supported_video_mode_lut);
        MSM_HDMI_MODES_SET_SUPP_TIMINGS(supported_video_mode_lut,
                                        MSM_HDMI_MODES_ALL);
        // Update the Source Product Information
        // Vendor Name
        setSPDInfo("vendor_name", "ro.product.manufacturer");
        // Product Description
        setSPDInfo("product_description", "ro.product.name");
    }

    ALOGD_IF(DEBUG, "%s mDisplayId(%d) mFbNum(%d)",
            __FUNCTION__, mDisplayId, mFbNum);
}
/* gets the product manufacturer and product name and writes it
 * to the sysfs node, so that the driver can get that information
 * Used to show QCOM 8974 instead of Input 1 for example
 */
void HDMIDisplay::setSPDInfo(const char* node, const char* property) {
    char info[PROPERTY_VALUE_MAX];
    ssize_t err = -1;
    int spdFile = openDeviceNode(node, O_RDWR);
    if (spdFile >= 0) {
        memset(info, 0, sizeof(info));
        property_get(property, info, UNKNOWN_STRING);
        ALOGD_IF(DEBUG, "In %s: %s = %s",
                __FUNCTION__, property, info);
        if (strncmp(info, UNKNOWN_STRING, SPD_NAME_LENGTH)) {
            err = write(spdFile, info, strlen(info));
            if (err <= 0) {
                ALOGE("%s: file write failed for '%s'"
                      "err no = %d", __FUNCTION__, node, errno);
            }
        } else {
            ALOGD_IF(DEBUG, "%s: property_get failed for SPD %s",
                         __FUNCTION__, node);
        }
        close(spdFile);
    }
}

void HDMIDisplay::setHPD(uint32_t value) {
    ALOGD_IF(DEBUG,"HPD enabled=%d", value);
    writeHPDOption(value);
}

void HDMIDisplay::setActionSafeDimension(int w, int h) {
    ALOGD_IF(DEBUG,"ActionSafe w=%d h=%d", w, h);
    char actionsafeWidth[PROPERTY_VALUE_MAX];
    char actionsafeHeight[PROPERTY_VALUE_MAX];
    snprintf(actionsafeWidth, sizeof(actionsafeWidth), "%d", w);
    property_set("persist.sys.actionsafe.width", actionsafeWidth);
    snprintf(actionsafeHeight, sizeof(actionsafeHeight), "%d", h);
    property_set("persist.sys.actionsafe.height", actionsafeHeight);
}

int HDMIDisplay::getModeCount() const {
    ALOGD_IF(DEBUG,"HPD mModeCount=%d", mModeCount);
    return mModeCount;
}

void HDMIDisplay::readCEUnderscanInfo()
{
    int hdmiScanInfoFile = -1;
    ssize_t len = -1;
    char scanInfo[17];
    char *ce_info_str = NULL;
    char *save_ptr;
    const char token[] = ", \n";
    int ce_info = -1;

    memset(scanInfo, 0, sizeof(scanInfo));
    hdmiScanInfoFile = openDeviceNode("scan_info", O_RDONLY);
    if (hdmiScanInfoFile < 0) {
        return;
    } else {
        len = read(hdmiScanInfoFile, scanInfo, sizeof(scanInfo)-1);
        ALOGD("%s: Scan Info string: %s length = %zu",
                 __FUNCTION__, scanInfo, len);
        if (len <= 0) {
            close(hdmiScanInfoFile);
            ALOGE("%s: Scan Info file empty", __FUNCTION__);
            return;
        }
        scanInfo[len] = '\0';  /* null terminate the string */
        close(hdmiScanInfoFile);
    }

    /*
     * The scan_info contains the three fields
     * PT - preferred video format
     * IT - video format
     * CE video format - containing the underscan support information
     */

    /* PT */
    ce_info_str = strtok_r(scanInfo, token, &save_ptr);
    if (ce_info_str) {
        /* IT */
        ce_info_str = strtok_r(NULL, token, &save_ptr);
        if (ce_info_str) {
            /* CE */
            ce_info_str = strtok_r(NULL, token, &save_ptr);
            if (ce_info_str)
                ce_info = atoi(ce_info_str);
        }
    }

    if (ce_info_str) {
        // ce_info contains the underscan information
        if (ce_info == HDMI_SCAN_ALWAYS_UNDERSCANED ||
            ce_info == HDMI_SCAN_BOTH_SUPPORTED)
            // if TV supported underscan, then driver will always underscan
            // hence no need to apply action safe rectangle
            mUnderscanSupported = true;
    } else {
        ALOGE("%s: scan_info string error", __FUNCTION__);
    }

    // Store underscan support info in a system property
    const char* prop = (mUnderscanSupported) ? "1" : "0";
    property_set("hw.underscan_supported", prop);
    return;
}

HDMIDisplay::~HDMIDisplay()
{
    delete [] supported_video_mode_lut;
    closeFrameBuffer();
}

/*
 * sets the fb_var_screeninfo from the hdmi_mode_timing_info
 */
void setDisplayTiming(struct fb_var_screeninfo &info,
                                const msm_hdmi_mode_timing_info* mode)
{
    info.reserved[0] = 0;
    info.reserved[1] = 0;
    info.reserved[2] = 0;
#ifndef FB_METADATA_VIDEO_INFO_CODE_SUPPORT
    info.reserved[3] = (info.reserved[3] & 0xFFFF) |
              (mode->video_format << 16);
#endif
    info.xoffset = 0;
    info.yoffset = 0;
    info.xres = mode->active_h;
    info.yres = mode->active_v;

    info.pixclock = (mode->pixel_freq)*1000;
    info.vmode = mode->interlaced ?
                    FB_VMODE_INTERLACED : FB_VMODE_NONINTERLACED;

    info.right_margin = mode->front_porch_h;
    info.hsync_len = mode->pulse_width_h;
    info.left_margin = mode->back_porch_h;
    info.lower_margin = mode->front_porch_v;
    info.vsync_len = mode->pulse_width_v;
    info.upper_margin = mode->back_porch_v;
}

int HDMIDisplay::parseResolution(char* edidStr)
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
        mEDIDModes[count] = (int) strtol(start, &end, 10);
        start = end+1;
        count++;
    }
    ALOGD_IF(DEBUG, "In %s: count = %d", __FUNCTION__, count);
    for (int i = 0; i < count; i++)
        ALOGD_IF(DEBUG, "Mode[%d] = %d", i, mEDIDModes[i]);
    return count;
}

bool HDMIDisplay::readResolution()
{
    ssize_t len = -1;
    char edidStr[128] = {'\0'};

    int hdmiEDIDFile = openDeviceNode("edid_modes", O_RDONLY);
    if (hdmiEDIDFile < 0) {
        return false;
    } else {
        len = read(hdmiEDIDFile, edidStr, sizeof(edidStr)-1);
        ALOGD_IF(DEBUG, "%s: EDID string: %s length = %zu",
                 __FUNCTION__, edidStr, len);
        if (len <= 0) {
            ALOGE("%s: edid_modes file empty", __FUNCTION__);
            edidStr[0] = '\0';
        }
        else {
            while (len > 1 && isspace(edidStr[len-1])) {
                --len;
            }
            edidStr[len] = '\0';
        }
        close(hdmiEDIDFile);
    }
    if(len > 0) {
        // Get EDID modes from the EDID strings
        mModeCount = parseResolution(edidStr);
        ALOGD_IF(DEBUG, "%s: mModeCount = %d", __FUNCTION__,
                 mModeCount);
    }

    return (len > 0);
}

bool HDMIDisplay::openFrameBuffer()
{
    if (mFd == -1) {
        char strDevPath[MAX_SYSFS_FILE_PATH];
        snprintf(strDevPath, MAX_SYSFS_FILE_PATH, "/dev/graphics/fb%d", mFbNum);
        mFd = open(strDevPath, O_RDWR);
        if (mFd < 0)
            ALOGE("%s: %s is not available", __FUNCTION__, strDevPath);
    }
    return (mFd > 0);
}

bool HDMIDisplay::closeFrameBuffer()
{
    int ret = 0;
    if(mFd >= 0) {
        ret = close(mFd);
        mFd = -1;
    }
    return (ret == 0);
}

// clears the vinfo, edid, best modes
void HDMIDisplay::resetInfo()
{
    memset(&mVInfo, 0, sizeof(mVInfo));
    memset(mEDIDModes, 0, sizeof(mEDIDModes));
    mModeCount = 0;
    mCurrentMode = -1;
    mUnderscanSupported = false;
    mXres = 0;
    mYres = 0;
    mVsyncPeriod = 0;
    mMDPScalingMode = false;
    // Reset the underscan supported system property
    const char* prop = "0";
    property_set("hw.underscan_supported", prop);
}

int HDMIDisplay::getModeOrder(int mode)
{
    for (int dataIndex = 0; dataIndex < gEDIDCount; dataIndex++) {
        if (gEDIDData[dataIndex].mMode == mode) {
            return gEDIDData[dataIndex].mModeOrder;
        }
    }
    ALOGE("%s Mode not found: %d", __FUNCTION__, mode);
    return -1;
}

/// Returns the user mode set(if any) using adb shell
int HDMIDisplay::getUserMode() {
    /* Based on the property set the resolution */
    char property_value[PROPERTY_VALUE_MAX];
    property_get("hw.hdmi.resolution", property_value, "-1");
    int mode = atoi(property_value);
    // We dont support interlaced modes
    if(isValidMode(mode) && !isInterlacedMode(mode)) {
        ALOGD_IF(DEBUG, "%s: setting the HDMI mode = %d", __FUNCTION__, mode);
        return mode;
    }
    return -1;
}

// Get the best mode for the current HD TV
int HDMIDisplay::getBestMode() {
    int bestOrder = 0;
    int bestMode = HDMI_VFRMT_640x480p60_4_3;
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

inline bool HDMIDisplay::isValidMode(int ID)
{
    bool valid = false;
    for (int i = 0; i < mModeCount; i++) {
        if(ID == mEDIDModes[i]) {
            valid = true;
            break;
        }
    }
    return valid;
}

// returns true if the mode(ID) is interlaced mode format
bool HDMIDisplay::isInterlacedMode(int ID) {
    bool interlaced = false;
    switch(ID) {
        case HDMI_VFRMT_1440x480i60_4_3:
        case HDMI_VFRMT_1440x480i60_16_9:
        case HDMI_VFRMT_1440x576i50_4_3:
        case HDMI_VFRMT_1440x576i50_16_9:
        case HDMI_VFRMT_1920x1080i60_16_9:
            interlaced = true;
            break;
        default:
            interlaced = false;
            break;
    }
    return interlaced;
}

// Does a put_vscreen info on the HDMI interface which will update
// the configuration (resolution, timing info) to match mCurrentMode
void HDMIDisplay::activateDisplay()
{
    int ret = 0;
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

    const struct msm_hdmi_mode_timing_info *mode =
            &supported_video_mode_lut[0];
    for (unsigned int i = 0; i < HDMI_VFRMT_MAX; ++i) {
        const struct msm_hdmi_mode_timing_info *cur =
                &supported_video_mode_lut[i];
        if (cur->video_format == (uint32_t)mCurrentMode) {
            mode = cur;
            break;
        }
    }
    setDisplayTiming(mVInfo, mode);
    ALOGD_IF(DEBUG, "%s: SET Info<ID=%d => Info<ID=%d %dx %d"
            "(%d,%d,%d), (%d,%d,%d) %dMHz>", __FUNCTION__, mCurrentMode,
            mode->video_format, mVInfo.xres, mVInfo.yres,
            mVInfo.right_margin, mVInfo.hsync_len, mVInfo.left_margin,
            mVInfo.lower_margin, mVInfo.vsync_len, mVInfo.upper_margin,
            mVInfo.pixclock/1000/1000);
#ifdef FB_METADATA_VIDEO_INFO_CODE_SUPPORT
    struct msmfb_metadata metadata;
    memset(&metadata, 0 , sizeof(metadata));
    metadata.op = metadata_op_vic;
    metadata.data.video_info_code = mode->video_format;
    if (ioctl(mFd, MSMFB_METADATA_SET, &metadata) == -1) {
        ALOGD("In %s: MSMFB_METADATA_SET failed Err Str = %s",
                __FUNCTION__, strerror(errno));
    }
#endif
    mVInfo.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_ALL | FB_ACTIVATE_FORCE;
    ret = ioctl(mFd, FBIOPUT_VSCREENINFO, &mVInfo);
    if(ret < 0) {
        ALOGD("In %s: FBIOPUT_VSCREENINFO failed Err Str = %s",
                __FUNCTION__, strerror(errno));
    }
}

bool HDMIDisplay::writeHPDOption(int userOption) const
{
    bool ret = true;
    if(mFbNum != -1) {
        int hdmiHPDFile = openDeviceNode("hpd", O_RDWR);
        if (hdmiHPDFile >= 0) {
            ssize_t err = -1;
            ALOGD_IF(DEBUG, "%s: option = %d",
                    __FUNCTION__, userOption);
            if(userOption)
                err = write(hdmiHPDFile, "1", 2);
            else
                err = write(hdmiHPDFile, "0" , 2);
            if (err <= 0) {
                ALOGE("%s: file write failed 'hpd'", __FUNCTION__);
                ret = false;
            }
            close(hdmiHPDFile);
        }
    }
    return ret;
}


void HDMIDisplay::setAttributes() {
    uint32_t fps = 0;
    // Always set dpyAttr res to mVInfo res
    getAttrForMode(mXres, mYres, fps);
    mMDPScalingMode = false;

    if(overlay::Overlay::getInstance()->isUIScalingOnExternalSupported()
        && mMDPDownscaleEnabled) {
        // if primary resolution is more than the hdmi resolution
        // configure dpy attr to primary resolution and set MDP
        // scaling mode
        // Restrict this upto 1080p resolution max, if target does not
        // support source split feature.
        uint32_t primaryArea = mPrimaryWidth * mPrimaryHeight;
        if(((primaryArea) > (mXres * mYres)) &&
            (((primaryArea) <= SUPPORTED_DOWNSCALE_AREA) ||
                qdutils::MDPVersion::getInstance().isSrcSplit())) {
            // tmpW and tmpH will hold the primary dimensions before we
            // update the aspect ratio if necessary.
            int tmpW = mPrimaryWidth;
            int tmpH = mPrimaryHeight;
            // HDMI is always in landscape, so always assign the higher
            // dimension to hdmi's xres
            if(mPrimaryHeight > mPrimaryWidth) {
                tmpW = mPrimaryHeight;
                tmpH = mPrimaryWidth;
            }
            // The aspect ratios of the external and primary displays
            // can be different. As a result, directly assigning primary
            // resolution could lead to an incorrect final image.
            // We get around this by calculating a new resolution by
            // keeping aspect ratio intact.
            hwc_rect r = {0, 0, 0, 0};
            qdutils::getAspectRatioPosition(tmpW, tmpH, mXres, mYres, r);
            uint32_t newExtW = r.right - r.left;
            uint32_t newExtH = r.bottom - r.top;
            uint32_t alignedExtW;
            uint32_t alignedExtH;
            // On 8994 and below targets MDP supports only 4X downscaling,
            // Restricting selected external resolution to be exactly 4X
            // greater resolution than actual external resolution
            uint32_t maxMDPDownScale =
                    qdutils::MDPVersion::getInstance().getMaxMDPDownscale();
            if((mXres * mYres * maxMDPDownScale) < (newExtW * newExtH)) {
                float upScaleFactor = (float)maxMDPDownScale / 2.0f;
                newExtW = (int)((float)mXres * upScaleFactor);
                newExtH = (int)((float)mYres * upScaleFactor);
            }
            // Align it down so that the new aligned resolution does not
            // exceed the maxMDPDownscale factor
            alignedExtW = overlay::utils::aligndown(newExtW, 4);
            alignedExtH = overlay::utils::aligndown(newExtH, 4);
            mXres = alignedExtW;
            mYres = alignedExtH;
            // Set External Display MDP Downscale mode indicator
            mMDPScalingMode = true;
        }
    }
    ALOGD_IF(DEBUG_MDPDOWNSCALE, "Selected external resolution [%d X %d] "
            "maxMDPDownScale %d mMDPScalingMode %d srcSplitEnabled %d "
            "MDPDownscale feature %d",
            mXres, mYres,
            qdutils::MDPVersion::getInstance().getMaxMDPDownscale(),
            mMDPScalingMode, qdutils::MDPVersion::getInstance().isSrcSplit(),
            mMDPDownscaleEnabled);
    mVsyncPeriod = (int) 1000000000l / fps;
    ALOGD_IF(DEBUG, "%s xres=%d, yres=%d", __FUNCTION__, mXres, mYres);
}

void HDMIDisplay::getAttrForMode(uint32_t& width, uint32_t& height,
        uint32_t& fps) {
    for (int dataIndex = 0; dataIndex < gEDIDCount; dataIndex++) {
        if (gEDIDData[dataIndex].mMode == mCurrentMode) {
            width = gEDIDData[dataIndex].mWidth;
            height = gEDIDData[dataIndex].mHeight;
            fps = gEDIDData[dataIndex].mFps;
            return;
        }
    }
    ALOGE("%s Unable to get attributes for %d", __FUNCTION__, mCurrentMode);
}

/* returns the fd related to the node specified*/
int HDMIDisplay::openDeviceNode(const char* node, int fileMode) const {
    char sysFsFilePath[MAX_SYSFS_FILE_PATH];
    memset(sysFsFilePath, 0, sizeof(sysFsFilePath));
    snprintf(sysFsFilePath , sizeof(sysFsFilePath),
            "/sys/devices/virtual/graphics/fb%d/%s",
            mFbNum, node);

    int fd = open(sysFsFilePath, fileMode, 0);

    if (fd < 0) {
        ALOGE("%s: file '%s' not found : ret = %d err str: %s",
                __FUNCTION__, sysFsFilePath, fd, strerror(errno));
    }
    return fd;
}

bool HDMIDisplay::isHDMIPrimaryDisplay() {
    int hdmiNode = qdutils::getHDMINode();
    return (hdmiNode == HWC_DISPLAY_PRIMARY);
}

int HDMIDisplay::getConnectedState() {
    int ret = -1;
    int mFbNum = qdutils::getHDMINode();
    int connectedNode = openDeviceNode("connected", O_RDONLY);
    if(connectedNode >= 0) {
        char opStr[4];
        ssize_t bytesRead = read(connectedNode, opStr, sizeof(opStr) - 1);
        if(bytesRead > 0) {
            opStr[bytesRead] = '\0';
            ret = atoi(opStr);
            ALOGD_IF(DEBUG, "%s: Read %d from connected", __FUNCTION__, ret);
        } else if(bytesRead == 0) {
            ALOGE("%s: HDMI connected node empty", __FUNCTION__);
        } else {
            ALOGE("%s: Read from HDMI connected node failed with error %s",
                    __FUNCTION__, strerror(errno));
        }
        close(connectedNode);
    } else {
        ALOGD("%s: /sys/class/graphics/fb%d/connected could not be opened : %s",
                __FUNCTION__, mFbNum, strerror(errno));
    }
    return ret;
}

void HDMIDisplay::setPrimaryAttributes(uint32_t primaryWidth,
        uint32_t primaryHeight) {
    mPrimaryHeight = primaryHeight;
    mPrimaryWidth = primaryWidth;
}

};
