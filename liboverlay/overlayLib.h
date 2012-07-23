/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010-2012, Code Aurora Forum. All rights reserved.
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
#include <utils/threads.h>
#include <utils/RefBase.h>
#include <alloc_controller.h>
#include <memalloc.h>

#ifdef USES_POST_PROCESSING
#include "lib-postproc.h"
#endif

#define HW_OVERLAY_MAGNIFICATION_LIMIT 8
#define HW_OVERLAY_MINIFICATION_LIMIT HW_OVERLAY_MAGNIFICATION_LIMIT

#define EVEN_OUT(x) if (x & 0x0001) {x--;}
#define NO_PIPE -1
#define VG0_PIPE 0
#define VG1_PIPE 1
#define NUM_CHANNELS 2
#define NUM_FB_DEVICES 3
#define FRAMEBUFFER_0 0
#define FRAMEBUFFER_1 1
#define FRAMEBUFFER_2 2

// To extract the src buffer transform
#define SHIFT_SRC_TRANSFORM 4
#define SRC_TRANSFORM_MASK  0x00F0
#define FINAL_TRANSFORM_MASK 0x000F


#define NUM_SHARPNESS_VALS 256
#define SHARPNESS_RANGE 1.0f
#define HUE_RANGE 180
#define BRIGHTNESS_RANGE 255
#define CON_SAT_RANGE 1.0f
#define CAP_RANGE(value,max,min) do { if (value - min < -0.0001)\
                                          {value = min;}\
                                      else if(value - max > 0.0001)\
                                          {value = max;}\
                                    } while(0);

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

enum {
    WAIT_FOR_VSYNC             = 1<<0,
    DISABLE_FRAMEBUFFER_FETCH  = 1<<1,
    INTERLACED_CONTENT         = 1<<2,
    OVERLAY_PIPE_SHARE         = 1<<3,
    SECURE_OVERLAY_SESSION     = 1<<4,
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

using android::Mutex;
namespace overlay {

#define FB_DEVICE_TEMPLATE "/dev/graphics/fb%u"

    //Utility Class to query the framebuffer info
    class FrameBufferInfo {
        int mFBWidth;
        int mFBHeight;
        bool mBorderFillSupported;
        static FrameBufferInfo *sFBInfoInstance;

        FrameBufferInfo():mFBWidth(0),mFBHeight(0), mBorderFillSupported(false) {
            char const * const device_name =
                       "/dev/graphics/fb0";
            int fd = open(device_name, O_RDWR, 0);
            mdp_overlay ov;
            memset(&ov, 0, sizeof(ov));
            if (fd < 0) {
               LOGE("FrameBufferInfo: Cant open framebuffer ");
               return;
            }
            fb_var_screeninfo vinfo;
            if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
                  LOGE("FrameBufferInfo: FBIOGET_VSCREENINFO on fb0 failed");
                  close(fd);
                  fd = -1;
                  return;
            }
            ov.id = 1;
            if(ioctl(fd, MSMFB_OVERLAY_GET, &ov)) {
                  LOGE("FrameBufferInfo: MSMFB_OVERLAY_GET on fb0 failed");
                  close(fd);
                  fd = -1;
                  return;
            }
            close(fd);
            fd = -1;
            mFBWidth = vinfo.xres;
            mFBHeight = vinfo.yres;
            mBorderFillSupported = (ov.flags & MDP_BORDERFILL_SUPPORTED) ?
                                                              true : false;
        }
        public:
        static FrameBufferInfo* getInstance(){
            if (!sFBInfoInstance){
                sFBInfoInstance = new FrameBufferInfo;
            }
            return sFBInfoInstance;
        }
        int getWidth() const { return mFBWidth; }
        int getHeight() const { return mFBHeight; }
        bool canSupportTrueMirroring() const {
            return mBorderFillSupported; }
    };

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
//Initializes the overlay - cleans up any existing overlay pipes
int initOverlay();

/* Print values being sent to driver in case of ioctl failures
   These logs are enabled only if DEBUG_OVERLAY is true       */
void dump(msm_rotator_img_info& mRotInfo);
void dump(mdp_overlay& mOvInfo);
const char* getFormatString(int format);

    //singleton class to decide the z order of new overlay surfaces
    class ZOrderManager {
        bool mFB0Pipes[NUM_CHANNELS];
        bool mFB1Pipes[NUM_CHANNELS+1]; //FB1 can have 3 pipes
        int  mPipesInuse; // Holds the number of pipes in use
        int  mMaxPipes;   // Max number of pipes
        static ZOrderManager *sInstance;
        Mutex *mObjMutex;
        ZOrderManager(){
            mPipesInuse = 0;
            // for true mirroring support there can be 3 pipes on secondary
            mMaxPipes = FrameBufferInfo::getInstance()->canSupportTrueMirroring()?
                                                  NUM_CHANNELS+1 : NUM_CHANNELS;
            for (int i = 0; i < NUM_CHANNELS; i++)
                mFB0Pipes[i] = false;
            for (int j = 0; j < mMaxPipes; j++)
                mFB1Pipes[j] = false;
            mObjMutex = new Mutex();
        }
        ~ZOrderManager() {
            delete sInstance;
            delete mObjMutex;
        }
        public:
        static ZOrderManager* getInstance(){
            if (!sInstance){
                sInstance = new ZOrderManager;
            }
            return sInstance;
        }
        int getZ(int fbnum);
        void decZ(int fbnum, int zorder);
    };
const int max_num_buffers = 3;
typedef struct mdp_rect overlay_rect;

class OverlayControlChannel {

enum {
    SET_NONE = 0,
    SET_SHARPNESS,
#ifdef USES_POST_PROCESSING
    SET_HUE,
    SET_BRIGHTNESS,
    SET_SATURATION,
    SET_CONTRAST,
#endif
    RESET_ALL,
};
    bool mNoRot;
    int mFBNum;
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
#ifdef USES_POST_PROCESSING
    struct display_pp_conv_cfg hsic_cfg;
#endif
    mdp_overlay mOVInfo;
    msm_rotator_img_info mRotInfo;
    msmfb_overlay_3d m3DOVInfo;
    bool mIsChannelUpdated;
    bool openDevices(int fbnum = -1);
    bool setOverlayInformation(const overlay_buffer_info& info,
                               int zorder = 0, int flags = 0,
                               int requestType = NEW_REQUEST);
    bool startOVRotatorSessions(const overlay_buffer_info& info, int requestType);
    void swapOVRotWidthHeight();
    int commitVisualParam(int8_t paramType, float paramValue);
    void setInformationFromFlags(int flags, mdp_overlay& ov);

public:
    OverlayControlChannel();
    ~OverlayControlChannel();
    bool startControlChannel(const overlay_buffer_info& info,
                               int fbnum, bool norot = false,
                               bool uichannel = false,
                               unsigned int format3D = 0, int zorder = 0,
                               int flags = 0);
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
    bool updateOverlayFlags(int flags);
    bool getAspectRatioPosition(int w, int h, overlay_rect *rect);
    // Calculates the aspect ratio for video on HDMI based on primary
    //  aspect ratio used in case of true mirroring
    bool getAspectRatioPosition(int w, int h, int orientation,
                                overlay_rect *inRect, overlay_rect *outRect);
    bool getPositionS3D(int channel, int format, overlay_rect *rect);
    bool updateOverlaySource(const overlay_buffer_info& info, int flags);
    bool getFormat() const { return mFormat; }
    bool setVisualParam(int8_t paramType, float paramValue);
    bool useVirtualFB ();
    bool doFlagsNeedUpdate(int flags);
};

class OverlayDataChannel {

    bool mNoRot;
    bool mSecure;
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
    bool mUpdateDataChannel;
    int mBufferType;

    bool openDevices(int fbnum = -1, bool uichannel = false, int num_buffers = 2);
    bool mapRotatorMemory(int num_buffers, bool uiChannel, int requestType);
    bool queue(uint32_t offset);
    bool freeRotatorMemory(void *pmemAddr, uint32_t pmemOffset, int pmemFD);

public:
    OverlayDataChannel();
    ~OverlayDataChannel();
    bool startDataChannel(const OverlayControlChannel& objOvCtrlChannel,
                                int fbnum, bool norot = false, bool secure = false,
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
    bool updateDataChannel(int size);
};

/*
 * Overlay class for single thread application
 * A multiple thread/process application need to use Overlay HAL
 */
class Overlay {

    bool mChannelUP;
    //stores the connected external display Ex: HDMI(1) WFD(2)
    int mExternalDisplay;
    unsigned int mS3DFormat;
    //Actual cropped source width and height of overlay
    int mCroppedSrcWidth;
    int mCroppedSrcHeight;
    overlay_buffer_info mOVBufferInfo;
    int mState;
    // Stores the current device orientation
    int mDevOrientation;
    //Store the Actual buffer Orientation
    int mSrcOrientation;
    OverlayControlChannel objOvCtrlChannel[2];
    OverlayDataChannel    objOvDataChannel[2];

public:
    Overlay();
    ~Overlay();

    static bool sHDMIAsPrimary;
    bool startChannel(const overlay_buffer_info& info, int fbnum, bool norot = false,
                          bool uichannel = false, unsigned int format3D = 0,
                          int channel = 0, int flags = 0,
                          int num_buffers = 2);
    bool closeChannel();
    bool setDeviceOrientation(int orientation);
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
    bool setSource(const overlay_buffer_info& info, int orientation, int hdmiConnected,
                    int flags, int numBuffers = 2);
    bool getAspectRatioPosition(int w, int h, overlay_rect *rect, int channel = 0);
    bool setCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    bool updateOverlayFlags(int flags);
    void setVisualParam(int8_t paramType, float paramValue);
    bool waitForHdmiVsync(int channel);
    int  getChannelStatus() const { return (mChannelUP ? OVERLAY_CHANNEL_UP: OVERLAY_CHANNEL_DOWN); }
    void closeExternalChannel();
private:
    bool setChannelPosition(int x, int y, uint32_t w, uint32_t h, int channel = 0);
    bool setChannelCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int channel);
    bool queueBuffer(int fd, uint32_t offset, int channel);
    bool updateOverlaySource(const overlay_buffer_info& info, int flags);
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
