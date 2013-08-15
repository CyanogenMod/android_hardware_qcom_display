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

int VirtualDisplay::teardown() {
    closeFrameBuffer();
    memset(&mVInfo, 0, sizeof(mVInfo));
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
    mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].xres = mVInfo.xres;
    mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].yres = mVInfo.yres;
    mHwcContext->dpyAttr[HWC_DISPLAY_VIRTUAL].vsync_period =
        1000000000l /60;
    ALOGD_IF(DEBUG,"%s: Setting Virtual Attr: res(%d x %d)",__FUNCTION__,
             mVInfo.xres, mVInfo.yres);
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
