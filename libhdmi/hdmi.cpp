/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2015, The Linux Foundation. All rights reserved.
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

int HDMIDisplay::configure() {
    if(!openFrameBuffer()) {
        ALOGE("%s: Failed to open FB: %d", __FUNCTION__, mFbNum);
        return -1;
    }
    readCEUnderscanInfo();
    readResolution();
    /* Used for changing the resolution
     * getUserConfig will get the preferred
     * config index set thru adb shell */
    mActiveConfig = getUserConfig();
    if (mActiveConfig == -1) {
        //Get the best mode and set
        mActiveConfig = getBestConfig();
    }

    // Read the system property to determine if downscale feature is enabled.
    char value[PROPERTY_VALUE_MAX];
    mMDPDownscaleEnabled = false;
    if(property_get("sys.hwc.mdp_downscale_enabled", value, "false")
            && !strcmp(value, "true")) {
        mMDPDownscaleEnabled = true;
    }

    // Set the mode corresponding to the active index
    mCurrentMode = mEDIDModes[mActiveConfig];
    setAttributes();
    // set system property
    property_set("hw.hdmiON", "1");

    // XXX: A debug property can be used to enable resolution change for
    // testing purposes: debug.hwc.enable_resolution_change
    mEnableResolutionChange = false;
    if(property_get("debug.hwc.enable_resolution_change", value, "false")
            && !strcmp(value, "true")) {
        mEnableResolutionChange = true;
    }
    return 0;
}

void HDMIDisplay::getAttributes(uint32_t& width, uint32_t& height) {
    uint32_t refresh = 0, fps = 0;
    getAttrForConfig(mActiveConfig, width, height, refresh, fps);
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
    mUnderscanSupported(false), mMDPDownscaleEnabled(false)
{
    memset(&mVInfo, 0, sizeof(mVInfo));
    mFbNum = qdutils::getHDMINode();

    mDisplayId = HWC_DISPLAY_EXTERNAL;
    // Update the display if HDMI is connected as primary
    if (isHDMIPrimaryDisplay()) {
        mDisplayId = HWC_DISPLAY_PRIMARY;
    }

    // Disable HPD at start if HDMI is external, it will be enabled later
    // when the display powers on
    // This helps for framework reboot or adb shell stop/start
    if (mDisplayId) {
        writeHPDOption(0);
    }

    if(mFbNum != -1) {
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
    // Set the sync polarity based on the current mode
    if (!mode->active_low_h)
        info.sync |= FB_SYNC_HOR_HIGH_ACT;
    else
        info.sync &= ~FB_SYNC_HOR_HIGH_ACT;

    if (!mode->active_low_v)
        info.sync |= FB_SYNC_VERT_HIGH_ACT;
    else
        info.sync &= ~FB_SYNC_VERT_HIGH_ACT;
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
    char edidStr[PAGE_SIZE] = {'\0'};

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
    // Populate the internal data structure with the timing information
    // for each edid mode read from the driver
    if (mModeCount > 0) {
        mDisplayConfigs = new msm_hdmi_mode_timing_info[mModeCount];
        readConfigs();
    } else {
        // If we fail to read from EDID when HDMI is connected, then
        // mModeCount will be 0 and bestConfigIndex will be invalid.
        // In this case, we populate the mEDIDModes structure with
        // a default mode at config index 0.
        uint32_t defaultConfigIndex = 0;
        mModeCount = 1;
        mEDIDModes[defaultConfigIndex] = HDMI_VFRMT_640x480p60_4_3;
        struct msm_hdmi_mode_timing_info defaultMode =
                HDMI_VFRMT_640x480p60_4_3_TIMING;
        mDisplayConfigs = new msm_hdmi_mode_timing_info[mModeCount];
        mDisplayConfigs[defaultConfigIndex] = defaultMode;
        ALOGD("%s Defaulting to HDMI_VFRMT_640x480p60_4_3", __FUNCTION__);
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
    if (mDisplayConfigs) {
        delete [] mDisplayConfigs;
        mDisplayConfigs = 0;
    }
    // Reset the underscan supported system property
    const char* prop = "0";
    property_set("hw.underscan_supported", prop);
}

/// Returns the index of the user mode set(if any) using adb shell
int HDMIDisplay::getUserConfig() {
    /* Based on the property set the resolution */
    char property_value[PROPERTY_VALUE_MAX];
    property_get("hw.hdmi.resolution", property_value, "-1");
    int mode = atoi(property_value);
    if(isValidMode(mode)) {
        ALOGD_IF(DEBUG, "%s: setting the HDMI mode = %d", __FUNCTION__, mode);
        return getModeIndex(mode);
    }
    return -1;
}

// Get the index of the best mode for the current HD TV
int HDMIDisplay::getBestConfig() {
    int bestConfigIndex = 0;
    int edidMode = -1;
    struct msm_hdmi_mode_timing_info currentModeInfo = {0};
    struct msm_hdmi_mode_timing_info bestModeInfo = {0};
    bestModeInfo.video_format = 0;
    bestModeInfo.active_v = 0;
    bestModeInfo.active_h = 0;
    bestModeInfo.refresh_rate = 0;
    bestModeInfo.ar = HDMI_RES_AR_INVALID;

    // for all the timing info read, get the best config
    for (int configIndex = 0; configIndex < mModeCount; configIndex++) {
        currentModeInfo = mDisplayConfigs[configIndex];
        edidMode = mEDIDModes[configIndex];

        if (!isValidMode(edidMode)) {
            ALOGD("%s EDID Mode %d is not supported", __FUNCTION__, edidMode);
            continue;
        }

        ALOGD_IF(DEBUG, "%s Best (%d) : (%dx%d) @ %d;"
                " Current (%d) (%dx%d) @ %d",
                __FUNCTION__, bestConfigIndex, bestModeInfo.active_h,
                bestModeInfo.active_v, bestModeInfo.refresh_rate, configIndex,
                currentModeInfo.active_h, currentModeInfo.active_v,
                currentModeInfo.refresh_rate);

        // Compare two HDMI modes in order of height, width, refresh rate and
        // aspect ratio.
        if (currentModeInfo.active_v > bestModeInfo.active_v) {
            bestConfigIndex = configIndex;
        } else if (currentModeInfo.active_v == bestModeInfo.active_v) {
            if (currentModeInfo.active_h > bestModeInfo.active_h) {
                bestConfigIndex = configIndex;
            } else if (currentModeInfo.active_h == bestModeInfo.active_h) {
                if (currentModeInfo.refresh_rate > bestModeInfo.refresh_rate) {
                    bestConfigIndex = configIndex;
                } else if (currentModeInfo.refresh_rate ==
                        bestModeInfo.refresh_rate) {
                    if (currentModeInfo.ar > bestModeInfo.ar) {
                        bestConfigIndex = configIndex;
                    }
                }
            }
        }
        if (bestConfigIndex == configIndex) {
            bestModeInfo = mDisplayConfigs[bestConfigIndex];
        }
    }
    return bestConfigIndex;
}

// Utility function used to request HDMI driver to write a new page of timing
// info into res_info node
void HDMIDisplay::requestNewPage(int pageNumber) {
    char pageString[PAGE_SIZE];
    int fd = openDeviceNode("res_info", O_WRONLY);
    if (fd >= 0) {
        snprintf(pageString, sizeof(pageString), "%d", pageNumber);
        ALOGD_IF(DEBUG, "%s: page=%s", __FUNCTION__, pageString);
        ssize_t err = write(fd, pageString, sizeof(pageString));
        if (err <= 0) {
            ALOGE("%s: Write to res_info failed (%d)", __FUNCTION__, errno);
        }
        close(fd);
    }
}

// Reads the contents of res_info node into a buffer if the file is not empty
bool HDMIDisplay::readResFile(char * configBuffer) {
    bool fileRead = false;
    size_t bytesRead = 0;
    int fd = openDeviceNode("res_info", O_RDONLY);
    if (fd >= 0 && (bytesRead = read(fd, configBuffer, PAGE_SIZE)) != 0) {
        fileRead = true;
    }
    close(fd);
    ALOGD_IF(DEBUG, "%s: bytesRead=%d fileRead=%d",
            __FUNCTION__, bytesRead, fileRead);
    return fileRead;
}

// Populates the internal timing info structure with the timing info obtained
// from the HDMI driver
void HDMIDisplay::readConfigs() {
    int configIndex = 0;
    int pageNumber = MSM_HDMI_INIT_RES_PAGE;
    long unsigned int size = sizeof(msm_hdmi_mode_timing_info);

    while (true) {
        char configBuffer[PAGE_SIZE] = {0};
        msm_hdmi_mode_timing_info *info =
                (msm_hdmi_mode_timing_info*) configBuffer;

        if (!readResFile(configBuffer))
            break;

        while (info->video_format && size < PAGE_SIZE) {
            mDisplayConfigs[configIndex] = *info;
            size += sizeof(msm_hdmi_mode_timing_info);
            info++;
            ALOGD_IF(DEBUG, "%s: Config=%d Mode %d: (%dx%d) @ %d",
                    __FUNCTION__, configIndex,
                    mDisplayConfigs[configIndex].video_format,
                    mDisplayConfigs[configIndex].active_h,
                    mDisplayConfigs[configIndex].active_v,
                    mDisplayConfigs[configIndex].refresh_rate);
            configIndex++;
        }
        size = sizeof(msm_hdmi_mode_timing_info);
        // Request HDMI driver to populate res_info with more
        // timing information
        pageNumber++;
        requestNewPage(pageNumber);
    }
}

inline bool HDMIDisplay::isValidMode(int ID)
{
    bool valid = false;
    int modeIndex = getModeIndex(ID);
    if (ID <= 0 || modeIndex < 0 || modeIndex > mModeCount) {
        return false;
    }
    struct msm_hdmi_mode_timing_info* mode = &mDisplayConfigs[modeIndex];
    // We dont support interlaced modes
    if (mode->supported && !mode->interlaced) {
        valid = true;
    }
    return valid;
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

    struct msm_hdmi_mode_timing_info *mode = &mDisplayConfigs[mActiveConfig];
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
    uint32_t refresh = 0, fps = 0;
    // Always set dpyAttr res to mVInfo res
    getAttrForConfig(mActiveConfig, mXres, mYres, refresh, fps);
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
    return (mFbNum == HWC_DISPLAY_PRIMARY);
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

int HDMIDisplay::setActiveConfig(int newConfig) {
    if(newConfig < 0 || newConfig > mModeCount) {
        ALOGE("%s Invalid configuration %d", __FUNCTION__, newConfig);
        return -EINVAL;
    }

    // XXX: Currently, we only support a change in frame rate.
    // We need to validate the new config before proceeding.
    if (!isValidConfigChange(newConfig)) {
        ALOGE("%s Invalid configuration %d", __FUNCTION__, newConfig);
        return -EINVAL;
    }

    mCurrentMode =  mEDIDModes[newConfig];
    mActiveConfig = newConfig;
    activateDisplay();
    ALOGD("%s config(%d) mode(%d)", __FUNCTION__, mActiveConfig, mCurrentMode);
    return 0;
}

static const char* getS3DStringFromMode(int s3dMode) {
    const char* ret ;
    switch(s3dMode) {
    case HDMI_S3D_NONE:
        ret = "None";
        break;
    case HDMI_S3D_SIDE_BY_SIDE:
        ret = "SSH";
        break;
    case HDMI_S3D_TOP_AND_BOTTOM:
        ret = "TAB";
        break;
    //FP (FramePacked) mode is not supported in the HAL
    default:
        ALOGD("%s: Unsupported s3d mode: %d", __FUNCTION__, s3dMode);
        ret = NULL;
    }
    return ret;
}

bool HDMIDisplay::isS3DModeSupported(int s3dMode) {
    if(s3dMode == HDMI_S3D_NONE)
        return true;

    char s3dEdidStr[PAGE_SIZE] = {'\0'};

    const char *s3dModeString = getS3DStringFromMode(s3dMode);

    if(s3dModeString == NULL)
        return false;

    int s3dEdidNode = openDeviceNode("edid_3d_modes", O_RDONLY);
    if(s3dEdidNode >= 0) {
        ssize_t len = read(s3dEdidNode, s3dEdidStr, sizeof(s3dEdidStr)-1);
        if (len > 0) {
            ALOGI("%s: s3dEdidStr: %s mCurrentMode:%d", __FUNCTION__,
                    s3dEdidStr, mCurrentMode);
            //Three level inception!
            //The string looks like 16=SSH,4=FP:TAB:SSH,5=FP:SSH,32=FP:TAB:SSH
            char *saveptr_l1, *saveptr_l2, *saveptr_l3;
            char *l1, *l2, *l3;
            int mode = 0;
            l1 = strtok_r(s3dEdidStr,",", &saveptr_l1);
            while (l1 != NULL) {
                l2 = strtok_r(l1, "=", &saveptr_l2);
                if (l2 != NULL)
                    mode = atoi(l2);
                while (l2 != NULL) {
                    if (mode != mCurrentMode) {
                        break;
                    }
                    l3 = strtok_r(l2, ":", &saveptr_l3);
                    while (l3 != NULL) {
                        if (strncmp(l3, s3dModeString,
                                    strlen(s3dModeString)) == 0) {
                            close(s3dEdidNode);
                            return true;
                        }
                        l3 = strtok_r(NULL, ":", &saveptr_l3);
                    }
                    l2 = strtok_r(NULL, "=", &saveptr_l2);
                }
                l1 = strtok_r(NULL, ",", &saveptr_l1);
            }

        }
    } else {
        ALOGI("%s: /sys/class/graphics/fb%d/edid_3d_modes could not be opened : %s",
                __FUNCTION__, mFbNum, strerror(errno));
    }
    close(s3dEdidNode);
    return false;
}

bool HDMIDisplay::writeS3DMode(int s3dMode) {
  bool ret = true;
    if(mFbNum != -1) {
        int hdmiS3DModeFile = openDeviceNode("s3d_mode", O_RDWR);
        if(hdmiS3DModeFile >=0 ) {
            char curModeStr[PROPERTY_VALUE_MAX];
            int currentS3DMode = -1;
            size_t len = read(hdmiS3DModeFile, curModeStr, sizeof(curModeStr) - 1);
            if(len > 0) {
                currentS3DMode = atoi(curModeStr);
            } else {
                ret = false;
                ALOGE("%s: Failed to read s3d_mode", __FUNCTION__);
            }

            if (currentS3DMode >=0 && currentS3DMode != s3dMode) {
                ssize_t err = -1;
                ALOGD_IF(DEBUG, "%s: mode = %d",
                        __FUNCTION__, s3dMode);
                char mode[PROPERTY_VALUE_MAX];
                snprintf(mode,sizeof(mode),"%d",s3dMode);
                err = write(hdmiS3DModeFile, mode, sizeof(mode));
                if (err <= 0) {
                    ALOGE("%s: file write failed 's3d_mode'", __FUNCTION__);
                    ret = false;
                }
            }
            close(hdmiS3DModeFile);
        }
    }
    return ret;
}

bool HDMIDisplay::configure3D(int s3dMode) {
    if(isS3DModeSupported(s3dMode)) {
        if(!writeS3DMode(s3dMode))
            return false;
    } else {
        ALOGE("%s: 3D mode: %d is not supported", __FUNCTION__, s3dMode);
        return false;
    }
    return true;
}

// returns false if the xres or yres of the new config do
// not match the current config
bool HDMIDisplay::isValidConfigChange(int newConfig) {
    int newMode = mEDIDModes[newConfig];
    uint32_t width = 0, height = 0, refresh = 0, fps = 0;
    getAttrForConfig(newConfig, width, height, refresh, fps);
    return ((mXres == width) && (mYres == height)) || mEnableResolutionChange;
}

int HDMIDisplay::getModeIndex(int mode) {
    int modeIndex = -1;
    for(int i = 0; i < mModeCount; i++) {
        if(mode == mEDIDModes[i]) {
            modeIndex = i;
            break;
        }
    }
    return modeIndex;
}

int HDMIDisplay::getAttrForConfig(int config, uint32_t& xres,
        uint32_t& yres, uint32_t& refresh, uint32_t& fps) const {
    if(config < 0 || config > mModeCount) {
        ALOGE("%s Invalid configuration %d", __FUNCTION__, config);
        return -EINVAL;
    }

    xres = mDisplayConfigs[config].active_h;
    yres = mDisplayConfigs[config].active_v;
    fps = (mDisplayConfigs[config].refresh_rate / 1000);

    refresh = (uint32_t) 1000000000l / fps;
    ALOGD_IF(DEBUG, "%s xres(%d) yres(%d) fps(%d) refresh(%d)", __FUNCTION__,
            xres, yres, fps, refresh);
    return 0;
}

int HDMIDisplay::getDisplayConfigs(uint32_t* configs,
        size_t* numConfigs) const {
    if (*numConfigs <= 0) {
        ALOGE("%s Invalid number of configs (%d)", __FUNCTION__, *numConfigs);
        return -EINVAL;
    }
    *numConfigs = mModeCount;
    for (int configIndex = 0; configIndex < mModeCount; configIndex++) {
        configs[configIndex] = (uint32_t)configIndex;
    }
    return 0;
}

};
