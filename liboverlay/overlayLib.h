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

#define EVEN_OUT(x) if (x & 0x0001) {x--;}
#define VG0_PIPE 0
#define VG1_PIPE 1
#define NUM_CHANNELS 2
#define FRAMEBUFFER_0 0
#define FRAMEBUFFER_1 1

enum {
    HDMI_OFF,
    HDMI_ON
};

enum {
    OVERLAY_CHANNEL_DOWN,
    OVERLAY_CHANNEL_UP
};
/* ------------------------------- 3D defines ---------------------------------------*/
// The compound format passed to the overlay is
// ABCCC where A is the input 3D format,
// B is the output 3D format
// CCC is the color format e.g YCbCr420SP YCrCb420SP etc.
#define FORMAT_3D(x) (x & 0xFF000)
#define COLOR_FORMAT(x) (x & 0xFFF)
// in the final 3D format, the MSB 2Bytes are the input format and the
// LSB 2bytes are the output format. Shift the output byte 12 bits.
#define FORMAT_3D_OUTPUT(x) ((x & 0xF000) >> 12)
#define FORMAT_3D_INPUT(x) (x & 0xF0000)
#define INPUT_MASK_3D         0xFFFF0000
#define OUTPUT_MASK_3D        0x0000FFFF
#define SHIFT_3D              16
// The output format is the 2MSB bytes. Shift the format by 12 to reflect this
#define HAL_3D_OUT_SIDE_BY_SIDE_HALF_MASK       ((HAL_3D_IN_SIDE_BY_SIDE_HALF_L_R|HAL_3D_IN_SIDE_BY_SIDE_HALF_R_L) >> SHIFT_3D)
#define HAL_3D_OUT_SIDE_BY_SIDE_FULL_MASK       (HAL_3D_IN_SIDE_BY_SIDE_FULL >> SHIFT_3D)
#define HAL_3D_OUT_TOP_BOTTOM_MASK              (HAL_3D_OUT_TOP_BOTTOM >> 12)
#define HAL_3D_OUT_INTERLEAVE_MASK              (HAL_3D_OUT_INTERLEAVE >> 12)
#define FORMAT_3D_FILE        "/sys/class/graphics/fb1/format_3d"
/* -------------------------- end 3D defines ----------------------------------------*/

namespace overlay {

const int max_num_buffers = 3;
struct overlay_rect {
    int x;
    int y;
    int width;
    int height;
};

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
    unsigned int mFormat3D;
    bool mUIChannel;
    mdp_overlay mOVInfo;
    msm_rotator_img_info mRotInfo;
    bool openDevices(int fbnum = -1);
    bool setOverlayInformation(int w, int h, int format,
                       int flags, int zorder = 0, bool ignoreFB = false);
    bool startOVRotatorSessions(int w, int h, int format);
    void swapOVRotWidthHeight();

public:
    OverlayControlChannel();
    ~OverlayControlChannel();
    bool startControlChannel(int w, int h, int format,
                               int fbnum, bool norot = false,
                               bool uichannel = false,
                               unsigned int format3D = 0, int zorder = 0,
                               bool ignoreFB = false);
    bool closeControlChannel();
    bool setPosition(int x, int y, uint32_t w, uint32_t h);
    bool setParameter(int param, int value, bool fetch = true);
    bool getPosition(int& x, int& y, uint32_t& w, uint32_t& h);
    bool getOvSessionID(int& sessionID) const;
    bool getRotSessionID(int& sessionID) const;
    bool getSize(int& size) const;
    bool isChannelUP() const { return (mFD > 0); }
    int getFBWidth() const { return mFBWidth; }
    int getFBHeight() const { return mFBHeight; }
    int getFormat3D() const { return mFormat3D; }
    bool getOrientation(int& orientation) const;
    bool setSource(uint32_t w, uint32_t h, int format,
                       int orientation, bool ignoreFB);
    bool getAspectRatioPosition(int w, int h, int format, overlay_rect *rect);
    bool getPositionS3D(int channel, int format, overlay_rect *rect);
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
    int mRotOffset[max_num_buffers];
    int mCurrentItem;
    int mNumBuffers;

    bool openDevices(int fbnum = -1, bool uichannel = false, int num_buffers = 2);

public:
    OverlayDataChannel();
    ~OverlayDataChannel();
    bool startDataChannel(const OverlayControlChannel& objOvCtrlChannel,
                                int fbnum, bool norot = false,
                                bool uichannel = false, int num_buffers = 2);
    bool startDataChannel(int ovid, int rotid, int size,
                       int fbnum, bool norot = false, bool uichannel = false,
                       int num_buffers = 2);
    bool closeDataChannel();
    bool setFd(int fd);
    bool queueBuffer(uint32_t offset);
    bool setCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    bool getCropS3D(overlay_rect *inRect, int channel, int format, overlay_rect *rect);
    bool isChannelUP() const { return (mFD > 0); }
};

/*
 * Overlay class for single thread application
 * A multiple thread/process application need to use Overlay HAL
 */
class Overlay {

    bool mChannelUP;
    bool mHDMIConnected;
    int  mS3DFormat;
    bool mCloseChannel;

    OverlayControlChannel objOvCtrlChannel[2];
    OverlayDataChannel    objOvDataChannel[2];

public:
    Overlay();
    ~Overlay();

    bool startChannel(int w, int h, int format, int fbnum, bool norot = false,
                          bool uichannel = false, unsigned int format3D = 0,
                          int channel = 0, bool ignoreFB = false,
                          int num_buffers = 2);
    bool closeChannel();
    bool setPosition(int x, int y, uint32_t w, uint32_t h);
    bool setParameter(int param, int value);
    bool setOrientation(int value, int channel = 0);
    bool setFd(int fd, int channel = 0);
    bool queueBuffer(uint32_t offset, int channel = 0);
    bool getPosition(int& x, int& y, uint32_t& w, uint32_t& h, int channel = 0);
    bool isChannelUP() const { return mChannelUP; }
    int getFBWidth(int channel = 0) const;
    int getFBHeight(int channel = 0) const;
    bool getOrientation(int& orientation, int channel = 0) const;
    bool queueBuffer(buffer_handle_t buffer);
    bool setSource(uint32_t w, uint32_t h, int format,
                    int orientation, bool hdmiConnected,
                    bool ignoreFB = false, int numBuffers = 2);
    bool setCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    int  getChannelStatus() const { return (mChannelUP ? OVERLAY_CHANNEL_UP: OVERLAY_CHANNEL_DOWN); }
    void setHDMIStatus (bool isHDMIConnected) { mHDMIConnected = isHDMIConnected; }
    int getHDMIStatus() const {return (mHDMIConnected ? HDMI_ON : HDMI_OFF); }

private:
    bool startChannelHDMI(int w, int h, int format, bool norot);
    bool startChannelS3D(int w, int h, int format, bool norot, int s3DFormat);
    bool setPositionS3D(int x, int y, uint32_t w, uint32_t h);
    bool setParameterS3D(int param, int value);
    bool setChannelPosition(int x, int y, uint32_t w, uint32_t h, int channel = 0);
    bool setChannelCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int channel);
    bool queueBuffer(int fd, uint32_t offset, int channel);
};

struct overlay_shared_data {
    int readyToQueue;
};
};
#endif
