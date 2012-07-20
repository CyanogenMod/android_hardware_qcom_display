/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
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

#define EXT_OBSERVER_DEBUG 0
#include <ctype.h>
#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <media/IAudioPolicyService.h>
#include <media/AudioSystem.h>
#include <utils/threads.h>
#include <utils/Errors.h>
#include <utils/Log.h>

#include <linux/msm_mdp.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/poll.h>

#include <hardware_legacy/uevent.h>
#include <cutils/properties.h>
#include "hwc_utils.h"
#include "hwc_ext_observer.h"

namespace qhwc {


#define DEVICE_ROOT "/sys/devices/virtual/graphics"
#define DEVICE_NODE "fb1"

#define SYSFS_CONNECTED         DEVICE_ROOT "/" DEVICE_NODE "/connected"
#define SYSFS_EDID_MODES        DEVICE_ROOT "/" DEVICE_NODE "/edid_modes"
#define SYSFS_HPD               DEVICE_ROOT "/" DEVICE_NODE "/hpd"


android::sp<ExtDisplayObserver> ExtDisplayObserver::
                                         sExtDisplayObserverInstance(0);

ExtDisplayObserver::ExtDisplayObserver() : Thread(false),
    fd(-1), mCurrentID(-1), mHwcContext(NULL)
{
    //Enable HPD for HDMI
    writeHPDOption(1);
}

ExtDisplayObserver::~ExtDisplayObserver() {
    if (fd > 0)
        close(fd);
}

ExtDisplayObserver *ExtDisplayObserver::getInstance() {
    ALOGD_IF(EXT_OBSERVER_DEBUG, "%s ", __FUNCTION__);
    if(sExtDisplayObserverInstance.get() == NULL)
        sExtDisplayObserverInstance = new ExtDisplayObserver();
    return sExtDisplayObserverInstance.get();
}

void ExtDisplayObserver::setHwcContext(hwc_context_t* hwcCtx) {
    ALOGD_IF(EXT_OBSERVER_DEBUG, "%s", __FUNCTION__);
    if(hwcCtx) {
        mHwcContext = hwcCtx;
    }
    return;
}
void ExtDisplayObserver::onFirstRef() {
    ALOGD_IF(EXT_OBSERVER_DEBUG, "%s", __FUNCTION__);
    run("ExtDisplayObserver", ANDROID_PRIORITY_DISPLAY);
}

int ExtDisplayObserver::readyToRun() {
    //Initialize the uevent
    uevent_init();
    ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: success", __FUNCTION__);
    return android::NO_ERROR;
}

void ExtDisplayObserver::handleUEvent(char* str){
    int connected = 0;
    // TODO: check for fb2(WFD) driver also
    if(!strcasestr(str, DEVICE_NODE))
    {
        ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: Not Ext Disp Event ", __FUNCTION__);
        return;
    }
    // Event will be of the form:
    // change@/devices/virtual/graphics/fb1 ACTION=change
    // DEVPATH=/devices/virtual/graphics/fb1
    // SUBSYSTEM=graphics HDCP_STATE=FAIL MAJOR=29
    // for now just parse the online or offline are important for us.
    if(!(strncmp(str,"online@",strlen("online@")))) {
        ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: external disp online", __FUNCTION__);
        connected = 1;
        readResolution();
        //Get the best mode and set
        // TODO: DO NOT call this for WFD
        setResolution(getBestMode());
    } else if(!(strncmp(str,"offline@",strlen("offline@")))) {
        ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: external disp online", __FUNCTION__);
        connected = 0;
        close(fd);
    }
    setExternalDisplayStatus(connected);
}

bool ExtDisplayObserver::threadLoop()
{
    static char uEventString[1024];
    memset(uEventString, 0, sizeof(uEventString));
    int count = uevent_next_event(uEventString, sizeof(uEventString));
    if(count) {
        ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: UeventString: %s len = %d",
                                          __FUNCTION__, uEventString, count);
        handleUEvent(uEventString);
    }
    return true;
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
    info.reserved[3] = video_format;

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
int ExtDisplayObserver::parseResolution(char* edidStr, int* edidModes, int len)
{
    char delim = ',';
    int count = 0;
    char *start, *end;
    // EDIDs are string delimited by ','
    // Ex: 16,4,5,3,32,34,1
    // Parse this string to get mode(int)
    start = (char*) edidStr;
    for(int i=0; i<len; i++) {
        edidModes[i] = (int) strtol(start, &end, 10);
        if(*end != delim) {
            // return as we reached end of string
            return count;
        }
        start = end+1;
        count++;
    }
    return count;
}
bool ExtDisplayObserver::readResolution()
{
    int hdmiEDIDFile = open(SYSFS_EDID_MODES, O_RDONLY, 0);
    int len = -1;

    memset(mEDIDs, 0, sizeof(mEDIDs));
    memset(mEDIDModes, 0, sizeof(mEDIDModes));
    mModeCount = 0;
    if (hdmiEDIDFile < 0) {
        ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: edid_modes file '%s' not found",
                                          __FUNCTION__, SYSFS_EDID_MODES);
        return false;
    } else {
        len = read(hdmiEDIDFile, mEDIDs, sizeof(mEDIDs)-1);
        ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: EDID string: %s length = %d",
                                               __FUNCTION__, mEDIDs, len);
        if ( len <= 0) {
            ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: edid_modes file empty '%s'",
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
        mModeCount = parseResolution(mEDIDs, mEDIDModes, len);
        ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: mModeCount = %d", __FUNCTION__,
                                                                  mModeCount);
    }

    return (strlen(mEDIDs) > 0);
}

bool ExtDisplayObserver::openFramebuffer()
{
    if (fd == -1) {
        fd = open("/dev/graphics/fb1", O_RDWR);
        if (fd < 0)
            ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: /dev/graphics/fb1 not available"
                                               "\n", __FUNCTION__);
    }
    return (fd > 0);
}


int ExtDisplayObserver::getModeOrder(int mode)
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
int ExtDisplayObserver::getBestMode() {
    int bestOrder = 0;
    int bestMode = m640x480p60_4_3;

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

inline bool ExtDisplayObserver::isValidMode(int ID)
{
    return ((ID >= m640x480p60_4_3) && (ID <= m1920x1080p30_16_9));
}

void ExtDisplayObserver::setResolution(int ID)
{
    struct fb_var_screeninfo info;
    if (!openFramebuffer())
        return;
    //If its a valid mode and its a new ID - update var_screeninfo
    if ((isValidMode(ID)) && mCurrentID != ID) {
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
        ioctl(fd, FBIOGET_VSCREENINFO, &info);
        ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: GET Info<ID=%d %dx%d (%d,%d,%d),"
                "(%d,%d,%d) %dMHz>", __FUNCTION__,
                info.reserved[3], info.xres, info.yres,
                info.right_margin, info.hsync_len, info.left_margin,
                info.lower_margin, info.vsync_len, info.upper_margin,
                info.pixclock/1000/1000);
        mode->set_info(info);
        ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: SET Info<ID=%d => Info<ID=%d %dx%d"
                "(%d,%d,%d), (%d,%d,%d) %dMHz>", __FUNCTION__, ID,
                info.reserved[3], info.xres, info.yres,
                info.right_margin, info.hsync_len, info.left_margin,
                info.lower_margin, info.vsync_len, info.upper_margin,
                info.pixclock/1000/1000);
        info.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_ALL | FB_ACTIVATE_FORCE;
        ioctl(fd, FBIOPUT_VSCREENINFO, &info);
        mCurrentID = ID;
    }
    //Powerup
    ioctl(fd, FBIOBLANK, FB_BLANK_UNBLANK);
    ioctl(fd, FBIOGET_VSCREENINFO, &info);
    //Pan_Display
    ioctl(fd, FBIOPAN_DISPLAY, &info);
    property_set("hw.hdmiON", "1");
}


int  ExtDisplayObserver::getExternalDisplay() const
{
 return mExternalDisplay;
}

void ExtDisplayObserver::setExternalDisplayStatus(int connected)
{

    hwc_context_t* ctx = mHwcContext;
    if(ctx) {
        ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: status = %d", __FUNCTION__,
                                                        connected);
        // Store the external display
        mExternalDisplay = connected;//(external_display_type)value;
        //Invalidate
        hwc_procs* proc = (hwc_procs*)ctx->device.reserved_proc[0];
        if(!proc) {
            ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: HWC proc not registered",
                                                                __FUNCTION__);
        } else {
            /* Trigger redraw */
            ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: Invalidate !!", __FUNCTION__);
            proc->invalidate(proc);
        }
    }
    return;
}

bool ExtDisplayObserver::writeHPDOption(int userOption) const
{
    bool ret = true;
    int hdmiHPDFile = open(SYSFS_HPD,O_RDWR, 0);
    if (hdmiHPDFile < 0) {
        ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: state file '%s' not found : ret%d"
        "err str: %s",  __FUNCTION__, SYSFS_HPD, hdmiHPDFile, strerror(errno));
        ret = false;
    } else {
        int err = -1;
        ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: option = %d", __FUNCTION__,
                                                            userOption);
        if(userOption)
            err = write(hdmiHPDFile, "1", 2);
        else
            err = write(hdmiHPDFile, "0" , 2);
        if (err <= 0) {
            ALOGD_IF(EXT_OBSERVER_DEBUG, "%s: file write failed '%s'",
                                                __FUNCTION__, SYSFS_HPD);
            ret = false;
        }
        close(hdmiHPDFile);
    }
    return ret;
}
};

