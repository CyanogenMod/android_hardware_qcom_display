/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#include "hwc_utils.h"
#include "hdmi.h"

namespace qhwc {

HDMIDisplay::HDMIDisplay() : mFd(-1), mCurrentMode(-1), mModeCount(0),
        mPrimaryWidth(0), mPrimaryHeight(0), mUnderscanSupported(false) {
    ALOGV("%s stub", __FUNCTION__);
}

HDMIDisplay::~HDMIDisplay() {
    ALOGV("%s stub", __FUNCTION__);
}

void HDMIDisplay::setHPD(uint32_t value) {
    ALOGV("%s stub", __FUNCTION__);
    (void) value;
}

bool HDMIDisplay::isHDMIPrimaryDisplay() {
    ALOGV("%s stub", __FUNCTION__);
    return false;
}

int HDMIDisplay::getConnectedState() {
    ALOGV("%s stub", __FUNCTION__);
    return -1;
}

void HDMIDisplay::activateDisplay() {
    ALOGV("%s stub", __FUNCTION__);
}

void HDMIDisplay::getAttributes(uint32_t& width, uint32_t& height) {
    ALOGV("%s stub", __FUNCTION__);
    width = height = 0;
}

int HDMIDisplay::configure() {
    ALOGV("%s stub", __FUNCTION__);
    return -1;
}

int HDMIDisplay::teardown() {
    ALOGV("%s stub", __FUNCTION__);
    return 0;
}

void HDMIDisplay::setPrimaryAttributes(uint32_t primaryWidth,
                                       uint32_t primaryHeight) {
    ALOGV("%s stub", __FUNCTION__);
    (void) primaryWidth;
    (void) primaryHeight;
}

};
