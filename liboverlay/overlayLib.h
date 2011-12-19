/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
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
#include <utils/RefBase.h>
#include <alloc_controller.h>
#include <memalloc.h>

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

enum {
    NEW_REQUEST,
    UPDATE_REQUEST
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
#define SHIFT_OUTPUT_3D 12
#define FORMAT_3D_OUTPUT(x) ((x & 0xF000) >> SHIFT_OUTPUT_3D)
#define FORMAT_3D_INPUT(x)  (x & 0xF0000)
#define INPUT_MASK_3D  0xFFFF0000
#define OUTPUT_MASK_3D 0x0000FFFF
#define SHIFT_3D       16
// The output format is the 2MSB bytes. Shift the format by 12 to reflect this
#define HAL_3D_OUT_SIDE_BY_SIDE_MASK (HAL_3D_OUT_SIDE_BY_SIDE >> SHIFT_OUTPUT_3D)
#define HAL_3D_OUT_TOP_BOTTOM_MASK   (HAL_3D_OUT_TOP_BOTTOM   >> SHIFT_OUTPUT_3D)
#define HAL_3D_OUT_INTERLEAVE_MASK   (HAL_3D_OUT_INTERLEAVE   >> SHIFT_OUTPUT_3D)
#define HAL_3D_OUT_MONOSCOPIC_MASK   (HAL_3D_OUT_MONOSCOPIC   >> SHIFT_OUTPUT_3D)

// 3D panel barrier orientation
#define BARRIER_LANDSCAPE 1
#define BARRIER_PORTRAIT  2

#ifdef HDMI_AS_PRIMARY
#define FORMAT_3D_FILE        "/sys/class/graphics/fb0/format_3d"
#define EDID_3D_INFO_FILE     "/sys/class/graphics/fb0/3d_present"
#else
#define FORMAT_3D_FILE        "/sys/class/graphics/fb1/format_3d"
#define EDID_3D_INFO_FILE     "/sys/class/graphics/fb1/3d_present"
#endif
#define BARRIER_FILE          "/sys/devices/platform/mipi_novatek.0/enable_3d_barrier"
/* -------------------------- end 3D defines ----------------------------------------*/

// Struct to hold the buffer info: geometry and size
struct overlay_buffer_info {
    int width;
    int height;
    int format;
    int size;
};

/* values for copybit_set_parameter(OVERLAY_TRANSFORM) */
enum {
    /* flip source image horizontally */
    OVERLAY_TRANSFORM_FLIP_H    = HAL_TRANSFORM_FLIP_H,
    /* flip source image vertically */
    OVERLAY_TRANSFORM_FLIP_V    = HAL_TRANSFORM_FLIP_V,
    /* rotate source image 90 degrees */
    OVERLAY_TRANSFORM_ROT_90    = HAL_TRANSFORM_ROT_90,
    /* rotate source image 180 degrees */
    OVERLAY_TRANSFORM_ROT_180   = HAL_TRANSFORM_ROT_180,
    /* rotate source image 270 degrees */
    OVERLAY_TRANSFORM_ROT_270   = HAL_TRANSFORM_ROT_270
};

namespace overlay {

enum {
    OV_UI_MIRROR_TV = 0,
    OV_2D_VIDEO_ON_PANEL,
    OV_2D_VIDEO_ON_TV,
    OV_3D_VIDEO_2D_PANEL,
    OV_3D_VIDEO_2D_TV,
    OV_3D_VIDEO_3D_PANEL,
    OV_3D_VIDEO_3D_TV
};
bool isHDMIConnected();
bool is3DTV();
bool isPanel3D();
bool usePanel3D();
bool send3DInfoPacket(unsigned int format3D);
bool enableBarrier(unsigned int orientation);
unsigned int  getOverlayConfig (unsigned int format3D, bool poll = true,
                                bool isHDMI = false);
int getColorFormat(int format);
bool isInterlacedContent(int format);
int get_mdp_format(int format);
int get_size(int format, int w, int h);
int get_rot_output_format(int format);
int get_mdp_orientation(int value);
void normalize_crop(uint32_t& xy, uint32_t& wh);

/* Print values being sent to driver in case of ioctl failures
   These logs are enabled only if DEBUG_OVERLAY is true       */
void dump(msm_rotator_img_info& mRotInfo);
void dump(mdp_overlay& mOvInfo);
const char* getFormatString(int format);

const int max_num_buffers = 3;
typedef struct mdp_rect overlay_rect;

class OverlayControlChannel {

    bool mNoRot;

    int mFBWidth;
    int mFBHeight;
    int mFBbpp;
    int mFBystride;
    int mFormat;
    int mFD;
    int mRotFD;
    int mSize;
    int mOrientation;
    unsigned int mFormat3D;
    bool mUIChannel;
    mdp_overlay mOVInfo;
    msm_rotator_img_info mRotInfo;
    msmfb_overlay_3d m3DOVInfo;
    bool mIsChannelUpdated;
    bool openDevices(int fbnum = -1);
    bool setOverlayInformation(const overlay_buffer_info& info,
                               int flags, int orientation, int zorder = 0, bool ignoreFB = false,
                               int requestType = NEW_REQUEST);
    bool startOVRotatorSessions(const overlay_buffer_info& info, int orientation, int requestType);
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
    bool setTransform(int value, bool fetch = true);
    void setSize (int size) { mSize = size; }
    bool getPosition(int& x, int& y, uint32_t& w, uint32_t& h);
    bool getOvSessionID(int& sessionID) const;
    bool getRotSessionID(int& sessionID) const;
    bool getSize(int& size) const;
    bool isChannelUP() const { return (mFD > 0); }
    int getFBWidth() const { return mFBWidth; }
    int getFBHeight() const { return mFBHeight; }
    int getFormat3D() const { return mFormat3D; }
    bool getOrientation(int& orientation) const;
    bool updateWaitForVsyncFlags(bool waitForVsync);
    bool getAspectRatioPosition(int w, int h, overlay_rect *rect);
    bool getPositionS3D(int channel, int format, overlay_rect *rect);
    bool updateOverlaySource(const overlay_buffer_info& info, int orientation, bool waitForVsync);
    bool getFormat() const { return mFormat; }
    bool useVirtualFB ();
    int getOverlayFlags() const { return mOVInfo.flags; }
};

class OverlayDataChannel {

    bool mNoRot;
    int mFD;
    int mRotFD;
    int mPmemFD;
    void* mPmemAddr;
    uint32_t mPmemOffset;
    uint32_t mNewPmemOffset;
    msmfb_overlay_data mOvData;
    msmfb_overlay_data mOvDataRot;
    msm_rotator_data_info mRotData;
    int mRotOffset[max_num_buffers];
    int mCurrentItem;
    int mNumBuffers;
    int mUpdateDataChannel;
    android::sp<gralloc::IAllocController> mAlloc;
    int mBufferType;

    bool openDevices(int fbnum = -1, bool uichannel = false, int num_buffers = 2);
    bool mapRotatorMemory(int num_buffers, bool uiChannel, int requestType);
    bool queue(uint32_t offset);

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
    bool waitForHdmiVsync();
    bool setCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    bool getCropS3D(overlay_rect *inRect, int channel, int format, overlay_rect *rect);
    bool isChannelUP() const { return (mFD > 0); }
    bool updateDataChannel(int updateStatus, int size);
};

/*
 * Overlay class for single thread application
 * A multiple thread/process application need to use Overlay HAL
 */
class Overlay {

    bool mChannelUP;
    bool mHDMIConnected;
    unsigned int mS3DFormat;
    //Actual cropped source width and height of overlay
    int mCroppedSrcWidth;
    int mCroppedSrcHeight;
    overlay_buffer_info mOVBufferInfo;
    int mState;
    OverlayControlChannel objOvCtrlChannel[2];
    OverlayDataChannel    objOvDataChannel[2];

public:
    Overlay();
    ~Overlay();

    static bool sHDMIAsPrimary;
    bool startChannel(const overlay_buffer_info& info, int fbnum, bool norot = false,
                          bool uichannel = false, unsigned int format3D = 0,
                          int channel = 0, bool ignoreFB = false,
                          int num_buffers = 2);
    bool closeChannel();
    bool setPosition(int x, int y, uint32_t w, uint32_t h);
    bool setTransform(int value);
    bool setOrientation(int value, int channel = 0);
    bool setFd(int fd, int channel = 0);
    bool queueBuffer(uint32_t offset, int channel = 0);
    bool getPosition(int& x, int& y, uint32_t& w, uint32_t& h, int channel = 0);
    bool isChannelUP() const { return mChannelUP; }
    int getFBWidth(int channel = 0) const;
    int getFBHeight(int channel = 0) const;
    bool getOrientation(int& orientation, int channel = 0) const;
    bool queueBuffer(buffer_handle_t buffer);
    bool setSource(const overlay_buffer_info& info, int orientation, bool hdmiConnected,
                    bool ignoreFB = false, int numBuffers = 2);
    bool setCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    bool waitForHdmiVsync(int channel);
    int  getChannelStatus() const { return (mChannelUP ? OVERLAY_CHANNEL_UP: OVERLAY_CHANNEL_DOWN); }
    void setHDMIStatus (bool isHDMIConnected) { mHDMIConnected = isHDMIConnected; mState = -1; }
    int getHDMIStatus() const {return (mHDMIConnected ? HDMI_ON : HDMI_OFF); }

private:
    bool setChannelPosition(int x, int y, uint32_t w, uint32_t h, int channel = 0);
    bool setChannelCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int channel);
    bool queueBuffer(int fd, uint32_t offset, int channel);
    bool updateOverlaySource(const overlay_buffer_info& info, int orientation, bool waitForVsync);
    int getS3DFormat(int format);
};

struct overlay_shared_data {
    volatile bool isControlSetup;
    unsigned int state;
    int rotid[2];
    int ovid[2];
};
};
#endif
