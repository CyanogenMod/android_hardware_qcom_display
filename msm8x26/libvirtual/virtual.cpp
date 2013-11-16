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

using namespace android;

namespace qhwc {

#define MAX_SYSFS_FILE_PATH             255

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

void VirtualDisplay::getAttributes(int& width, int& height) {
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

void VirtualDisplay::setAttributes() {
    if(mHwcContext) {
        unsigned int &w = mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].xres;
        unsigned int &h = mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].yres;

        // Always set dpyAttr res to mVInfo res, only on an ONLINE event. Keep
        // the original configuration to cater for DRC initiated RESUME events
        if(w == 0 || h == 0){
            w = mVInfo.xres;
            h = mVInfo.yres;
        }
        mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].mDownScaleMode = false;

        if(!qdutils::MDPVersion::getInstance().is8x26()) {
            uint32_t priW = mHwcContext->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
            uint32_t priH = mHwcContext->dpyAttr[HWC_DISPLAY_PRIMARY].yres;

            // Find the maximum resolution between primary and virtual
            uint32_t maxArea = max((w * h), (priW * priH));

            // If primary resolution is more than the wfd resolution
            // configure dpy attr to primary resolution and set
            // downscale mode.
            // DRC is only valid when the original resolution on the WiFi
            // display is greater than the new resolution in mVInfo.
            if(maxArea > (mVInfo.xres * mVInfo.yres)) {
                if(maxArea == (priW * priH)) {
                    // Here we account for the case when primary resolution is
                    // greater than that of the WiFi display
                    w = priW;
                    h = priH;
                    // WFD is always in landscape, so always assign the higher
                    // dimension to wfd's xres
                    if(priH > priW) {
                        w = priH;
                        h = priW;
                    }
                }
                // Set External Display MDP Downscale mode indicator
                mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].mDownScaleMode = true;
            }
        }
        mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].vsync_period =
                1000000000l /60;
        ALOGD_IF(DEBUG,"%s: Setting Virtual Attr: res(%d x %d)",__FUNCTION__,
                 mVInfo.xres, mVInfo.yres);
    }
}

bool VirtualDisplay::openFrameBuffer()
{
    if (mFd == -1) {
        int fbNum = overlay::Overlay::getInstance()->
                                   getFbForDpy(HWC_DISPLAY_VIRTUAL);

        char strDevPath[MAX_SYSFS_FILE_PATH];
        sprintf(strDevPath,"/dev/graphics/fb%d", fbNum);

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
