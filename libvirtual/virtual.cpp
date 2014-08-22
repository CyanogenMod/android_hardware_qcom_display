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
        // Always set dpyAttr res to mVInfo res
        mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].xres = mVInfo.xres;
        mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].yres = mVInfo.yres;
        mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].mDownScaleMode = false;
        uint32_t priW = mHwcContext->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
        uint32_t priH = mHwcContext->dpyAttr[HWC_DISPLAY_PRIMARY].yres;
        bool &secure = mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].secure;
        // if primary resolution is more than WFD resolution and
        // downscale_factor is zero(which corresponds to downscale
        // to > 50% of orig),then configure dpy attr to primary
        // resolution and set downscale mode.
        if((priW * priH) > (mVInfo.xres * mVInfo.yres)) {
            int downscale_factor = overlay::utils::getDownscaleFactor(priW, priH,
                                   mVInfo.xres, mVInfo.yres);
            if(!downscale_factor) {
                mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].xres = priW;
                mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].yres = priH;
                // WFD is always in landscape, so always assign the higher
                // dimension to wfd's xres
                if(priH > priW) {
                    mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].xres = priH;
                    mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].yres = priW;
                }
                // Set External Display MDP Downscale mode indicator
                mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].mDownScaleMode = true;
            }
        }
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
