/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2013, The Linux Foundation All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
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
#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)
#define HWC_UTILS_DEBUG 0
#include <math.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <binder/IServiceManager.h>
#include <EGL/egl.h>
#include <cutils/properties.h>
#include <dlfcn.h>
#include <utils/Trace.h>
#include <gralloc_priv.h>
#include <overlay.h>
#include <overlayRotator.h>
#include <overlayWriteback.h>
#include "hwc_utils.h"
#include "hwc_mdpcomp.h"
#include "hwc_fbupdate.h"
#include "hwc_ad.h"
#include "mdp_version.h"
#include "hwc_copybit.h"
#include "hwc_dump_layers.h"
#include "hwc_vpuclient.h"
#include "hdmi.h"
#include "virtual.h"
#include "hwc_qclient.h"
#include "QService.h"
#include "comptype.h"
#include "qd_utils.h"
#include "hwc_virtual.h"

using namespace qClient;
using namespace qdutils;
using namespace qService;
using namespace android;
using namespace overlay;
using namespace overlay::utils;
namespace ovutils = overlay::utils;

#ifdef QTI_BSP

#define EGL_GPU_HINT_1        0x32D0
#define EGL_GPU_HINT_2        0x32D1

#define EGL_GPU_LEVEL_0       0x0
#define EGL_GPU_LEVEL_1       0x1
#define EGL_GPU_LEVEL_2       0x2
#define EGL_GPU_LEVEL_3       0x3
#define EGL_GPU_LEVEL_4       0x4
#define EGL_GPU_LEVEL_5       0x5

#endif

namespace qhwc {

// Std refresh rates for digital videos- 24p, 30p, 48p and 60p
uint32_t stdRefreshRates[] = { 30, 24, 48, 60 };

// external display class state
void updateDisplayInfo(hwc_context_t* ctx, int dpy) {
    ctx->dpyAttr[dpy].fd = ctx->mHDMIDisplay->getFd();
    ctx->dpyAttr[dpy].xres = ctx->mHDMIDisplay->getWidth();
    ctx->dpyAttr[dpy].yres = ctx->mHDMIDisplay->getHeight();
    ctx->dpyAttr[dpy].mDownScaleMode = ctx->mHDMIDisplay->getMDPScalingMode();
    ctx->dpyAttr[dpy].vsync_period = ctx->mHDMIDisplay->getVsyncPeriod();
    ctx->mViewFrame[dpy].left = 0;
    ctx->mViewFrame[dpy].top = 0;
    ctx->mViewFrame[dpy].right = ctx->dpyAttr[dpy].xres;
    ctx->mViewFrame[dpy].bottom = ctx->dpyAttr[dpy].yres;
    //FIXME: for now assume HDMI as secure
    //Will need to read the HDCP status from the driver
    //and update this accordingly
    if (dpy == HWC_DISPLAY_EXTERNAL) {
        ctx->dpyAttr[dpy].secure = true;
    }
}

// Reset hdmi display attributes and list stats structures
void resetDisplayInfo(hwc_context_t* ctx, int dpy) {
    memset(&(ctx->dpyAttr[dpy]), 0, sizeof(ctx->dpyAttr[dpy]));
    memset(&(ctx->listStats[dpy]), 0, sizeof(ctx->listStats[dpy]));
    // We reset the fd to -1 here but External display class is responsible
    // for it when the display is disconnected. This is handled as part of
    // EXTERNAL_OFFLINE event.
    ctx->dpyAttr[dpy].fd = -1;
}

// Initialize composition resources
void initCompositionResources(hwc_context_t* ctx, int dpy) {
    ctx->mFBUpdate[dpy] = IFBUpdate::getObject(ctx, dpy);
    ctx->mMDPComp[dpy] = MDPComp::getObject(ctx, dpy);
}

void destroyCompositionResources(hwc_context_t* ctx, int dpy) {
    if(ctx->mFBUpdate[dpy]) {
        delete ctx->mFBUpdate[dpy];
        ctx->mFBUpdate[dpy] = NULL;
    }
    if(ctx->mMDPComp[dpy]) {
        delete ctx->mMDPComp[dpy];
        ctx->mMDPComp[dpy] = NULL;
    }
}

static int openFramebufferDevice(hwc_context_t *ctx)
{
    struct fb_fix_screeninfo finfo;
    struct fb_var_screeninfo info;

    int fb_fd = openFb(HWC_DISPLAY_PRIMARY);
    if(fb_fd < 0) {
        ALOGE("%s: Error Opening FB : %s", __FUNCTION__, strerror(errno));
        return -errno;
    }

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("%s:Error in ioctl FBIOGET_VSCREENINFO: %s", __FUNCTION__,
                                                       strerror(errno));
        close(fb_fd);
        return -errno;
    }

    if (int(info.width) <= 0 || int(info.height) <= 0) {
        // the driver doesn't return that information
        // default to 160 dpi
        info.width  = ((info.xres * 25.4f)/160.0f + 0.5f);
        info.height = ((info.yres * 25.4f)/160.0f + 0.5f);
    }

    float xdpi = (info.xres * 25.4f) / info.width;
    float ydpi = (info.yres * 25.4f) / info.height;

#ifdef MSMFB_METADATA_GET
    struct msmfb_metadata metadata;
    memset(&metadata, 0 , sizeof(metadata));
    metadata.op = metadata_op_frame_rate;

    if (ioctl(fb_fd, MSMFB_METADATA_GET, &metadata) == -1) {
        ALOGE("%s:Error retrieving panel frame rate: %s", __FUNCTION__,
                                                      strerror(errno));
        close(fb_fd);
        return -errno;
    }

    float fps  = metadata.data.panel_frame_rate;
#else
    //XXX: Remove reserved field usage on all baselines
    //The reserved[3] field is used to store FPS by the driver.
    float fps  = info.reserved[3] & 0xFF;
#endif

    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        ALOGE("%s:Error in ioctl FBIOGET_FSCREENINFO: %s", __FUNCTION__,
                                                       strerror(errno));
        close(fb_fd);
        return -errno;
    }

    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].fd = fb_fd;
    //xres, yres may not be 32 aligned
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].stride = finfo.line_length /(info.xres/8);
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres = info.xres;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres = info.yres;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xdpi = xdpi;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].ydpi = ydpi;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period = 1000000000l / fps;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].secure = true;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].refreshRate = (uint32_t)fps;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].dynRefreshRate = (uint32_t)fps;

    //Unblank primary on first boot
    if(ioctl(fb_fd, FBIOBLANK,FB_BLANK_UNBLANK) < 0) {
        ALOGE("%s: Failed to unblank display", __FUNCTION__);
        return -errno;
    }
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].isActive = true;

    return 0;
}

void initContext(hwc_context_t *ctx)
{
    openFramebufferDevice(ctx);
    char value[PROPERTY_VALUE_MAX];
    ctx->mMDP.version = qdutils::MDPVersion::getInstance().getMDPVersion();
    ctx->mMDP.hasOverlay = qdutils::MDPVersion::getInstance().hasOverlay();
    ctx->mMDP.panel = qdutils::MDPVersion::getInstance().getPanelType();
    overlay::Overlay::initOverlay();
    ctx->mOverlay = overlay::Overlay::getInstance();
    ctx->mRotMgr = RotMgr::getInstance();

    // Initialize composition objects for the primary display
    initCompositionResources(ctx, HWC_DISPLAY_PRIMARY);

    // Check if the target supports copybit compostion (dyn/mdp) to
    // decide if we need to open the copybit module.
    int compositionType =
        qdutils::QCCompositionType::getInstance().getCompositionType();

    // Only MDP copybit is used
    if ((compositionType & (qdutils::COMPOSITION_TYPE_DYN |
            qdutils::COMPOSITION_TYPE_MDP)) &&
            (qdutils::MDPVersion::getInstance().getMDPVersion() ==
            qdutils::MDP_V3_0_4)) {
        ctx->mCopyBit[HWC_DISPLAY_PRIMARY] = new CopyBit(ctx,
                                                         HWC_DISPLAY_PRIMARY);
    }

    ctx->mHDMIDisplay = new HDMIDisplay();
    ctx->mVirtualDisplay = new VirtualDisplay(ctx);
    ctx->mVirtualonExtActive = false;
    // Send the primary resolution to the external display class
    // to be used for MDP scaling functionality
    uint32_t priW = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
    uint32_t priH = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres;
    ctx->mHDMIDisplay->setPrimaryAttributes(priW, priH);
    ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].isActive = false;
    ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].connected = false;
    ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].isActive = false;
    ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].connected = false;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].mDownScaleMode= false;
    ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].mDownScaleMode = false;
    ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].mDownScaleMode = false;

    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].connected = true;
    //Initialize the primary display viewFrame info
    ctx->mViewFrame[HWC_DISPLAY_PRIMARY].left = 0;
    ctx->mViewFrame[HWC_DISPLAY_PRIMARY].top = 0;
    ctx->mViewFrame[HWC_DISPLAY_PRIMARY].right =
        (int)ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
    ctx->mViewFrame[HWC_DISPLAY_PRIMARY].bottom =
         (int)ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres;

    ctx->mVDSEnabled = false;
    if((property_get("persist.hwc.enable_vds", value, NULL) > 0)) {
        if(atoi(value) != 0) {
            ctx->mVDSEnabled = true;
        }
    }
    ctx->mHWCVirtual = HWCVirtualBase::getObject(ctx->mVDSEnabled);

    for (uint32_t i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        ctx->mHwcDebug[i] = new HwcDebug(i);
        ctx->mLayerRotMap[i] = new LayerRotMap();
        ctx->mAnimationState[i] = ANIMATION_STOPPED;
        ctx->dpyAttr[i].mActionSafePresent = false;
        ctx->dpyAttr[i].mAsWidthRatio = 0;
        ctx->dpyAttr[i].mAsHeightRatio = 0;
    }

    for (uint32_t i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        ctx->mPrevHwLayerCount[i] = 0;
    }

    MDPComp::init(ctx);
    ctx->mAD = new AssertiveDisplay(ctx);

    ctx->vstate.enable = false;
    ctx->vstate.fakevsync = false;
    ctx->mExtOrientation = 0;
    ctx->numActiveDisplays = 1;

    //Right now hwc starts the service but anybody could do it, or it could be
    //independent process as well.
    QService::init();
    sp<IQClient> client = new QClient(ctx);
    interface_cast<IQService>(
            defaultServiceManager()->getService(
            String16("display.qservice")))->connect(client);

    // Initialize device orientation to its default orientation
    ctx->deviceOrientation = 0;
    ctx->mBufferMirrorMode = false;
    ctx->mVPUClient = NULL;

    // Read the system property to determine if downscale feature is enabled.
    ctx->mMDPDownscaleEnabled = false;
    if(property_get("sys.hwc.mdp_downscale_enabled", value, "false")
            && !strcmp(value, "true")) {
        ctx->mMDPDownscaleEnabled = true;
    }

#ifdef VPU_TARGET
    ctx->mVPUClient = new VPUClient();
#endif

    ctx->enableABC = false;
    property_get("debug.sf.hwc.canUseABC", value, "0");
    ctx->enableABC  = atoi(value) ? true : false;

    // Initialize gpu perfomance hint related parameters
#ifdef QTI_BSP
    ctx->mEglLib = NULL;
    ctx->mpfn_eglGpuPerfHintQCOM = NULL;
    ctx->mpfn_eglGetCurrentDisplay = NULL;
    ctx->mpfn_eglGetCurrentContext = NULL;
    ctx->mGPUHintInfo.mGpuPerfModeEnable = false;
    ctx->mGPUHintInfo.mEGLDisplay = NULL;
    ctx->mGPUHintInfo.mEGLContext = NULL;
    ctx->mGPUHintInfo.mPrevCompositionGLES = false;
    ctx->mGPUHintInfo.mCurrGPUPerfMode = EGL_GPU_LEVEL_0;
    if(property_get("sys.hwc.gpu_perf_mode", value, "0") > 0) {
        int val = atoi(value);
        if(val > 0 && loadEglLib(ctx)) {
            ctx->mGPUHintInfo.mGpuPerfModeEnable = true;
        }
    }
#endif
    ctx->mUseMetaDataRefreshRate = true;
    if(property_get("persist.metadata_dynfps.disable", value, "false")
            && !strcmp(value, "true")) {
        ctx->mUseMetaDataRefreshRate = false;
    }

    memset(&(ctx->mPtorInfo), 0, sizeof(ctx->mPtorInfo));
    ctx->mHPDEnabled = false;
    ALOGI("Initializing Qualcomm Hardware Composer");
    ALOGI("MDP version: %d", ctx->mMDP.version);
}

void closeContext(hwc_context_t *ctx)
{
    if(ctx->mOverlay) {
        delete ctx->mOverlay;
        ctx->mOverlay = NULL;
    }

    if(ctx->mRotMgr) {
        delete ctx->mRotMgr;
        ctx->mRotMgr = NULL;
    }

    for(int i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        if(ctx->mCopyBit[i]) {
            delete ctx->mCopyBit[i];
            ctx->mCopyBit[i] = NULL;
        }
    }

    if(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].fd) {
        close(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].fd);
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].fd = -1;
    }

    if(ctx->mHDMIDisplay) {
        delete ctx->mHDMIDisplay;
        ctx->mHDMIDisplay = NULL;
    }

#ifdef VPU_TARGET
    if(ctx->mVPUClient) {
        delete ctx->mVPUClient;
    }
#endif

    for(int i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        destroyCompositionResources(ctx, i);

        if(ctx->mHwcDebug[i]) {
            delete ctx->mHwcDebug[i];
            ctx->mHwcDebug[i] = NULL;
        }
        if(ctx->mLayerRotMap[i]) {
            delete ctx->mLayerRotMap[i];
            ctx->mLayerRotMap[i] = NULL;
        }
    }
    if(ctx->mHWCVirtual) {
        delete ctx->mHWCVirtual;
        ctx->mHWCVirtual = NULL;
    }
    if(ctx->mAD) {
        delete ctx->mAD;
        ctx->mAD = NULL;
    }

#ifdef QTI_BSP
    ctx->mpfn_eglGpuPerfHintQCOM = NULL;
    ctx->mpfn_eglGetCurrentDisplay = NULL;
    ctx->mpfn_eglGetCurrentContext = NULL;
    if(ctx->mEglLib) {
        dlclose(ctx->mEglLib);
        ctx->mEglLib = NULL;
    }
#endif
}

//Helper to roundoff the refreshrates
uint32_t roundOff(uint32_t refreshRate) {
    int count =  (int) (sizeof(stdRefreshRates)/sizeof(stdRefreshRates[0]));
    uint32_t rate = refreshRate;
    for(int i=0; i< count; i++) {
        if(abs(stdRefreshRates[i] - refreshRate) < 2) {
            // Most likely used for video, the fps can fluctuate
            // Ex: b/w 29 and 30 for 30 fps clip
            rate = stdRefreshRates[i];
            break;
        }
    }
    return rate;
}

//Helper func to set the dyn fps
void setRefreshRate(hwc_context_t* ctx, int dpy, uint32_t refreshRate) {
    //Update only if different
    if(!ctx || refreshRate == ctx->dpyAttr[dpy].dynRefreshRate)
        return;
    const int fbNum = Overlay::getFbForDpy(dpy);
    char sysfsPath[MAX_SYSFS_FILE_PATH];
    snprintf (sysfsPath, sizeof(sysfsPath),
            "/sys/class/graphics/fb%d/dynamic_fps", fbNum);

    int fd = open(sysfsPath, O_WRONLY);
    if(fd >= 0) {
        char str[64];
        snprintf(str, sizeof(str), "%d", refreshRate);
        ssize_t ret = write(fd, str, strlen(str));
        if(ret < 0) {
            ALOGE("%s: Failed to write %d with error %s",
                    __FUNCTION__, refreshRate, strerror(errno));
        } else {
            ctx->dpyAttr[dpy].dynRefreshRate = refreshRate;
            ALOGD_IF(HWC_UTILS_DEBUG, "%s: Wrote %d to dynamic_fps",
                     __FUNCTION__, refreshRate);
        }
        close(fd);
    } else {
        ALOGE("%s: Failed to open %s with error %s", __FUNCTION__, sysfsPath,
              strerror(errno));
    }
}

void dumpsys_log(android::String8& buf, const char* fmt, ...)
{
    va_list varargs;
    va_start(varargs, fmt);
    buf.appendFormatV(fmt, varargs);
    va_end(varargs);
}

int getExtOrientation(hwc_context_t* ctx) {
    int extOrient = ctx->mExtOrientation;
    if(ctx->mBufferMirrorMode)
        extOrient = getMirrorModeOrientation(ctx);
    return extOrient;
}

/* Calculates the destination position based on the action safe rectangle */
void getActionSafePosition(hwc_context_t *ctx, int dpy, hwc_rect_t& rect) {
    // Position
    int x = rect.left, y = rect.top;
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;

    if(!ctx->dpyAttr[dpy].mActionSafePresent)
        return;
   // Read action safe properties
    int asWidthRatio = ctx->dpyAttr[dpy].mAsWidthRatio;
    int asHeightRatio = ctx->dpyAttr[dpy].mAsHeightRatio;

    float wRatio = 1.0;
    float hRatio = 1.0;
    float xRatio = 1.0;
    float yRatio = 1.0;

    uint32_t fbWidth = ctx->dpyAttr[dpy].xres;
    uint32_t fbHeight = ctx->dpyAttr[dpy].yres;
    if(ctx->dpyAttr[dpy].mDownScaleMode) {
        // if MDP scaling mode is enabled for external, need to query
        // the actual width and height, as that is the physical w & h
         ctx->mHDMIDisplay->getAttributes(fbWidth, fbHeight);
    }


    // Since external is rotated 90, need to swap width/height
    int extOrient = getExtOrientation(ctx);

    if(extOrient & HWC_TRANSFORM_ROT_90)
        swap(fbWidth, fbHeight);

    float asX = 0;
    float asY = 0;
    float asW = fbWidth;
    float asH = fbHeight;

    // based on the action safe ratio, get the Action safe rectangle
    asW = fbWidth * (1.0f -  asWidthRatio / 100.0f);
    asH = fbHeight * (1.0f -  asHeightRatio / 100.0f);
    asX = (fbWidth - asW) / 2;
    asY = (fbHeight - asH) / 2;

    // calculate the position ratio
    xRatio = (float)x/fbWidth;
    yRatio = (float)y/fbHeight;
    wRatio = (float)w/fbWidth;
    hRatio = (float)h/fbHeight;

    //Calculate the position...
    x = (xRatio * asW) + asX;
    y = (yRatio * asH) + asY;
    w = (wRatio * asW);
    h = (hRatio * asH);

    // Convert it back to hwc_rect_t
    rect.left = x;
    rect.top = y;
    rect.right = w + rect.left;
    rect.bottom = h + rect.top;

    return;
}

// This function gets the destination position for Seconday display
// based on the position and aspect ratio with orientation
void getAspectRatioPosition(hwc_context_t* ctx, int dpy, int extOrientation,
                            hwc_rect_t& inRect, hwc_rect_t& outRect) {
    // Physical display resolution
    float fbWidth  = ctx->dpyAttr[dpy].xres;
    float fbHeight = ctx->dpyAttr[dpy].yres;
    //display position(x,y,w,h) in correct aspectratio after rotation
    int xPos = 0;
    int yPos = 0;
    float width = fbWidth;
    float height = fbHeight;
    // Width/Height used for calculation, after rotation
    float actualWidth = fbWidth;
    float actualHeight = fbHeight;

    float wRatio = 1.0;
    float hRatio = 1.0;
    float xRatio = 1.0;
    float yRatio = 1.0;
    hwc_rect_t rect = {0, 0, (int)fbWidth, (int)fbHeight};

    Dim inPos(inRect.left, inRect.top, inRect.right - inRect.left,
                inRect.bottom - inRect.top);
    Dim outPos(outRect.left, outRect.top, outRect.right - outRect.left,
                outRect.bottom - outRect.top);

    Whf whf(fbWidth, fbHeight, 0);
    eTransform extorient = static_cast<eTransform>(extOrientation);
    // To calculate the destination co-ordinates in the new orientation
    preRotateSource(extorient, whf, inPos);

    if(extOrientation & HAL_TRANSFORM_ROT_90) {
        // Swap width/height for input position
        swapWidthHeight(actualWidth, actualHeight);
        qdutils::getAspectRatioPosition((int)fbWidth, (int)fbHeight,
                                (int)actualWidth, (int)actualHeight, rect);
        xPos = rect.left;
        yPos = rect.top;
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
    }
    xRatio = inPos.x/actualWidth;
    yRatio = inPos.y/actualHeight;
    wRatio = inPos.w/actualWidth;
    hRatio = inPos.h/actualHeight;


    //Calculate the position...
    outPos.x = (xRatio * width) + xPos;
    outPos.y = (yRatio * height) + yPos;
    outPos.w = wRatio * width;
    outPos.h = hRatio * height;
    ALOGD_IF(HWC_UTILS_DEBUG, "%s: Calculated AspectRatio Position: x = %d,"
                 "y = %d w = %d h = %d", __FUNCTION__, outPos.x, outPos.y,
                 outPos.w, outPos.h);

    // For sidesync, the dest fb will be in portrait orientation, and the crop
    // will be updated to avoid the black side bands, and it will be upscaled
    // to fit the dest RB, so recalculate
    // the position based on the new width and height
    if ((extOrientation & HWC_TRANSFORM_ROT_90) &&
                        isOrientationPortrait(ctx)) {
        hwc_rect_t r = {0, 0, 0, 0};
        //Calculate the position
        xRatio = (outPos.x - xPos)/width;
        // GetaspectRatio -- tricky to get the correct aspect ratio
        // But we need to do this.
        qdutils::getAspectRatioPosition((int)width, (int)height,
                               (int)width,(int)height, r);
        xPos = r.left;
        yPos = r.top;
        float tempWidth = r.right - r.left;
        float tempHeight = r.bottom - r.top;
        yRatio = yPos/height;
        wRatio = outPos.w/width;
        hRatio = tempHeight/height;

        //Map the coordinates back to Framebuffer domain
        outPos.x = (xRatio * fbWidth);
        outPos.y = (yRatio * fbHeight);
        outPos.w = wRatio * fbWidth;
        outPos.h = hRatio * fbHeight;

        ALOGD_IF(HWC_UTILS_DEBUG, "%s: Calculated AspectRatio for device in"
                 "portrait: x = %d,y = %d w = %d h = %d", __FUNCTION__,
                 outPos.x, outPos.y,
                 outPos.w, outPos.h);
    }
    if(ctx->dpyAttr[dpy].mDownScaleMode) {
        uint32_t extW = 0, extH = 0;
        if(dpy == HWC_DISPLAY_EXTERNAL)
            ctx->mHDMIDisplay->getAttributes(extW, extH);
        else
            ctx->mVirtualDisplay->getAttributes(extW, extH);
        fbWidth  = ctx->dpyAttr[dpy].xres;
        fbHeight = ctx->dpyAttr[dpy].yres;
        //Calculate the position...
        xRatio = outPos.x/fbWidth;
        yRatio = outPos.y/fbHeight;
        wRatio = outPos.w/fbWidth;
        hRatio = outPos.h/fbHeight;

        outPos.x = xRatio * extW;
        outPos.y = yRatio * extH;
        outPos.w = wRatio * extW;
        outPos.h = hRatio * extH;
    }
    // Convert Dim to hwc_rect_t
    outRect.left = outPos.x;
    outRect.top = outPos.y;
    outRect.right = outPos.x + outPos.w;
    outRect.bottom = outPos.y + outPos.h;

    return;
}

bool isPrimaryPortrait(hwc_context_t *ctx) {
    int fbWidth = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
    int fbHeight = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres;
    if(fbWidth < fbHeight) {
        return true;
    }
    return false;
}

bool isOrientationPortrait(hwc_context_t *ctx) {
    if(isPrimaryPortrait(ctx)) {
        return !(ctx->deviceOrientation & 0x1);
    }
    return (ctx->deviceOrientation & 0x1);
}

void calcExtDisplayPosition(hwc_context_t *ctx,
                               private_handle_t *hnd,
                               int dpy,
                               hwc_rect_t& sourceCrop,
                               hwc_rect_t& displayFrame,
                               int& transform,
                               ovutils::eTransform& orient) {
    // Swap width and height when there is a 90deg transform
    int extOrient = getExtOrientation(ctx);
    if(dpy && !qdutils::MDPVersion::getInstance().is8x26()) {
        if(!isYuvBuffer(hnd)) {
            if(extOrient & HWC_TRANSFORM_ROT_90) {
                int dstWidth = ctx->dpyAttr[dpy].xres;
                int dstHeight = ctx->dpyAttr[dpy].yres;;
                int srcWidth = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
                int srcHeight = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres;
                if(!isPrimaryPortrait(ctx)) {
                    swap(srcWidth, srcHeight);
                }                    // Get Aspect Ratio for external
                qdutils::getAspectRatioPosition(dstWidth, dstHeight, srcWidth,
                                    srcHeight, displayFrame);
                // Crop - this is needed, because for sidesync, the dest fb will
                // be in portrait orientation, so update the crop to not show the
                // black side bands.
                if (isOrientationPortrait(ctx)) {
                    sourceCrop = displayFrame;
                    displayFrame.left = 0;
                    displayFrame.top = 0;
                    displayFrame.right = dstWidth;
                    displayFrame.bottom = dstHeight;
                }
            }
            if(ctx->dpyAttr[dpy].mDownScaleMode) {
                uint32_t extW = 0, extH = 0;
                // if MDP scaling mode is enabled, map the co-ordinates to new
                // domain(downscaled)
                float fbWidth  = ctx->dpyAttr[dpy].xres;
                float fbHeight = ctx->dpyAttr[dpy].yres;
                // query MDP configured attributes
                if(dpy == HWC_DISPLAY_EXTERNAL)
                    ctx->mHDMIDisplay->getAttributes(extW, extH);
                else
                    ctx->mVirtualDisplay->getAttributes(extW, extH);
                //Calculate the ratio...
                float wRatio = ((float)extW)/fbWidth;
                float hRatio = ((float)extH)/fbHeight;

                //convert Dim to hwc_rect_t
                displayFrame.left *= wRatio;
                displayFrame.top *= hRatio;
                displayFrame.right *= wRatio;
                displayFrame.bottom *= hRatio;
            }
        }else {
            if(extOrient || ctx->dpyAttr[dpy].mDownScaleMode) {
                getAspectRatioPosition(ctx, dpy, extOrient,
                                       displayFrame, displayFrame);
            }
        }
        // If there is a external orientation set, use that
        if(extOrient) {
            transform = extOrient;
            orient = static_cast<ovutils::eTransform >(extOrient);
        }
        // Calculate the actionsafe dimensions for External(dpy = 1 or 2)
        getActionSafePosition(ctx, dpy, displayFrame);
    }
}

/* Returns the orientation which needs to be set on External for
 *  SideSync/Buffer Mirrormode
 */
int getMirrorModeOrientation(hwc_context_t *ctx) {
    int extOrientation = 0;
    int deviceOrientation = ctx->deviceOrientation;
    if(!isPrimaryPortrait(ctx))
        deviceOrientation = (deviceOrientation + 1) % 4;
     if (deviceOrientation == 0)
         extOrientation = HWC_TRANSFORM_ROT_270;
     else if (deviceOrientation == 1)//90
         extOrientation = 0;
     else if (deviceOrientation == 2)//180
         extOrientation = HWC_TRANSFORM_ROT_90;
     else if (deviceOrientation == 3)//270
         extOrientation = HWC_TRANSFORM_FLIP_V | HWC_TRANSFORM_FLIP_H;

    return extOrientation;
}

/* Get External State names */
const char* getExternalDisplayState(uint32_t external_state) {
    static const char* externalStates[EXTERNAL_MAXSTATES] = {0};
    externalStates[EXTERNAL_OFFLINE] = STR(EXTERNAL_OFFLINE);
    externalStates[EXTERNAL_ONLINE]  = STR(EXTERNAL_ONLINE);
    externalStates[EXTERNAL_PAUSE]   = STR(EXTERNAL_PAUSE);
    externalStates[EXTERNAL_RESUME]  = STR(EXTERNAL_RESUME);

    if(external_state >= EXTERNAL_MAXSTATES) {
        return "EXTERNAL_INVALID";
    }

    return externalStates[external_state];
}

bool isDownscaleRequired(hwc_layer_1_t const* layer) {
    hwc_rect_t displayFrame  = layer->displayFrame;
    hwc_rect_t sourceCrop = integerizeSourceCrop(layer->sourceCropf);
    int dst_w, dst_h, src_w, src_h;
    dst_w = displayFrame.right - displayFrame.left;
    dst_h = displayFrame.bottom - displayFrame.top;
    src_w = sourceCrop.right - sourceCrop.left;
    src_h = sourceCrop.bottom - sourceCrop.top;

    if(((src_w > dst_w) || (src_h > dst_h)))
        return true;

    return false;
}
bool needsScaling(hwc_layer_1_t const* layer) {
    int dst_w, dst_h, src_w, src_h;
    hwc_rect_t displayFrame  = layer->displayFrame;
    hwc_rect_t sourceCrop = integerizeSourceCrop(layer->sourceCropf);

    dst_w = displayFrame.right - displayFrame.left;
    dst_h = displayFrame.bottom - displayFrame.top;
    src_w = sourceCrop.right - sourceCrop.left;
    src_h = sourceCrop.bottom - sourceCrop.top;

    if(((src_w != dst_w) || (src_h != dst_h)))
        return true;

    return false;
}

// Checks if layer needs scaling with split
bool needsScalingWithSplit(hwc_context_t* ctx, hwc_layer_1_t const* layer,
        const int& dpy) {

    int src_width_l, src_height_l;
    int src_width_r, src_height_r;
    int dst_width_l, dst_height_l;
    int dst_width_r, dst_height_r;
    int hw_w = ctx->dpyAttr[dpy].xres;
    int hw_h = ctx->dpyAttr[dpy].yres;
    hwc_rect_t cropL, dstL, cropR, dstR;
    const int lSplit = getLeftSplit(ctx, dpy);
    hwc_rect_t sourceCrop = integerizeSourceCrop(layer->sourceCropf);
    hwc_rect_t displayFrame  = layer->displayFrame;
    private_handle_t *hnd = (private_handle_t *)layer->handle;

    cropL = sourceCrop;
    dstL = displayFrame;
    hwc_rect_t scissorL = { 0, 0, lSplit, hw_h };
    scissorL = getIntersection(ctx->mViewFrame[dpy], scissorL);
    qhwc::calculate_crop_rects(cropL, dstL, scissorL, 0);

    cropR = sourceCrop;
    dstR = displayFrame;
    hwc_rect_t scissorR = { lSplit, 0, hw_w, hw_h };
    scissorR = getIntersection(ctx->mViewFrame[dpy], scissorR);
    qhwc::calculate_crop_rects(cropR, dstR, scissorR, 0);

    // Sanitize Crop to stitch
    sanitizeSourceCrop(cropL, cropR, hnd);

    // Calculate the left dst
    dst_width_l = dstL.right - dstL.left;
    dst_height_l = dstL.bottom - dstL.top;
    src_width_l = cropL.right - cropL.left;
    src_height_l = cropL.bottom - cropL.top;

    // check if there is any scaling on the left
    if(((src_width_l != dst_width_l) || (src_height_l != dst_height_l)))
        return true;

    // Calculate the right dst
    dst_width_r = dstR.right - dstR.left;
    dst_height_r = dstR.bottom - dstR.top;
    src_width_r = cropR.right - cropR.left;
    src_height_r = cropR.bottom - cropR.top;

    // check if there is any scaling on the right
    if(((src_width_r != dst_width_r) || (src_height_r != dst_height_r)))
        return true;

    return false;
}

bool isAlphaScaled(hwc_layer_1_t const* layer) {
    if(needsScaling(layer) && isAlphaPresent(layer)) {
        return true;
    }
    return false;
}

bool isAlphaPresent(hwc_layer_1_t const* layer) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(hnd) {
        int format = hnd->format;
        switch(format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
            // In any more formats with Alpha go here..
            return true;
        default : return false;
        }
    }
    return false;
}

static void trimLayer(hwc_context_t *ctx, const int& dpy, const int& transform,
        hwc_rect_t& crop, hwc_rect_t& dst) {
    int hw_w = ctx->dpyAttr[dpy].xres;
    int hw_h = ctx->dpyAttr[dpy].yres;
    if(dst.left < 0 || dst.top < 0 ||
            dst.right > hw_w || dst.bottom > hw_h) {
        hwc_rect_t scissor = {0, 0, hw_w, hw_h };
        scissor = getIntersection(ctx->mViewFrame[dpy], scissor);
        qhwc::calculate_crop_rects(crop, dst, scissor, transform);
    }
}

static void trimList(hwc_context_t *ctx, hwc_display_contents_1_t *list,
        const int& dpy) {
    for(uint32_t i = 0; i < list->numHwLayers - 1; i++) {
        hwc_layer_1_t *layer = &list->hwLayers[i];
        hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);
        int transform = (list->hwLayers[i].flags & HWC_COLOR_FILL) ? 0 :
                list->hwLayers[i].transform;
        trimLayer(ctx, dpy,
                transform,
                (hwc_rect_t&)crop,
                (hwc_rect_t&)list->hwLayers[i].displayFrame);
        layer->sourceCropf.left = crop.left;
        layer->sourceCropf.right = crop.right;
        layer->sourceCropf.top = crop.top;
        layer->sourceCropf.bottom = crop.bottom;
    }
}

void setListStats(hwc_context_t *ctx,
        hwc_display_contents_1_t *list, int dpy) {
    const int prevYuvCount = ctx->listStats[dpy].yuvCount;
    memset(&ctx->listStats[dpy], 0, sizeof(ListStats));
    ctx->listStats[dpy].numAppLayers = list->numHwLayers - 1;
    ctx->listStats[dpy].fbLayerIndex = list->numHwLayers - 1;
    ctx->listStats[dpy].skipCount = 0;
    ctx->listStats[dpy].preMultipliedAlpha = false;
    ctx->listStats[dpy].isSecurePresent = false;
    ctx->listStats[dpy].yuvCount = 0;
    char property[PROPERTY_VALUE_MAX];
    ctx->listStats[dpy].isDisplayAnimating = false;
    ctx->listStats[dpy].roi = ovutils::Dim(0, 0,
                      (int)ctx->dpyAttr[dpy].xres, (int)ctx->dpyAttr[dpy].yres);
    ctx->listStats[dpy].secureUI = false;
    ctx->listStats[dpy].yuv4k2kCount = 0;
    ctx->dpyAttr[dpy].mActionSafePresent = isActionSafePresent(ctx, dpy);
    ctx->listStats[dpy].renderBufIndexforABC = -1;
    ctx->listStats[dpy].refreshRateRequest = ctx->dpyAttr[dpy].refreshRate;
    uint32_t refreshRate = 0;
    qdutils::MDPVersion& mdpHw = qdutils::MDPVersion::getInstance();

    trimList(ctx, list, dpy);
    optimizeLayerRects(ctx, list, dpy);

    for (size_t i = 0; i < (size_t)ctx->listStats[dpy].numAppLayers; i++) {
        hwc_layer_1_t const* layer = &list->hwLayers[i];
        private_handle_t *hnd = (private_handle_t *)layer->handle;

#ifdef QTI_BSP
        if (layer->flags & HWC_SCREENSHOT_ANIMATOR_LAYER) {
            ctx->listStats[dpy].isDisplayAnimating = true;
        }
        if(isSecureDisplayBuffer(hnd)) {
            ctx->listStats[dpy].secureUI = true;
        }
#endif
        // continue if number of app layers exceeds MAX_NUM_APP_LAYERS
        if(ctx->listStats[dpy].numAppLayers > MAX_NUM_APP_LAYERS)
            continue;

        //reset yuv indices
        ctx->listStats[dpy].yuvIndices[i] = -1;
        ctx->listStats[dpy].yuv4k2kIndices[i] = -1;

        if (isSecureBuffer(hnd)) {
            ctx->listStats[dpy].isSecurePresent = true;
        }

        if (isSkipLayer(&list->hwLayers[i])) {
            ctx->listStats[dpy].skipCount++;
        }

        if (UNLIKELY(isYuvBuffer(hnd))) {
            int& yuvCount = ctx->listStats[dpy].yuvCount;
            ctx->listStats[dpy].yuvIndices[yuvCount] = i;
            yuvCount++;

            if(UNLIKELY(is4kx2kYuvBuffer(hnd))){
                int& yuv4k2kCount = ctx->listStats[dpy].yuv4k2kCount;
                ctx->listStats[dpy].yuv4k2kIndices[yuv4k2kCount] = i;
                yuv4k2kCount++;
            }
        }
        if(layer->blending == HWC_BLENDING_PREMULT)
            ctx->listStats[dpy].preMultipliedAlpha = true;

#ifdef DYNAMIC_FPS
        if (!dpy && mdpHw.isDynFpsSupported() && ctx->mUseMetaDataRefreshRate){
            //dyn fps: get refreshrate from metadata
            //Support multiple refresh rates if they are same
            //else set to  default
            MetaData_t *mdata = hnd ? (MetaData_t *)hnd->base_metadata : NULL;
            if (mdata && (mdata->operation & UPDATE_REFRESH_RATE)) {
                // Valid refreshRate in metadata and within the range
                uint32_t rate = roundOff(mdata->refreshrate);
                if((rate >= mdpHw.getMinFpsSupported() &&
                                rate <= mdpHw.getMaxFpsSupported())) {
                    if (!refreshRate) {
                        refreshRate = rate;
                    } else if(refreshRate != rate) {
                        // multiple refreshrate requests, set to default
                        refreshRate = ctx->dpyAttr[dpy].refreshRate;
                    }
                }
            }
        }
#endif
    }
    if(ctx->listStats[dpy].yuvCount > 0) {
        if (property_get("hw.cabl.yuv", property, NULL) > 0) {
            if (atoi(property) != 1) {
                property_set("hw.cabl.yuv", "1");
            }
        }
    } else {
        if (property_get("hw.cabl.yuv", property, NULL) > 0) {
            if (atoi(property) != 0) {
                property_set("hw.cabl.yuv", "0");
            }
        }
    }

    //The marking of video begin/end is useful on some targets where we need
    //to have a padding round to be able to shift pipes across mixers.
    if(prevYuvCount != ctx->listStats[dpy].yuvCount) {
        ctx->mVideoTransFlag = true;
    }

    if(dpy == HWC_DISPLAY_PRIMARY) {
        ctx->mAD->markDoable(ctx, list);
        //Store the requested fresh rate
        ctx->listStats[dpy].refreshRateRequest = refreshRate ?
                                refreshRate : ctx->dpyAttr[dpy].refreshRate;
    }
}


static void calc_cut(double& leftCutRatio, double& topCutRatio,
        double& rightCutRatio, double& bottomCutRatio, int orient) {
    if(orient & HAL_TRANSFORM_FLIP_H) {
        swap(leftCutRatio, rightCutRatio);
    }
    if(orient & HAL_TRANSFORM_FLIP_V) {
        swap(topCutRatio, bottomCutRatio);
    }
    if(orient & HAL_TRANSFORM_ROT_90) {
        //Anti clock swapping
        double tmpCutRatio = leftCutRatio;
        leftCutRatio = topCutRatio;
        topCutRatio = rightCutRatio;
        rightCutRatio = bottomCutRatio;
        bottomCutRatio = tmpCutRatio;
    }
}

bool isSecuring(hwc_context_t* ctx, hwc_layer_1_t const* layer) {
    if((ctx->mMDP.version < qdutils::MDSS_V5) &&
       (ctx->mMDP.version > qdutils::MDP_V3_0) &&
        ctx->mSecuring) {
        return true;
    }
    if (isSecureModePolicy(ctx->mMDP.version)) {
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(ctx->mSecureMode) {
            if (! isSecureBuffer(hnd)) {
                ALOGD_IF(HWC_UTILS_DEBUG,"%s:Securing Turning ON ...",
                         __FUNCTION__);
                return true;
            }
        } else {
            if (isSecureBuffer(hnd)) {
                ALOGD_IF(HWC_UTILS_DEBUG,"%s:Securing Turning OFF ...",
                         __FUNCTION__);
                return true;
            }
        }
    }
    return false;
}

bool isSecureModePolicy(int mdpVersion) {
    if (mdpVersion < qdutils::MDSS_V5)
        return true;
    else
        return false;
}

// returns true if Action safe dimensions are set and target supports Actionsafe
bool isActionSafePresent(hwc_context_t *ctx, int dpy) {
    // if external supports underscan, do nothing
    // it will be taken care in the driver
    // Disable Action safe for 8974 due to HW limitation for downscaling
    // layers with overlapped region
    // Disable Actionsafe for non HDMI displays.
    if(!(dpy == HWC_DISPLAY_EXTERNAL) ||
        qdutils::MDPVersion::getInstance().is8x74v2() ||
        ctx->mHDMIDisplay->isCEUnderscanSupported()) {
        return false;
    }

    char value[PROPERTY_VALUE_MAX];
    // Read action safe properties
    property_get("persist.sys.actionsafe.width", value, "0");
    ctx->dpyAttr[dpy].mAsWidthRatio = atoi(value);
    property_get("persist.sys.actionsafe.height", value, "0");
    ctx->dpyAttr[dpy].mAsHeightRatio = atoi(value);

    if(!ctx->dpyAttr[dpy].mAsWidthRatio && !ctx->dpyAttr[dpy].mAsHeightRatio) {
        //No action safe ratio set, return
        return false;
    }
    return true;
}

int getBlending(int blending) {
    switch(blending) {
    case HWC_BLENDING_NONE:
        return overlay::utils::OVERLAY_BLENDING_OPAQUE;
    case HWC_BLENDING_PREMULT:
        return overlay::utils::OVERLAY_BLENDING_PREMULT;
    case HWC_BLENDING_COVERAGE :
    default:
        return overlay::utils::OVERLAY_BLENDING_COVERAGE;
    }
}

//Crops source buffer against destination and FB boundaries
void calculate_crop_rects(hwc_rect_t& crop, hwc_rect_t& dst,
                          const hwc_rect_t& scissor, int orient) {

    int& crop_l = crop.left;
    int& crop_t = crop.top;
    int& crop_r = crop.right;
    int& crop_b = crop.bottom;
    int crop_w = crop.right - crop.left;
    int crop_h = crop.bottom - crop.top;

    int& dst_l = dst.left;
    int& dst_t = dst.top;
    int& dst_r = dst.right;
    int& dst_b = dst.bottom;
    int dst_w = abs(dst.right - dst.left);
    int dst_h = abs(dst.bottom - dst.top);

    const int& sci_l = scissor.left;
    const int& sci_t = scissor.top;
    const int& sci_r = scissor.right;
    const int& sci_b = scissor.bottom;
    int sci_w = abs(sci_r - sci_l);
    int sci_h = abs(sci_b - sci_t);

    double leftCutRatio = 0.0, rightCutRatio = 0.0, topCutRatio = 0.0,
            bottomCutRatio = 0.0;

    if(dst_l < sci_l) {
        leftCutRatio = (double)(sci_l - dst_l) / (double)dst_w;
        dst_l = sci_l;
    }

    if(dst_r > sci_r) {
        rightCutRatio = (double)(dst_r - sci_r) / (double)dst_w;
        dst_r = sci_r;
    }

    if(dst_t < sci_t) {
        topCutRatio = (double)(sci_t - dst_t) / (double)dst_h;
        dst_t = sci_t;
    }

    if(dst_b > sci_b) {
        bottomCutRatio = (double)(dst_b - sci_b) / (double)dst_h;
        dst_b = sci_b;
    }

    calc_cut(leftCutRatio, topCutRatio, rightCutRatio, bottomCutRatio, orient);
    crop_l += (int)round((double)crop_w * leftCutRatio);
    crop_t += (int)round((double)crop_h * topCutRatio);
    crop_r -= (int)round((double)crop_w * rightCutRatio);
    crop_b -= (int)round((double)crop_h * bottomCutRatio);
}

bool areLayersIntersecting(const hwc_layer_1_t* layer1,
        const hwc_layer_1_t* layer2) {
    hwc_rect_t irect = getIntersection(layer1->displayFrame,
            layer2->displayFrame);
    return isValidRect(irect);
}

bool isValidRect(const hwc_rect& rect)
{
   return ((rect.bottom > rect.top) && (rect.right > rect.left)) ;
}

bool layerUpdating(const hwc_layer_1_t* layer) {
     hwc_region_t surfDamage = layer->surfaceDamage;
     return ((surfDamage.numRects == 0) ||
              isValidRect(layer->surfaceDamage.rects[0]));
}

hwc_rect_t calculateDirtyRect(const hwc_layer_1_t* layer,
                                       hwc_rect_t& scissor) {
    hwc_region_t surfDamage = layer->surfaceDamage;
    hwc_rect_t src = integerizeSourceCrop(layer->sourceCropf);
    hwc_rect_t dst = layer->displayFrame;
    int x_off = dst.left - src.left;
    int y_off = dst.top - src.top;
    hwc_rect dirtyRect = (hwc_rect){0, 0, 0, 0};
    hwc_rect_t updatingRect = dst;

    if (surfDamage.numRects == 0) {
      // full layer updating, dirty rect is full frame
        dirtyRect = getIntersection(layer->displayFrame, scissor);
    } else {
        for(uint32_t i = 0; i < surfDamage.numRects; i++) {
            updatingRect = moveRect(surfDamage.rects[i], x_off, y_off);
            hwc_rect_t intersect = getIntersection(updatingRect, scissor);
            if(isValidRect(intersect)) {
               dirtyRect = getUnion(intersect, dirtyRect);
            }
        }
     }
     return dirtyRect;
}

hwc_rect_t moveRect(const hwc_rect_t& rect, const int& x_off, const int& y_off)
{
    hwc_rect_t res;

    if(!isValidRect(rect))
        return (hwc_rect_t){0, 0, 0, 0};

    res.left = rect.left + x_off;
    res.top = rect.top + y_off;
    res.right = rect.right + x_off;
    res.bottom = rect.bottom + y_off;

    return res;
}

bool operator ==(const hwc_rect_t& lhs, const hwc_rect_t& rhs) {
    if(lhs.left == rhs.left && lhs.top == rhs.top &&
       lhs.right == rhs.right &&  lhs.bottom == rhs.bottom )
          return true ;
    return false;
}

/* computes the intersection of two rects */
hwc_rect_t getIntersection(const hwc_rect_t& rect1, const hwc_rect_t& rect2)
{
   hwc_rect_t res;

   if(!isValidRect(rect1) || !isValidRect(rect2)){
      return (hwc_rect_t){0, 0, 0, 0};
   }


   res.left = max(rect1.left, rect2.left);
   res.top = max(rect1.top, rect2.top);
   res.right = min(rect1.right, rect2.right);
   res.bottom = min(rect1.bottom, rect2.bottom);

   if(!isValidRect(res))
      return (hwc_rect_t){0, 0, 0, 0};

   return res;
}

/* computes the union of two rects */
hwc_rect_t getUnion(const hwc_rect &rect1, const hwc_rect &rect2)
{
   hwc_rect_t res;

   if(!isValidRect(rect1)){
      return rect2;
   }

   if(!isValidRect(rect2)){
      return rect1;
   }

   res.left = min(rect1.left, rect2.left);
   res.top = min(rect1.top, rect2.top);
   res.right =  max(rect1.right, rect2.right);
   res.bottom =  max(rect1.bottom, rect2.bottom);

   return res;
}

/* Not a geometrical rect deduction. Deducts rect2 from rect1 only if it results
 * a single rect */
hwc_rect_t deductRect(const hwc_rect_t& rect1, const hwc_rect_t& rect2) {

   hwc_rect_t res = rect1;

   if((rect1.left == rect2.left) && (rect1.right == rect2.right)) {
      if((rect1.top == rect2.top) && (rect2.bottom <= rect1.bottom))
         res.top = rect2.bottom;
      else if((rect1.bottom == rect2.bottom)&& (rect2.top >= rect1.top))
         res.bottom = rect2.top;
   }
   else if((rect1.top == rect2.top) && (rect1.bottom == rect2.bottom)) {
      if((rect1.left == rect2.left) && (rect2.right <= rect1.right))
         res.left = rect2.right;
      else if((rect1.right == rect2.right)&& (rect2.left >= rect1.left))
         res.right = rect2.left;
   }
   return res;
}

void optimizeLayerRects(hwc_context_t *ctx,
                        const hwc_display_contents_1_t *list, const int& dpy) {
    int i=list->numHwLayers-2;
    hwc_rect_t irect;
    while(i > 0) {

        //see if there is no blending required.
        //If it is opaque see if we can substract this region from below layers.
        if(list->hwLayers[i].blending == HWC_BLENDING_NONE &&
                list->hwLayers[i].planeAlpha == 0xFF) {
            int j= i-1;
            hwc_rect_t& topframe =
                (hwc_rect_t&)list->hwLayers[i].displayFrame;
            while(j >= 0) {
               if(!needsScaling(&list->hwLayers[j])) {
                  hwc_layer_1_t* layer = (hwc_layer_1_t*)&list->hwLayers[j];
                  hwc_rect_t& bottomframe = layer->displayFrame;
                  hwc_rect_t bottomCrop =
                      integerizeSourceCrop(layer->sourceCropf);
                  int transform = (layer->flags & HWC_COLOR_FILL) ? 0 :
                      layer->transform;

                  hwc_rect_t irect = getIntersection(bottomframe, topframe);
                  if(isValidRect(irect)) {
                     hwc_rect_t dest_rect;
                     //if intersection is valid rect, deduct it
                     dest_rect  = deductRect(bottomframe, irect);
                     qhwc::calculate_crop_rects(bottomCrop, bottomframe,
                                                dest_rect, transform);
                     //Update layer sourceCropf
                     layer->sourceCropf.left =(float)bottomCrop.left;
                     layer->sourceCropf.top = (float)bottomCrop.top;
                     layer->sourceCropf.right = (float)bottomCrop.right;
                     layer->sourceCropf.bottom = (float)bottomCrop.bottom;
                  }
               }
               j--;
            }
        }
        i--;
    }
}

void getNonWormholeRegion(hwc_display_contents_1_t* list,
                              hwc_rect_t& nwr)
{
    uint32_t last = list->numHwLayers - 1;
    hwc_rect_t fbDisplayFrame = list->hwLayers[last].displayFrame;
    //Initiliaze nwr to first frame
    nwr.left =  list->hwLayers[0].displayFrame.left;
    nwr.top =  list->hwLayers[0].displayFrame.top;
    nwr.right =  list->hwLayers[0].displayFrame.right;
    nwr.bottom =  list->hwLayers[0].displayFrame.bottom;

    for (uint32_t i = 1; i < last; i++) {
        hwc_rect_t displayFrame = list->hwLayers[i].displayFrame;
        nwr = getUnion(nwr, displayFrame);
    }

    //Intersect with the framebuffer
    nwr = getIntersection(nwr, fbDisplayFrame);
}

bool isExternalActive(hwc_context_t* ctx) {
    return ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].isActive;
}

void closeAcquireFds(hwc_display_contents_1_t* list) {
    if(LIKELY(list)) {
        for(uint32_t i = 0; i < list->numHwLayers; i++) {
            //Close the acquireFenceFds
            //HWC_FRAMEBUFFER are -1 already by SF, rest we close.
            if(list->hwLayers[i].acquireFenceFd >= 0) {
                close(list->hwLayers[i].acquireFenceFd);
                list->hwLayers[i].acquireFenceFd = -1;
            }
        }
        //Writeback
        if(list->outbufAcquireFenceFd >= 0) {
            close(list->outbufAcquireFenceFd);
            list->outbufAcquireFenceFd = -1;
        }
    }
}

int hwc_sync(hwc_context_t *ctx, hwc_display_contents_1_t* list, int dpy,
        int fd) {
    ATRACE_CALL();
    int ret = 0;
    int acquireFd[MAX_NUM_APP_LAYERS];
    int count = 0;
    int releaseFd = -1;
    int retireFd = -1;
    int fbFd = -1;
    bool swapzero = false;
    int mdpVersion = qdutils::MDPVersion::getInstance().getMDPVersion();

    struct mdp_buf_sync data;
    memset(&data, 0, sizeof(data));
    data.acq_fen_fd = acquireFd;
    data.rel_fen_fd = &releaseFd;
    data.retire_fen_fd = &retireFd;
    data.flags = MDP_BUF_SYNC_FLAG_RETIRE_FENCE;

    char property[PROPERTY_VALUE_MAX];
    if(property_get("debug.egl.swapinterval", property, "1") > 0) {
        if(atoi(property) == 0)
            swapzero = true;
    }

    bool isExtAnimating = false;
    if(dpy)
       isExtAnimating = ctx->listStats[dpy].isDisplayAnimating;

    //Send acquireFenceFds to rotator
    for(uint32_t i = 0; i < ctx->mLayerRotMap[dpy]->getCount(); i++) {
        int rotFd = ctx->mRotMgr->getRotDevFd();
        int rotReleaseFd = -1;
        overlay::Rotator* currRot = ctx->mLayerRotMap[dpy]->getRot(i);
        hwc_layer_1_t* currLayer = ctx->mLayerRotMap[dpy]->getLayer(i);
        if((currRot == NULL) || (currLayer == NULL)) {
            continue;
        }
        struct mdp_buf_sync rotData;
        memset(&rotData, 0, sizeof(rotData));
        rotData.acq_fen_fd =
                &currLayer->acquireFenceFd;
        rotData.rel_fen_fd = &rotReleaseFd; //driver to populate this
        rotData.session_id = currRot->getSessId();
        int ret = 0;
        ret = ioctl(rotFd, MSMFB_BUFFER_SYNC, &rotData);
        if(ret < 0) {
            ALOGE("%s: ioctl MSMFB_BUFFER_SYNC failed for rot sync, err=%s",
                    __FUNCTION__, strerror(errno));
        } else {
            close(currLayer->acquireFenceFd);
            //For MDP to wait on.
            currLayer->acquireFenceFd =
                    dup(rotReleaseFd);
            //A buffer is free to be used by producer as soon as its copied to
            //rotator
            currLayer->releaseFenceFd =
                    rotReleaseFd;
        }
    }

    //Accumulate acquireFenceFds for MDP
    if(list->outbufAcquireFenceFd >= 0) {
        //Writeback output buffer
        acquireFd[count++] = list->outbufAcquireFenceFd;
    }

    for(uint32_t i = 0; i < list->numHwLayers; i++) {
        if(((isAbcInUse(ctx)== true ) ||
          (list->hwLayers[i].compositionType == HWC_OVERLAY)) &&
                        list->hwLayers[i].acquireFenceFd >= 0) {
            if(UNLIKELY(swapzero))
                acquireFd[count++] = -1;
            // if ABC is enabled for more than one layer.
            // renderBufIndexforABC will work as FB.Hence
            // set the acquireFD from fd - which is coming from copybit
            else if(fd >= 0 && (isAbcInUse(ctx) == true)) {
                if(ctx->listStats[dpy].renderBufIndexforABC ==(int32_t)i)
                   acquireFd[count++] = fd;
                else
                   continue;
            } else
                acquireFd[count++] = list->hwLayers[i].acquireFenceFd;
        }
        if(list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
            if(UNLIKELY(swapzero))
                acquireFd[count++] = -1;
            else if(fd >= 0) {
                //set the acquireFD from fd - which is coming from c2d
                acquireFd[count++] = fd;
                // Buffer sync IOCTL should be async when using c2d fence is
                // used
                data.flags &= ~MDP_BUF_SYNC_FLAG_WAIT;
            } else if(list->hwLayers[i].acquireFenceFd >= 0)
                acquireFd[count++] = list->hwLayers[i].acquireFenceFd;
        }
    }

    if ((fd >= 0) && !dpy && ctx->mPtorInfo.isActive()) {
        // Acquire c2d fence of Overlap render buffer
        acquireFd[count++] = fd;
    }

    data.acq_fen_fd_cnt = count;
    fbFd = ctx->dpyAttr[dpy].fd;

    //Waits for acquire fences, returns a release fence
    if(LIKELY(!swapzero)) {
        uint64_t start = systemTime();
        ret = ioctl(fbFd, MSMFB_BUFFER_SYNC, &data);
        ALOGD_IF(HWC_UTILS_DEBUG, "%s: time taken for MSMFB_BUFFER_SYNC IOCTL = %d",
                            __FUNCTION__, (size_t) ns2ms(systemTime() - start));
    }

    if(ret < 0) {
        ALOGE("%s: ioctl MSMFB_BUFFER_SYNC failed, err=%s",
                  __FUNCTION__, strerror(errno));
        ALOGE("%s: acq_fen_fd_cnt=%d flags=%d fd=%d dpy=%d numHwLayers=%d",
              __FUNCTION__, data.acq_fen_fd_cnt, data.flags, fbFd,
              dpy, list->numHwLayers);
    }

    for(uint32_t i = 0; i < list->numHwLayers; i++) {
        if(list->hwLayers[i].compositionType == HWC_OVERLAY ||
#ifdef QTI_BSP
           list->hwLayers[i].compositionType == HWC_BLIT ||
#endif
           list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
            //Populate releaseFenceFds.
            if(UNLIKELY(swapzero)) {
                list->hwLayers[i].releaseFenceFd = -1;
            } else if(isExtAnimating) {
                // Release all the app layer fds immediately,
                // if animation is in progress.
                list->hwLayers[i].releaseFenceFd = -1;
            } else if(list->hwLayers[i].releaseFenceFd < 0 ) {
#ifdef QTI_BSP
                //If rotator has not already populated this field
                // & if it's a not VPU layer

                // if ABC is enabled for more than one layer
                if(fd >= 0 && (isAbcInUse(ctx) == true) &&
                  ctx->listStats[dpy].renderBufIndexforABC !=(int32_t)i){
                    list->hwLayers[i].releaseFenceFd = dup(fd);
                } else if((list->hwLayers[i].compositionType == HWC_BLIT)&&
                                               (isAbcInUse(ctx) == false)){
                    //For Blit, the app layers should be released when the Blit
                    //is complete. This fd was passed from copybit->draw
                    list->hwLayers[i].releaseFenceFd = dup(fd);
                } else 
#endif
                {
                    list->hwLayers[i].releaseFenceFd = dup(releaseFd);
                }
            }
        }
    }

    if(fd >= 0) {
        close(fd);
        fd = -1;
    }

    if (ctx->mCopyBit[dpy]) {
        if (!dpy && ctx->mPtorInfo.isActive())
            ctx->mCopyBit[dpy]->setReleaseFdSync(releaseFd);
        else
            ctx->mCopyBit[dpy]->setReleaseFd(releaseFd);
    }

    //Signals when MDP finishes reading rotator buffers.
    ctx->mLayerRotMap[dpy]->setReleaseFd(releaseFd);
    close(releaseFd);
    releaseFd = -1;

    if(UNLIKELY(swapzero)) {
        list->retireFenceFd = -1;
    } else {
        list->retireFenceFd = retireFd;
    }
    return ret;
}

void setMdpFlags(hwc_layer_1_t *layer,
        ovutils::eMdpFlags &mdpFlags,
        int rotDownscale, int transform) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    MetaData_t *metadata = hnd ? (MetaData_t *)hnd->base_metadata : NULL;

    if(layer->blending == HWC_BLENDING_PREMULT) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_BLEND_FG_PREMULT);
    }

    if(isYuvBuffer(hnd)) {
        if(isSecureBuffer(hnd)) {
            ovutils::setMdpFlags(mdpFlags,
                    ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
        }
        if(metadata && (metadata->operation & PP_PARAM_INTERLACED) &&
                metadata->interlaced) {
            ovutils::setMdpFlags(mdpFlags,
                    ovutils::OV_MDP_DEINTERLACE);
        }
        //Pre-rotation will be used using rotator.
        if(transform & HWC_TRANSFORM_ROT_90) {
            ovutils::setMdpFlags(mdpFlags,
                    ovutils::OV_MDP_SOURCE_ROTATED_90);
        }
    }

    if(isSecureDisplayBuffer(hnd)) {
        // Secure display needs both SECURE_OVERLAY and SECURE_DISPLAY_OV
        ovutils::setMdpFlags(mdpFlags,
                             ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
        ovutils::setMdpFlags(mdpFlags,
                             ovutils::OV_MDP_SECURE_DISPLAY_OVERLAY_SESSION);
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_SMP_FORCE_ALLOC);
    }

    if(isSecureBuffer(hnd) || isProtectedBuffer(hnd)) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_SMP_FORCE_ALLOC);
    }
    //No 90 component and no rot-downscale then flips done by MDP
    //If we use rot then it might as well do flips
    if(!(transform & HWC_TRANSFORM_ROT_90) && !rotDownscale) {
        if(transform & HWC_TRANSFORM_FLIP_H) {
            ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_FLIP_H);
        }

        if(transform & HWC_TRANSFORM_FLIP_V) {
            ovutils::setMdpFlags(mdpFlags,  ovutils::OV_MDP_FLIP_V);
        }
    }

    if(metadata &&
        ((metadata->operation & PP_PARAM_HSIC)
        || (metadata->operation & PP_PARAM_IGC)
        || (metadata->operation & PP_PARAM_SHARP2))) {
        ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_PP_EN);
    }
}

int configRotator(Rotator *rot, Whf& whf,
        hwc_rect_t& crop, const eMdpFlags& mdpFlags,
        const eTransform& orient, const int& downscale) {

    //Check if input switched from secure->non-secure OR non-secure->secure
    //Need to fail rotator setup as rotator buffer needs reallocation.
    if(!rot->isRotBufReusable(mdpFlags)) return -1;

    // Fix alignments for TILED format
    if(whf.format == MDP_Y_CRCB_H2V2_TILE ||
                            whf.format == MDP_Y_CBCR_H2V2_TILE) {
        whf.w =  utils::alignup(whf.w, 64);
        whf.h = utils::alignup(whf.h, 32);
    }
    rot->setSource(whf);

    if (qdutils::MDPVersion::getInstance().getMDPVersion() >=
        qdutils::MDSS_V5) {
         Dim rotCrop(crop.left, crop.top, crop.right - crop.left,
                crop.bottom - crop.top);
        rot->setCrop(rotCrop);
    }

    rot->setFlags(mdpFlags);
    rot->setTransform(orient);
    rot->setDownscale(downscale);
    if(!rot->commit()) return -1;
    return 0;
}

int configMdp(Overlay *ov, const PipeArgs& parg,
        const eTransform& orient, const hwc_rect_t& crop,
        const hwc_rect_t& pos, const MetaData_t *metadata,
        const eDest& dest) {
    ov->setSource(parg, dest);
    ov->setTransform(orient, dest);

    int crop_w = crop.right - crop.left;
    int crop_h = crop.bottom - crop.top;
    Dim dcrop(crop.left, crop.top, crop_w, crop_h);
    ov->setCrop(dcrop, dest);

    int posW = pos.right - pos.left;
    int posH = pos.bottom - pos.top;
    Dim position(pos.left, pos.top, posW, posH);
    ov->setPosition(position, dest);

    if (metadata)
        ov->setVisualParams(*metadata, dest);

    if (!ov->commit(dest)) {
        return -1;
    }
    return 0;
}

int configColorLayer(hwc_context_t *ctx, hwc_layer_1_t *layer,
        const int& dpy, eMdpFlags& mdpFlags, eZorder& z,
        eIsFg& isFg, const eDest& dest) {

    hwc_rect_t dst = layer->displayFrame;
    trimLayer(ctx, dpy, 0, dst, dst);

    int w = ctx->dpyAttr[dpy].xres;
    int h = ctx->dpyAttr[dpy].yres;
    int dst_w = dst.right - dst.left;
    int dst_h = dst.bottom - dst.top;
    uint32_t color = layer->transform;
    Whf whf(w, h, getMdpFormat(HAL_PIXEL_FORMAT_RGBA_8888), 0);

    ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_SOLID_FILL);
    if (layer->blending == HWC_BLENDING_PREMULT)
        ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_BLEND_FG_PREMULT);

    PipeArgs parg(mdpFlags, whf, z, isFg, static_cast<eRotFlags>(0),
                  layer->planeAlpha,
                  (ovutils::eBlending) getBlending(layer->blending));

    // Configure MDP pipe for Color layer
    Dim pos(dst.left, dst.top, dst_w, dst_h);
    ctx->mOverlay->setSource(parg, dest);
    ctx->mOverlay->setColor(color, dest);
    ctx->mOverlay->setTransform(0, dest);
    ctx->mOverlay->setCrop(pos, dest);
    ctx->mOverlay->setPosition(pos, dest);

    if (!ctx->mOverlay->commit(dest)) {
        ALOGE("%s: Configure color layer failed!", __FUNCTION__);
        return -1;
    }
    return 0;
}

void updateSource(eTransform& orient, Whf& whf,
        hwc_rect_t& crop, Rotator *rot) {
    Dim transformedCrop(crop.left, crop.top,
            crop.right - crop.left,
            crop.bottom - crop.top);
    if (qdutils::MDPVersion::getInstance().getMDPVersion() >=
        qdutils::MDSS_V5) {
        //B-family rotator internally could modify destination dimensions if
        //downscaling is supported
        whf = rot->getDstWhf();
        transformedCrop = rot->getDstDimensions();
    } else {
        //A-family rotator rotates entire buffer irrespective of crop, forcing
        //us to recompute the crop based on transform
        orient = static_cast<eTransform>(ovutils::getMdpOrient(orient));
        preRotateSource(orient, whf, transformedCrop);
    }

    crop.left = transformedCrop.x;
    crop.top = transformedCrop.y;
    crop.right = transformedCrop.x + transformedCrop.w;
    crop.bottom = transformedCrop.y + transformedCrop.h;
}

int configureNonSplit(hwc_context_t *ctx, hwc_layer_1_t *layer,
        const int& dpy, eMdpFlags& mdpFlags, eZorder& z,
        eIsFg& isFg, const eDest& dest, Rotator **rot) {

    private_handle_t *hnd = (private_handle_t *)layer->handle;

    if(!hnd) {
        if (layer->flags & HWC_COLOR_FILL) {
            // Configure Color layer
            return configColorLayer(ctx, layer, dpy, mdpFlags, z, isFg, dest);
        }
        ALOGE("%s: layer handle is NULL", __FUNCTION__);
        return -1;
    }

    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;

    hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);
    hwc_rect_t dst = layer->displayFrame;
    int transform = layer->transform;
    eTransform orient = static_cast<eTransform>(transform);
    int downscale = 0;
    int rotFlags = ovutils::ROT_FLAGS_NONE;
    Whf whf(getWidth(hnd), getHeight(hnd),
            getMdpFormat(hnd->format), hnd->size);

    // Handle R/B swap
    if (layer->flags & HWC_FORMAT_RB_SWAP) {
        if (hnd->format == HAL_PIXEL_FORMAT_RGBA_8888)
            whf.format = getMdpFormat(HAL_PIXEL_FORMAT_BGRA_8888);
        else if (hnd->format == HAL_PIXEL_FORMAT_RGBX_8888)
            whf.format = getMdpFormat(HAL_PIXEL_FORMAT_BGRX_8888);
    }

    calcExtDisplayPosition(ctx, hnd, dpy, crop, dst, transform, orient);

    if(isYuvBuffer(hnd) && ctx->mMDP.version >= qdutils::MDP_V4_2 &&
       ctx->mMDP.version < qdutils::MDSS_V5) {
        downscale =  getDownscaleFactor(
            crop.right - crop.left,
            crop.bottom - crop.top,
            dst.right - dst.left,
            dst.bottom - dst.top);
        if(downscale) {
            rotFlags = ROT_DOWNSCALE_ENABLED;
        }
    }

    setMdpFlags(layer, mdpFlags, downscale, transform);

    if(isYuvBuffer(hnd) && //if 90 component or downscale, use rot
            ((transform & HWC_TRANSFORM_ROT_90) || downscale)) {
        *rot = ctx->mRotMgr->getNext();
        if(*rot == NULL) return -1;
        ctx->mLayerRotMap[dpy]->add(layer, *rot);
        if(!dpy)
            BwcPM::setBwc(ctx, crop, dst, transform, mdpFlags);
        //Configure rotator for pre-rotation
        if(configRotator(*rot, whf, crop, mdpFlags, orient, downscale) < 0) {
            ALOGE("%s: configRotator failed!", __FUNCTION__);
            return -1;
        }
        updateSource(orient, whf, crop, *rot);
        rotFlags |= ovutils::ROT_PREROTATED;
    }

    //For the mdp, since either we are pre-rotating or MDP does flips
    orient = OVERLAY_TRANSFORM_0;
    transform = 0;
    PipeArgs parg(mdpFlags, whf, z, isFg,
                  static_cast<eRotFlags>(rotFlags), layer->planeAlpha,
                  (ovutils::eBlending) getBlending(layer->blending));

    if(configMdp(ctx->mOverlay, parg, orient, crop, dst, metadata, dest) < 0) {
        ALOGE("%s: commit failed for low res panel", __FUNCTION__);
        return -1;
    }
    return 0;
}

//Helper to 1) Ensure crops dont have gaps 2) Ensure L and W are even
void sanitizeSourceCrop(hwc_rect_t& cropL, hwc_rect_t& cropR,
        private_handle_t *hnd) {
    if(cropL.right - cropL.left) {
        if(isYuvBuffer(hnd)) {
            //Always safe to even down left
            ovutils::even_floor(cropL.left);
            //If right is even, automatically width is even, since left is
            //already even
            ovutils::even_floor(cropL.right);
        }
        //Make sure there are no gaps between left and right splits if the layer
        //is spread across BOTH halves
        if(cropR.right - cropR.left) {
            cropR.left = cropL.right;
        }
    }

    if(cropR.right - cropR.left) {
        if(isYuvBuffer(hnd)) {
            //Always safe to even down left
            ovutils::even_floor(cropR.left);
            //If right is even, automatically width is even, since left is
            //already even
            ovutils::even_floor(cropR.right);
        }
    }
}

int configureSplit(hwc_context_t *ctx, hwc_layer_1_t *layer,
        const int& dpy, eMdpFlags& mdpFlagsL, eZorder& z,
        eIsFg& isFg, const eDest& lDest, const eDest& rDest,
        Rotator **rot) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(!hnd) {
        ALOGE("%s: layer handle is NULL", __FUNCTION__);
        return -1;
    }

    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;

    int hw_w = ctx->dpyAttr[dpy].xres;
    int hw_h = ctx->dpyAttr[dpy].yres;
    hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);
    hwc_rect_t dst = layer->displayFrame;
    int transform = layer->transform;
    eTransform orient = static_cast<eTransform>(transform);
    const int downscale = 0;
    int rotFlags = ROT_FLAGS_NONE;

    Whf whf(getWidth(hnd), getHeight(hnd),
            getMdpFormat(hnd->format), hnd->size);

    // Handle R/B swap
    if (layer->flags & HWC_FORMAT_RB_SWAP) {
        if (hnd->format == HAL_PIXEL_FORMAT_RGBA_8888)
            whf.format = getMdpFormat(HAL_PIXEL_FORMAT_BGRA_8888);
        else if (hnd->format == HAL_PIXEL_FORMAT_RGBX_8888)
            whf.format = getMdpFormat(HAL_PIXEL_FORMAT_BGRX_8888);
    }

    /* Calculate the external display position based on MDP downscale,
       ActionSafe, and extorientation features. */
    calcExtDisplayPosition(ctx, hnd, dpy, crop, dst, transform, orient);

    setMdpFlags(layer, mdpFlagsL, 0, transform);

    if(lDest != OV_INVALID && rDest != OV_INVALID) {
        //Enable overfetch
        setMdpFlags(mdpFlagsL, OV_MDSS_MDP_DUAL_PIPE);
    }

    //Will do something only if feature enabled and conditions suitable
    //hollow call otherwise
    if(ctx->mAD->prepare(ctx, crop, whf, hnd)) {
        overlay::Writeback *wb = overlay::Writeback::getInstance();
        whf.format = wb->getOutputFormat();
    }

    if(isYuvBuffer(hnd) && (transform & HWC_TRANSFORM_ROT_90)) {
        (*rot) = ctx->mRotMgr->getNext();
        if((*rot) == NULL) return -1;
        ctx->mLayerRotMap[dpy]->add(layer, *rot);
        //Configure rotator for pre-rotation
        if(configRotator(*rot, whf, crop, mdpFlagsL, orient, downscale) < 0) {
            ALOGE("%s: configRotator failed!", __FUNCTION__);
            return -1;
        }
        updateSource(orient, whf, crop, *rot);
        rotFlags |= ROT_PREROTATED;
    }

    eMdpFlags mdpFlagsR = mdpFlagsL;
    setMdpFlags(mdpFlagsR, OV_MDSS_MDP_RIGHT_MIXER);

    hwc_rect_t tmp_cropL = {0}, tmp_dstL = {0};
    hwc_rect_t tmp_cropR = {0}, tmp_dstR = {0};

    const int lSplit = getLeftSplit(ctx, dpy);

    // Calculate Left rects
    if(dst.left < lSplit) {
        tmp_cropL = crop;
        tmp_dstL = dst;
        hwc_rect_t scissor = {0, 0, lSplit, hw_h };
        scissor = getIntersection(ctx->mViewFrame[dpy], scissor);
        qhwc::calculate_crop_rects(tmp_cropL, tmp_dstL, scissor, 0);
    }

    // Calculate Right rects
    if(dst.right > lSplit) {
        tmp_cropR = crop;
        tmp_dstR = dst;
        hwc_rect_t scissor = {lSplit, 0, hw_w, hw_h };
        scissor = getIntersection(ctx->mViewFrame[dpy], scissor);
        qhwc::calculate_crop_rects(tmp_cropR, tmp_dstR, scissor, 0);
    }

    sanitizeSourceCrop(tmp_cropL, tmp_cropR, hnd);

    //When buffer is H-flipped, contents of mixer config also needs to swapped
    //Not needed if the layer is confined to one half of the screen.
    //If rotator has been used then it has also done the flips, so ignore them.
    if((orient & OVERLAY_TRANSFORM_FLIP_H) && (dst.left < lSplit) &&
            (dst.right > lSplit) && (*rot) == NULL) {
        hwc_rect_t new_cropR;
        new_cropR.left = tmp_cropL.left;
        new_cropR.right = new_cropR.left + (tmp_cropR.right - tmp_cropR.left);

        hwc_rect_t new_cropL;
        new_cropL.left  = new_cropR.right;
        new_cropL.right = tmp_cropR.right;

        tmp_cropL.left =  new_cropL.left;
        tmp_cropL.right =  new_cropL.right;

        tmp_cropR.left = new_cropR.left;
        tmp_cropR.right =  new_cropR.right;

    }

    //For the mdp, since either we are pre-rotating or MDP does flips
    orient = OVERLAY_TRANSFORM_0;
    transform = 0;

    //configure left mixer
    if(lDest != OV_INVALID) {
        PipeArgs pargL(mdpFlagsL, whf, z, isFg,
                       static_cast<eRotFlags>(rotFlags), layer->planeAlpha,
                       (ovutils::eBlending) getBlending(layer->blending));

        if(configMdp(ctx->mOverlay, pargL, orient,
                tmp_cropL, tmp_dstL, metadata, lDest) < 0) {
            ALOGE("%s: commit failed for left mixer config", __FUNCTION__);
            return -1;
        }
    }

    //configure right mixer
    if(rDest != OV_INVALID) {
        PipeArgs pargR(mdpFlagsR, whf, z, isFg,
                       static_cast<eRotFlags>(rotFlags),
                       layer->planeAlpha,
                       (ovutils::eBlending) getBlending(layer->blending));
        tmp_dstR.right = tmp_dstR.right - lSplit;
        tmp_dstR.left = tmp_dstR.left - lSplit;
        if(configMdp(ctx->mOverlay, pargR, orient,
                tmp_cropR, tmp_dstR, metadata, rDest) < 0) {
            ALOGE("%s: commit failed for right mixer config", __FUNCTION__);
            return -1;
        }
    }

    return 0;
}

int configureSourceSplit(hwc_context_t *ctx, hwc_layer_1_t *layer,
        const int& dpy, eMdpFlags& mdpFlagsL, eZorder& z,
        eIsFg& isFg, const eDest& lDest, const eDest& rDest,
        Rotator **rot) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(!hnd) {
        ALOGE("%s: layer handle is NULL", __FUNCTION__);
        return -1;
    }

    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;

    int hw_w = ctx->dpyAttr[dpy].xres;
    int hw_h = ctx->dpyAttr[dpy].yres;
    hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);;
    hwc_rect_t dst = layer->displayFrame;
    int transform = layer->transform;
    eTransform orient = static_cast<eTransform>(transform);
    const int downscale = 0;
    int rotFlags = ROT_FLAGS_NONE;
    //Splitting only YUV layer on primary panel needs different zorders
    //for both layers as both the layers are configured to single mixer
    eZorder lz = z;
    eZorder rz = (eZorder)(z + 1);

    Whf whf(getWidth(hnd), getHeight(hnd),
            getMdpFormat(hnd->format), hnd->size);

    /* Calculate the external display position based on MDP downscale,
       ActionSafe, and extorientation features. */
    calcExtDisplayPosition(ctx, hnd, dpy, crop, dst, transform, orient);

    setMdpFlags(layer, mdpFlagsL, 0, transform);
    trimLayer(ctx, dpy, transform, crop, dst);

    if(isYuvBuffer(hnd) && (transform & HWC_TRANSFORM_ROT_90)) {
        (*rot) = ctx->mRotMgr->getNext();
        if((*rot) == NULL) return -1;
        ctx->mLayerRotMap[dpy]->add(layer, *rot);
        //Configure rotator for pre-rotation
        if(configRotator(*rot, whf, crop, mdpFlagsL, orient, downscale) < 0) {
            ALOGE("%s: configRotator failed!", __FUNCTION__);
            return -1;
        }
        updateSource(orient, whf, crop, *rot);
        rotFlags |= ROT_PREROTATED;
    }

    eMdpFlags mdpFlagsR = mdpFlagsL;
    int lSplit = dst.left + (dst.right - dst.left)/2;

    hwc_rect_t tmp_cropL = {0}, tmp_dstL = {0};
    hwc_rect_t tmp_cropR = {0}, tmp_dstR = {0};

    if(lDest != OV_INVALID) {
        tmp_cropL = crop;
        tmp_dstL = dst;
        hwc_rect_t scissor = {dst.left, dst.top, lSplit, dst.bottom };
        qhwc::calculate_crop_rects(tmp_cropL, tmp_dstL, scissor, 0);
    }
    if(rDest != OV_INVALID) {
        tmp_cropR = crop;
        tmp_dstR = dst;
        hwc_rect_t scissor = {lSplit, dst.top, dst.right, dst.bottom };
        qhwc::calculate_crop_rects(tmp_cropR, tmp_dstR, scissor, 0);
    }

    sanitizeSourceCrop(tmp_cropL, tmp_cropR, hnd);

    //When buffer is H-flipped, contents of mixer config also needs to swapped
    //Not needed if the layer is confined to one half of the screen.
    //If rotator has been used then it has also done the flips, so ignore them.
    if((orient & OVERLAY_TRANSFORM_FLIP_H) && lDest != OV_INVALID
            && rDest != OV_INVALID && (*rot) == NULL) {
        hwc_rect_t new_cropR;
        new_cropR.left = tmp_cropL.left;
        new_cropR.right = new_cropR.left + (tmp_cropR.right - tmp_cropR.left);

        hwc_rect_t new_cropL;
        new_cropL.left  = new_cropR.right;
        new_cropL.right = tmp_cropR.right;

        tmp_cropL.left =  new_cropL.left;
        tmp_cropL.right =  new_cropL.right;

        tmp_cropR.left = new_cropR.left;
        tmp_cropR.right =  new_cropR.right;

    }

    //For the mdp, since either we are pre-rotating or MDP does flips
    orient = OVERLAY_TRANSFORM_0;
    transform = 0;

    //configure left half
    if(lDest != OV_INVALID) {
        PipeArgs pargL(mdpFlagsL, whf, lz, isFg,
                static_cast<eRotFlags>(rotFlags), layer->planeAlpha,
                (ovutils::eBlending) getBlending(layer->blending));

        if(configMdp(ctx->mOverlay, pargL, orient,
                    tmp_cropL, tmp_dstL, metadata, lDest) < 0) {
            ALOGE("%s: commit failed for left half config", __FUNCTION__);
            return -1;
        }
    }

    //configure right half
    if(rDest != OV_INVALID) {
        PipeArgs pargR(mdpFlagsR, whf, rz, isFg,
                static_cast<eRotFlags>(rotFlags),
                layer->planeAlpha,
                (ovutils::eBlending) getBlending(layer->blending));
        if(configMdp(ctx->mOverlay, pargR, orient,
                    tmp_cropR, tmp_dstR, metadata, rDest) < 0) {
            ALOGE("%s: commit failed for right half config", __FUNCTION__);
            return -1;
        }
    }

    return 0;
}

bool canUseRotator(hwc_context_t *ctx, int dpy) {
    if(qdutils::MDPVersion::getInstance().is8x26() &&
            isSecondaryConnected(ctx) &&
            !ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].isPause) {
        /* 8x26 mdss driver supports multiplexing of DMA pipe
         * in LINE and BLOCK modes for writeback panels.
         */
        if(dpy == HWC_DISPLAY_PRIMARY)
            return false;
    }
    if(ctx->mMDP.version == qdutils::MDP_V3_0_4)
        return false;
    return true;
}

int getLeftSplit(hwc_context_t *ctx, const int& dpy) {
    //Default even split for all displays with high res
    int lSplit = ctx->dpyAttr[dpy].xres / 2;
    if(dpy == HWC_DISPLAY_PRIMARY &&
            qdutils::MDPVersion::getInstance().getLeftSplit()) {
        //Override if split published by driver for primary
        lSplit = qdutils::MDPVersion::getInstance().getLeftSplit();
    }
    return lSplit;
}

bool isDisplaySplit(hwc_context_t* ctx, int dpy) {
    if(ctx->dpyAttr[dpy].xres > qdutils::MAX_DISPLAY_DIM) {
        return true;
    }
    //For testing we could split primary via device tree values
    if(dpy == HWC_DISPLAY_PRIMARY &&
        qdutils::MDPVersion::getInstance().getRightSplit()) {
        return true;
    }
    return false;
}

void dumpBuffer(private_handle_t *ohnd, char *bufferName) {
    if (ohnd != NULL && ohnd->base) {
        char dumpFilename[PATH_MAX];
        bool bResult = false;
        snprintf(dumpFilename, sizeof(dumpFilename), "/data/%s.%s.%dx%d.raw",
            bufferName,
            overlay::utils::getFormatString(utils::getMdpFormat(ohnd->format)),
            getWidth(ohnd), getHeight(ohnd));
        FILE* fp = fopen(dumpFilename, "w+");
        if (NULL != fp) {
            bResult = (bool) fwrite((void*)ohnd->base, ohnd->size, 1, fp);
            fclose(fp);
        }
        ALOGD("Buffer[%s] Dump to %s: %s",
        bufferName, dumpFilename, bResult ? "Success" : "Fail");
    }
}

bool isAbcInUse(hwc_context_t *ctx){
  return (ctx->enableABC && ctx->listStats[0].renderBufIndexforABC == 0);
}

bool isGLESComp(hwc_context_t *ctx,
                     hwc_display_contents_1_t* list) {
    int numAppLayers = ctx->listStats[HWC_DISPLAY_PRIMARY].numAppLayers;
    for(int index = 0; index < numAppLayers; index++) {
        hwc_layer_1_t* layer = &(list->hwLayers[index]);
        if(layer->compositionType == HWC_FRAMEBUFFER)
            return true;
    }
    return false;
}

void setGPUHint(hwc_context_t* ctx, hwc_display_contents_1_t* list) {
#ifdef QTI_BSP
    struct gpu_hint_info *gpuHint = &ctx->mGPUHintInfo;
    if(!gpuHint->mGpuPerfModeEnable)
        return;

    /* Set the GPU hint flag to high for MIXED/GPU composition only for
       first frame after MDP -> GPU/MIXED mode transition. Set the GPU
       hint to default if the previous composition is GPU or current GPU
       composition is due to idle fallback */
    if(!gpuHint->mEGLDisplay || !gpuHint->mEGLContext) {
        gpuHint->mEGLDisplay = (*(ctx->mpfn_eglGetCurrentDisplay))();
        if(!gpuHint->mEGLDisplay) {
            ALOGW("%s Warning: EGL current display is NULL", __FUNCTION__);
            return;
        }
        gpuHint->mEGLContext = (*(ctx->mpfn_eglGetCurrentContext))();
        if(!gpuHint->mEGLContext) {
            ALOGW("%s Warning: EGL current context is NULL", __FUNCTION__);
            return;
        }
    }
    if(isGLESComp(ctx, list)) {
        if(!gpuHint->mPrevCompositionGLES && !MDPComp::isIdleFallback()) {
            EGLint attr_list[] = {EGL_GPU_HINT_1,
                                  EGL_GPU_LEVEL_3,
                                  EGL_NONE };
            if((gpuHint->mCurrGPUPerfMode != EGL_GPU_LEVEL_3) &&
                !((*(ctx->mpfn_eglGpuPerfHintQCOM))(gpuHint->mEGLDisplay,
                                    gpuHint->mEGLContext, attr_list))) {
                ALOGW("eglGpuPerfHintQCOM failed for Built in display");
            } else {
                gpuHint->mCurrGPUPerfMode = EGL_GPU_LEVEL_3;
                gpuHint->mPrevCompositionGLES = true;
            }
        } else {
            EGLint attr_list[] = {EGL_GPU_HINT_1,
                                  EGL_GPU_LEVEL_0,
                                  EGL_NONE };
            if((gpuHint->mCurrGPUPerfMode != EGL_GPU_LEVEL_0) &&
                !((*(ctx->mpfn_eglGpuPerfHintQCOM))(gpuHint->mEGLDisplay,
                                    gpuHint->mEGLContext, attr_list))) {
                ALOGW("eglGpuPerfHintQCOM failed for Built in display");
            } else {
                gpuHint->mCurrGPUPerfMode = EGL_GPU_LEVEL_0;
            }
        }
    } else {
        /* set the GPU hint flag to default for MDP composition */
        EGLint attr_list[] = {EGL_GPU_HINT_1,
                              EGL_GPU_LEVEL_0,
                              EGL_NONE };
        if((gpuHint->mCurrGPUPerfMode != EGL_GPU_LEVEL_0) &&
                !((*(ctx->mpfn_eglGpuPerfHintQCOM))(gpuHint->mEGLDisplay,
                                    gpuHint->mEGLContext, attr_list))) {
            ALOGW("eglGpuPerfHintQCOM failed for Built in display");
        } else {
            gpuHint->mCurrGPUPerfMode = EGL_GPU_LEVEL_0;
        }
        gpuHint->mPrevCompositionGLES = false;
    }
#endif
}

bool isPeripheral(const hwc_rect_t& rect1, const hwc_rect_t& rect2) {
    // To be peripheral, 3 boundaries should match.
   uint8_t eqBounds = 0;
    if (rect1.left == rect2.left)
        eqBounds++;
    if (rect1.top == rect2.top)
       eqBounds++;
    if (rect1.right == rect2.right)
        eqBounds++;
    if (rect1.bottom == rect2.bottom)
        eqBounds++;
    return (eqBounds == 3);
}

//clear prev layer prop flags and realloc for current frame
void reset_layer_prop(hwc_context_t* ctx, int dpy, int numAppLayers) {
    if(ctx->layerProp[dpy]) {
       delete[] ctx->layerProp[dpy];
       ctx->layerProp[dpy] = NULL;
    }
    ctx->layerProp[dpy] = new LayerProp[numAppLayers];
}

void BwcPM::setBwc(hwc_context_t *ctx, const hwc_rect_t& crop,
            const hwc_rect_t& dst, const int& transform,
            ovutils::eMdpFlags& mdpFlags) {
    //Target doesnt support Bwc
    if(!qdutils::MDPVersion::getInstance().supportsBWC()) {
        return;
    }
    //src width > MAX mixer supported dim
    if((crop.right - crop.left) > qdutils::MAX_DISPLAY_DIM) {
        return;
    }
    //Decimation necessary, cannot use BWC. H/W requirement.
    if(qdutils::MDPVersion::getInstance().supportsDecimation()) {
        int src_w = crop.right - crop.left;
        int src_h = crop.bottom - crop.top;
        int dst_w = dst.right - dst.left;
        int dst_h = dst.bottom - dst.top;
        if(transform & HAL_TRANSFORM_ROT_90) {
            swap(src_w, src_h);
        }
        float horDscale = 0.0f;
        float verDscale = 0.0f;
        int horzDeci = 0;
        int vertDeci = 0;
        ovutils::getDecimationFactor(src_w, src_h, dst_w, dst_h, horDscale,
                verDscale);
        //TODO Use log2f once math.h has it
        if((int)horDscale)
            horzDeci = (int)(log(horDscale) / log(2));
        if((int)verDscale)
            vertDeci = (int)(log(verDscale) / log(2));
        if(horzDeci || vertDeci) return;
    }
    //Property
    char value[PROPERTY_VALUE_MAX];
    property_get("debug.disable.bwc", value, "0");
     if(atoi(value)) return;

    ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDSS_MDP_BWC_EN);
}

void LayerRotMap::add(hwc_layer_1_t* layer, Rotator *rot) {
    if(mCount >= MAX_SESS) return;
    mLayer[mCount] = layer;
    mRot[mCount] = rot;
    mCount++;
}

void LayerRotMap::reset() {
    for (int i = 0; i < MAX_SESS; i++) {
        mLayer[i] = 0;
        mRot[i] = 0;
    }
    mCount = 0;
}

void LayerRotMap::clear() {
    RotMgr::getInstance()->markUnusedTop(mCount);
    reset();
}

void LayerRotMap::setReleaseFd(const int& fence) {
    for(uint32_t i = 0; i < mCount; i++) {
        mRot[i]->setReleaseFd(dup(fence));
    }
}

hwc_rect_t sanitizeROI(struct hwc_rect roi, hwc_rect boundary)
{
   if(!isValidRect(roi))
      return roi;

   struct hwc_rect t_roi = roi;

   const int LEFT_ALIGN = qdutils::MDPVersion::getInstance().getLeftAlign();
   const int WIDTH_ALIGN = qdutils::MDPVersion::getInstance().getWidthAlign();
   const int TOP_ALIGN = qdutils::MDPVersion::getInstance().getTopAlign();
   const int HEIGHT_ALIGN = qdutils::MDPVersion::getInstance().getHeightAlign();
   const int MIN_WIDTH = qdutils::MDPVersion::getInstance().getMinROIWidth();

   /* Align to minimum width recommended by the panel */
   if((t_roi.right - t_roi.left) < MIN_WIDTH) {
       if((t_roi.left + MIN_WIDTH) > boundary.right)
           t_roi.left = t_roi.right - MIN_WIDTH;
       else
           t_roi.right = t_roi.left + MIN_WIDTH;
   }

   if(LEFT_ALIGN)
       t_roi.left = t_roi.left - (t_roi.left % LEFT_ALIGN);

   /* Align left and width to meet panel restrictions */
   if(WIDTH_ALIGN) {
       int width = t_roi.right - t_roi.left;
       width = WIDTH_ALIGN * ((width + (WIDTH_ALIGN - 1)) / WIDTH_ALIGN);
       t_roi.right = t_roi.left + width;

       if(t_roi.right > boundary.right) {
           t_roi.right = boundary.right;
           t_roi.left = t_roi.right - width;

           if(LEFT_ALIGN)
             t_roi.left = t_roi.left - (t_roi.left % LEFT_ALIGN);
       }
   }

   /* Align top and height to meet panel restrictions */
   if(TOP_ALIGN)
       t_roi.top = t_roi.top - (t_roi.top % TOP_ALIGN);

   if(HEIGHT_ALIGN) {
       int height = t_roi.bottom - t_roi.top;
       height = HEIGHT_ALIGN *  ((height + (HEIGHT_ALIGN - 1)) / HEIGHT_ALIGN);
       t_roi.bottom = t_roi.top  + height;

       if(t_roi.bottom > boundary.bottom) {
           t_roi.bottom = boundary.bottom;
           t_roi.top = t_roi.bottom - height;

           if(TOP_ALIGN)
             t_roi.top = t_roi.top - (t_roi.top % TOP_ALIGN);
       }
   }

   return t_roi;
}

bool loadEglLib(hwc_context_t* ctx) {
    bool success = false;
#ifdef QTI_BSP
    const char* error;
    dlerror();

    ctx->mEglLib = dlopen("libEGL_adreno.so", RTLD_NOW);
    if(ctx->mEglLib) {
        *(void **)&(ctx->mpfn_eglGpuPerfHintQCOM) = dlsym(ctx->mEglLib, "eglGpuPerfHintQCOM");
        *(void **)&(ctx->mpfn_eglGetCurrentDisplay) = dlsym(ctx->mEglLib,"eglGetCurrentDisplay");
        *(void **)&(ctx->mpfn_eglGetCurrentContext) = dlsym(ctx->mEglLib,"eglGetCurrentContext");
        if (!ctx->mpfn_eglGpuPerfHintQCOM ||
            !ctx->mpfn_eglGetCurrentDisplay ||
            !ctx->mpfn_eglGetCurrentContext) {
            ALOGE("Failed to load symbols from libEGL");
            dlclose(ctx->mEglLib);
            ctx->mEglLib = NULL;
            return false;
        }
        success = true;
        ALOGI("Successfully Loaded GPUPerfHint APIs");
    } else {
        ALOGE("Couldn't load libEGL: %s", dlerror());
    }
#endif
    return success;
}

};//namespace qhwc
