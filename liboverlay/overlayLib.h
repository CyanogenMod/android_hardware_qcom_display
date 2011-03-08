/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.
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

#ifndef INCLUDE_OVERLAY_LIB
#define INCLUDE_OVERLAY_LIB

#include <cutils/log.h>
#include <cutils/properties.h>
#include <cutils/atomic.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include <linux/fb.h>
#include <linux/msm_mdp.h>
#include <linux/msm_rotator.h>
#include <linux/android_pmem.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <hardware/overlay.h>

#define HW_OVERLAY_MAGNIFICATION_LIMIT 8
#define HW_OVERLAY_MINIFICATION_LIMIT HW_OVERLAY_MAGNIFICATION_LIMIT

namespace overlay {

class OverlayControlChannel {

    bool mNoRot;

    int mFBWidth;
    int mFBHeight;
    int mFBbpp;
    int mFBystride;

    int mFD;
    int mRotFD;
    int mSize;
    int mOrientation;
    mdp_overlay mOVInfo;
    msm_rotator_img_info mRotInfo;
    bool openDevices(int fbnum = -1);
    bool setOverlayInformation(int w, int h, int format, int flags);
    bool startOVRotatorSessions(int w, int h, int format);
    void swapOVRotWidthHeight();

public:
    OverlayControlChannel();
    ~OverlayControlChannel();
    bool startControlChannel(int w, int h, int format,
                               int fbnum, bool norot = false);
    bool closeControlChannel();
    bool setPosition(int x, int y, uint32_t w, uint32_t h);
    bool setParameter(int param, int value);
    bool getPosition(int& x, int& y, uint32_t& w, uint32_t& h);
    bool getOvSessionID(int& sessionID) const;
    bool getRotSessionID(int& sessionID) const;
    bool getSize(int& size) const;
    bool isChannelUP() const { return (mFD > 0); }
    int getFBWidth() const { return mFBWidth; }
    int getFBHeight() const { return mFBHeight; }
    bool getOrientation(int& orientation) const;
    bool setSource(uint32_t w, uint32_t h, int format, int orientation);
};

class OverlayDataChannel {

    bool mNoRot;
    int mFD;
    int mRotFD;
    int mPmemFD;
    void* mPmemAddr;
    uint32_t mPmemOffset;
    msmfb_overlay_data mOvData;
    msmfb_overlay_data mOvDataRot;
    msm_rotator_data_info mRotData;

    bool openDevices(int fbnum = -1, bool uichannel = false);

public:
    OverlayDataChannel();
    ~OverlayDataChannel();
    bool startDataChannel(const OverlayControlChannel& objOvCtrlChannel,
                                int fbnum, bool norot = false, bool uichannel = false);
    bool startDataChannel(int ovid, int rotid, int size,
                       int fbnum, bool norot = false, bool uichannel = false);
    bool closeDataChannel();
    bool setFd(int fd);
    bool queueBuffer(uint32_t offset);
    bool setCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    bool isChannelUP() const { return (mFD > 0); }
};

/*
 * Overlay class for single thread application
 * A multiple thread/process application need to use Overlay HAL
 */
class Overlay {

    bool mChannelUP;

    OverlayControlChannel objOvCtrlChannel;
    OverlayDataChannel    objOvDataChannel;

public:
    Overlay();
    ~Overlay();

    bool startChannel(int w, int h, int format, int fbnum, bool norot = false, bool uichannel = false);
    bool closeChannel();
    bool setPosition(int x, int y, uint32_t w, uint32_t h);
    bool setParameter(int param, int value);
    bool setOrientation(int value);
    bool setFd(int fd);
    bool queueBuffer(uint32_t offset);
    bool getPosition(int& x, int& y, uint32_t& w, uint32_t& h);
    bool isChannelUP() const { return mChannelUP; }
    int getFBWidth() const;
    int getFBHeight() const;
    bool getOrientation(int& orientation) const;
    bool queueBuffer(buffer_handle_t buffer);
    bool setSource(uint32_t w, uint32_t h, int format, int orientation);
    bool setCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
};

struct overlay_shared_data {
    int readyToQueue;
};

};
#endif
