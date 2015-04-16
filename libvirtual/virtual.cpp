/*
* Copyright (c) 2013 The Linux Foundation. All rights reserved.
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
#include "virtual.h"
#include "overlayUtils.h"
#include "overlay.h"
#include "mdp_version.h"
#include "qd_utils.h"

using namespace android;
using namespace qdutils;

namespace qhwc {

int VirtualDisplay::configure() {
    if(!openFrameBuffer())
        return -1;

    if(ioctl(mFd, FBIOGET_VSCREENINFO, &mVInfo) < 0) {
        ALOGD("%s: FBIOGET_VSCREENINFO failed with %s", __FUNCTION__,
                strerror(errno));
        return -1;
    }
    setAttributes();
    return 0;
}

void VirtualDisplay::getAttributes(uint32_t& width, uint32_t& height) {
    width = mVInfo.xres;
    height = mVInfo.yres;
}

int VirtualDisplay::teardown() {
    closeFrameBuffer();
    memset(&mVInfo, 0, sizeof(mVInfo));
    // Reset the resolution when we close the fb for this device. We need
    // this to distinguish between an ONLINE and RESUME event.
    mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].xres = 0;
    mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].yres = 0;
    mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].secure = 0;
    return 0;
}

VirtualDisplay::VirtualDisplay(hwc_context_t* ctx):mFd(-1),
     mHwcContext(ctx)
{
    memset(&mVInfo, 0, sizeof(mVInfo));
}

VirtualDisplay::~VirtualDisplay()
{
    closeFrameBuffer();
}

/* Initializes the resolution attributes of the virtual display
   that are reported to SurfaceFlinger.
   Cases:
       1. ONLINE event - initialize to frame buffer resolution
       2. RESUME event - retain original resolution
*/
void VirtualDisplay::initResolution(uint32_t &extW, uint32_t &extH) {
    // On ONLINE event, display resolution attributes are 0.
    if(extW == 0 || extH == 0){
        extW = mVInfo.xres;
        extH = mVInfo.yres;
    }
}

/* Sets the virtual resolution to match that of the primary
   display in the event that the virtual display currently
   connected has a lower resolution. NB: we always report the
   highest available resolution to SurfaceFlinger.
*/
void VirtualDisplay::setToPrimary(uint32_t maxArea,
                                  uint32_t priW,
                                  uint32_t priH,
                                  uint32_t &extW,
                                  uint32_t &extH) {
    // for eg., primary in 1600p and WFD in 1080p
    // we wont use downscale feature because MAX MDP
    // writeback resolution supported is 1080p (tracked
    // by SUPPORTED_DOWNSCALE_AREA).
    if((maxArea == (priW * priH))
        && (maxArea <= SUPPORTED_DOWNSCALE_AREA)) {
        // tmpW and tmpH will hold the primary dimensions before we
        // update the aspect ratio if necessary.
        uint32_t tmpW = priW;
        uint32_t tmpH = priH;
        // If WFD is in landscape, assign the higher dimension
        // to WFD's xres.
        if(priH > priW) {
            tmpW = priH;
            tmpH = priW;
        }
        // The aspect ratios of the external and primary displays
        // can be different. As a result, directly assigning primary
        // resolution could lead to an incorrect final image.
        // We get around this by calculating a new resolution by
        // keeping aspect ratio intact.
        hwc_rect r = {0, 0, 0, 0};
        qdutils::getAspectRatioPosition(tmpW, tmpH, extW, extH, r);
        extW = r.right - r.left;
        extH = r.bottom - r.top;
    }
}

/* Set External Display MDP Downscale mode indicator. Only set to
   TRUE for the following scenarios:
   1. Valid DRC scenarios i.e. when the original WFD resolution
      is greater than the new/requested resolution in mVInfo.
   2. WFD down scale path i.e. when WFD resolution is lower than
      primary resolution.
   Furthermore, downscale mode is only valid when downscaling from
   SUPPORTED_DOWNSCALE_AREA to a lower resolution.
   (SUPPORTED_DOWNSCALE_AREA represents the maximum resolution that
   we can configure to the virtual display)
*/
void VirtualDisplay::setDownScaleMode(uint32_t maxArea) {
    if((maxArea > (mVInfo.xres * mVInfo.yres))
        && (maxArea <= SUPPORTED_DOWNSCALE_AREA)) {
        mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].mDownScaleMode = true;
    }else {
        mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].mDownScaleMode = false;
    }
}

bool VirtualDisplay::isSinkSecure() {
    char sysFsPath[MAX_SYSFS_FILE_PATH];
    bool ret = false;
    int fbNum = overlay::Overlay::getInstance()->
        getFbForDpy(HWC_DISPLAY_VIRTUAL);
    snprintf(sysFsPath, sizeof(sysFsPath),
             "/sys/devices/virtual/graphics/fb%d/"
             "secure", fbNum);

    int fileFd = open(sysFsPath, O_RDONLY, 0);
    if (fileFd < 0) {
        ALOGE("In %s: file '%s' not found", __FUNCTION__, sysFsPath);
        ret = false;
    } else {
        char buf;
        ssize_t err = read(fileFd, &buf, 1);
        if (err <= 0) {
            ALOGE("%s: empty file '%s'", __FUNCTION__, sysFsPath);
        } else {
            if (buf == '1') {
                // HDCP Supported: secure
                ret = true;
            } else {
                // NonHDCP: non-secure
                ret = false;
            }
        }
        close(fileFd);
    }
    return ret;
}

void VirtualDisplay::setAttributes() {
    if(mHwcContext) {
        uint32_t &extW = mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].xres;
        uint32_t &extH = mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].yres;
        uint32_t priW = mHwcContext->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
        uint32_t priH = mHwcContext->dpyAttr[HWC_DISPLAY_PRIMARY].yres;
        bool &secure = mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].secure;

        // Dynamic Resolution Change depends on MDP downscaling.
        // MDP downscale property will be ignored to exercise DRC use case.
        // If DRC is in progress, ext WxH will have non-zero values.
        bool isDRC = (extW > mVInfo.xres) && (extH > mVInfo.yres);

        initResolution(extW, extH);

        if(!qdutils::MDPVersion::getInstance().is8x26()
                && (mHwcContext->mMDPDownscaleEnabled || isDRC)) {

            // maxArea represents the maximum resolution between
            // primary and virtual display.
            uint32_t maxArea = max((extW * extH), (priW * priH));

            setToPrimary(maxArea, priW, priH, extW, extH);

            setDownScaleMode(maxArea);
        }
        //Initialize the display viewFrame info
        mHwcContext->mViewFrame[HWC_DISPLAY_VIRTUAL].left = 0;
        mHwcContext->mViewFrame[HWC_DISPLAY_VIRTUAL].top = 0;
        mHwcContext->mViewFrame[HWC_DISPLAY_VIRTUAL].right =
            (int)mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].xres;
        mHwcContext->mViewFrame[HWC_DISPLAY_VIRTUAL].bottom =
            (int)mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].yres;
        mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].vsync_period =
                1000000000l /60;
        if(mHwcContext->mVirtualonExtActive) {
            //For WFD using V4l2 read the sysfs node to determine
            //if the sink is secure
            secure = isSinkSecure();
        }
        ALOGD_IF(DEBUG,"%s: Setting Virtual Attr: res(%d x %d)",__FUNCTION__,
                 mVInfo.xres, mVInfo.yres);
    }
}

bool VirtualDisplay::openFrameBuffer()
{
    if (mFd == -1) {
        int fbNum = overlay::Overlay::getInstance()->
                                   getFbForDpy(HWC_DISPLAY_VIRTUAL);

        char strDevPath[qdutils::MAX_SYSFS_FILE_PATH];
        snprintf(strDevPath,sizeof(strDevPath), "/dev/graphics/fb%d", fbNum);

        mFd = open(strDevPath, O_RDWR);
        if(mFd < 0) {
            ALOGE("%s: Unable to open %s ", __FUNCTION__,strDevPath);
            return -1;
        }

        mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].fd = mFd;
    }
    return 1;
}

bool VirtualDisplay::closeFrameBuffer()
{
    if(mFd >= 0) {
        if(close(mFd) < 0 ) {
            ALOGE("%s: Unable to close FD(%d)", __FUNCTION__, mFd);
            return -1;
        }
        mFd = -1;
        mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].fd = mFd;
    }
    return 1;
}
};
