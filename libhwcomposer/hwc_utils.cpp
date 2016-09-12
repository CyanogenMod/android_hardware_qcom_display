/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2014,2016, The Linux Foundation All rights reserved.
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
#include <utils/Trace.h>
#include <gralloc_priv.h>
#include <overlay.h>
#include <overlayRotator.h>
#include <overlayWriteback.h>
#include <overlayCursor.h>
#include "hwc_utils.h"
#include "hwc_mdpcomp.h"
#include "hwc_fbupdate.h"
#include "hwc_ad.h"
#include "mdp_version.h"
#include "hwc_copybit.h"
#include "hwc_dump_layers.h"
#include "hdmi.h"
#include "hwc_qclient.h"
#include "QService.h"
#include "comptype.h"
#include "hwc_virtual.h"
#include "qd_utils.h"
#include <sys/sysinfo.h>
#include <dlfcn.h>
#include <video/msm_hdmi_modes.h>

using namespace qClient;
using namespace qService;
using namespace android;
using namespace overlay;
using namespace overlay::utils;
namespace ovutils = overlay::utils;

#define PROP_DEFAULT_APPBUFFER  "hw.sf.app_buff_count"
#define MAX_RAM_SIZE  512*1024*1024
#define qHD_WIDTH 540


namespace qhwc {

// Std refresh rates for digital videos- 24p, 30p, 48p and 60p
uint32_t stdRefreshRates[] = { 30, 24, 48, 60 };

bool isValidResolution(hwc_context_t *ctx, uint32_t xres, uint32_t yres)
{
    return !((xres > qdutils::MDPVersion::getInstance().getMaxPipeWidth() &&
                !isDisplaySplit(ctx, HWC_DISPLAY_PRIMARY)) ||
            (xres < MIN_DISPLAY_XRES || yres < MIN_DISPLAY_YRES));
}

static void handleFbScaling(hwc_context_t *ctx, int xresPanel, int yresPanel,
        int width, int height) {
    const int dpy = HWC_DISPLAY_PRIMARY;
    //Store original display resolution.
    ctx->dpyAttr[dpy].xresFB = xresPanel;
    ctx->dpyAttr[dpy].yresFB = yresPanel;
    ctx->dpyAttr[dpy].fbScaling = false;
    char property[PROPERTY_VALUE_MAX] = {'\0'};
    char *yptr = NULL;
    if (property_get("debug.hwc.fbsize", property, NULL) > 0) {
        yptr = strcasestr(property,"x");
        if(yptr) {
            int xresFB = atoi(property);
            int yresFB = atoi(yptr + 1);
            if (isValidResolution(ctx, xresFB, yresFB) &&
                xresFB != xresPanel && yresFB != yresPanel) {
                ctx->dpyAttr[dpy].xresFB = xresFB;
                ctx->dpyAttr[dpy].yresFB = yresFB;
                ctx->dpyAttr[dpy].fbScaling = true;

                //Calculate DPI according to changed resolution.
                float xdpi = ((float)xresFB * 25.4f) / (float)width;
                float ydpi = ((float)yresFB * 25.4f) / (float)height;
                ctx->dpyAttr[dpy].xdpi = xdpi;
                ctx->dpyAttr[dpy].ydpi = ydpi;
            }
        }
    }
    ctx->dpyAttr[dpy].fbWidthScaleRatio = (float) ctx->dpyAttr[dpy].xres /
            (float) ctx->dpyAttr[dpy].xresFB;
    ctx->dpyAttr[dpy].fbHeightScaleRatio = (float) ctx->dpyAttr[dpy].yres /
            (float) ctx->dpyAttr[dpy].yresFB;
}

// Initialize hdmi display attributes based on
// hdmi display class state
void updateDisplayInfo(hwc_context_t* ctx, int dpy) {
    ctx->dpyAttr[dpy].fd = ctx->mHDMIDisplay->getFd();
    ctx->dpyAttr[dpy].xres = ctx->mHDMIDisplay->getFBWidth();
    ctx->dpyAttr[dpy].yres = ctx->mHDMIDisplay->getFBHeight();
    ctx->dpyAttr[dpy].mMDPScalingMode = ctx->mHDMIDisplay->getMDPScalingMode();
    ctx->dpyAttr[dpy].vsync_period = ctx->mHDMIDisplay->getVsyncPeriod();
    //FIXME: for now assume HDMI as secure
    //Will need to read the HDCP status from the driver
    //and update this accordingly
    ctx->dpyAttr[dpy].secure = true;
    ctx->mViewFrame[dpy].left = 0;
    ctx->mViewFrame[dpy].top = 0;
    ctx->mViewFrame[dpy].right = ctx->dpyAttr[dpy].xres;
    ctx->mViewFrame[dpy].bottom = ctx->dpyAttr[dpy].yres;
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
        info.width  = (int)(((float)info.xres * 25.4f)/160.0f + 0.5f);
        info.height = (int)(((float)info.yres * 25.4f)/160.0f + 0.5f);
    }

    float xdpi = ((float)info.xres * 25.4f) / (float)info.width;
    float ydpi = ((float)info.yres * 25.4f) / (float)info.height;

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

    float fps  = (float)metadata.data.panel_frame_rate;
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
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].refreshRate = (uint32_t)fps;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].dynRefreshRate = (uint32_t)fps;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].secure = true;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period =
            (uint32_t)(1000000000l / fps);

    handleFbScaling(ctx, info.xres, info.yres, info.width, info.height);

    //Unblank primary on first boot
    if(ioctl(fb_fd, FBIOBLANK,FB_BLANK_UNBLANK) < 0) {
        ALOGE("%s: Failed to unblank display", __FUNCTION__);
        return -errno;
    }
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].isActive = true;

    return 0;
}

static void changeDefaultAppBufferCount() {
    struct sysinfo info;
    unsigned long int ramSize = 0;
    if (!sysinfo(&info)) {
           ramSize = info.totalram ;
    }
    int fb_fd = -1;
    struct fb_var_screeninfo sInfo ={0};
    fb_fd = open("/dev/graphics/fb0", O_RDONLY);
    if (fb_fd >=0) {
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &sInfo);
        close(fb_fd);
    }
    if ((ramSize && ramSize < MAX_RAM_SIZE) &&
         (sInfo.xres &&  sInfo.xres <= qHD_WIDTH )) {
                  property_set(PROP_DEFAULT_APPBUFFER, "2");
    }
}

int initContext(hwc_context_t *ctx)
{
    int error = -1;
    int compositionType = 0;

    //Right now hwc starts the service but anybody could do it, or it could be
    //independent process as well.
    QService::init();
    sp<IQClient> client = new QClient(ctx);
    android::sp<qService::IQService> qservice_sp = interface_cast<IQService>(
            defaultServiceManager()->getService(
            String16("display.qservice")));
    if (qservice_sp.get()) {
      qservice_sp->connect(client);
    } else {
      ALOGE("%s: Failed to acquire service pointer", __FUNCTION__);
      return error;
    }

    overlay::Overlay::initOverlay();
    ctx->mHDMIDisplay = new HDMIDisplay();
    uint32_t priW = 0, priH = 0;
    // 1. HDMI as Primary
    //    -If HDMI cable is connected, read display configs from edid data
    //    -If HDMI cable is not connected then use default data in vscreeninfo
    // 2. HDMI as External
    //    -Initialize HDMI class for use with external display
    //    -Use vscreeninfo to populate display configs
    if(ctx->mHDMIDisplay->isHDMIPrimaryDisplay()) {
        int connected = ctx->mHDMIDisplay->getConnectedState();
        if(connected == 1) {
            error = ctx->mHDMIDisplay->configure();
            if (error < 0) {
                goto OpenFBError;
            }
            updateDisplayInfo(ctx, HWC_DISPLAY_PRIMARY);
            ctx->dpyAttr[HWC_DISPLAY_PRIMARY].connected = true;
        } else {
            error = openFramebufferDevice(ctx);
            if(error < 0) {
                goto OpenFBError;
            }
            ctx->dpyAttr[HWC_DISPLAY_PRIMARY].connected = false;
        }
    } else {
        error = openFramebufferDevice(ctx);
        if(error < 0) {
            goto OpenFBError;
        }
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].connected = true;
        // Send the primary resolution to the hdmi display class
        // to be used for MDP scaling functionality
        priW = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
        priH = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres;
        ctx->mHDMIDisplay->setPrimaryAttributes(priW, priH);
    }

    char value[PROPERTY_VALUE_MAX];
    ctx->mMDP.version = qdutils::MDPVersion::getInstance().getMDPVersion();
    ctx->mMDP.hasOverlay = qdutils::MDPVersion::getInstance().hasOverlay();
    ctx->mMDP.panel = qdutils::MDPVersion::getInstance().getPanelType();
    ctx->mOverlay = overlay::Overlay::getInstance();
    ctx->mRotMgr = RotMgr::getInstance();
    ctx->mBWCEnabled = qdutils::MDPVersion::getInstance().supportsBWC();

    //default_app_buffer for ferrum
    if (ctx->mMDP.version ==  qdutils::MDP_V3_0_5) {
       changeDefaultAppBufferCount();
    }
    // Initialize composition objects for the primary display
    initCompositionResources(ctx, HWC_DISPLAY_PRIMARY);

    // Check if the target supports copybit compostion (dyn/mdp) to
    // decide if we need to open the copybit module.
    compositionType =
                qdutils::QCCompositionType::getInstance().getCompositionType();

    // Only MDP copybit is used
    if ((compositionType & (qdutils::COMPOSITION_TYPE_DYN |
            qdutils::COMPOSITION_TYPE_MDP)) &&
            ((qdutils::MDPVersion::getInstance().getMDPVersion() ==
            qdutils::MDP_V3_0_4) ||
            (qdutils::MDPVersion::getInstance().getMDPVersion() ==
            qdutils::MDP_V3_0_5))) {
        ctx->mCopyBit[HWC_DISPLAY_PRIMARY] = new CopyBit(ctx,
                                                         HWC_DISPLAY_PRIMARY);
    }

    ctx->mHWCVirtual = new HWCVirtualVDS();
    ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].isActive = false;
    ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].connected = false;
    ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].isActive = false;
    ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].connected = false;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].mMDPScalingMode= false;
    ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].mMDPScalingMode = false;
    ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].mMDPScalingMode = false;

    //Initialize the primary display viewFrame info
    ctx->mViewFrame[HWC_DISPLAY_PRIMARY].left = 0;
    ctx->mViewFrame[HWC_DISPLAY_PRIMARY].top = 0;
    ctx->mViewFrame[HWC_DISPLAY_PRIMARY].right =
        (int)ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
    ctx->mViewFrame[HWC_DISPLAY_PRIMARY].bottom =
         (int)ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres;

    for (uint32_t i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        ctx->mHwcDebug[i] = new HwcDebug(i);
        ctx->mLayerRotMap[i] = new LayerRotMap();
        ctx->mAnimationState[i] = ANIMATION_STOPPED;
        ctx->dpyAttr[i].mActionSafePresent = false;
        ctx->dpyAttr[i].mAsWidthRatio = 0;
        ctx->dpyAttr[i].mAsHeightRatio = 0;
        ctx->dpyAttr[i].s3dMode = HDMI_S3D_NONE;
        ctx->dpyAttr[i].s3dModeForced = false;
    }

    //Make sure that the 3D mode is unset at bootup
    //This makes sure that the state is accurate on framework reboots
    ctx->mHDMIDisplay->configure3D(HDMI_S3D_NONE);

    for (uint32_t i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        ctx->mPrevHwLayerCount[i] = 0;
    }

    MDPComp::init(ctx);
    ctx->mAD = new AssertiveDisplay(ctx);

    ctx->vstate.enable = false;
    ctx->vstate.fakevsync = false;
    ctx->mExtOrientation = 0;
    ctx->numActiveDisplays = 1;

    // Initialize device orientation to its default orientation
    ctx->deviceOrientation = 0;
    ctx->mBufferMirrorMode = false;

    property_get("sys.hwc.windowbox_aspect_ratio_tolerance", value, "0");
    ctx->mAspectRatioToleranceLevel = (((float)atoi(value)) / 100.0f);

    ctx->enableABC = false;
    property_get("debug.sf.hwc.canUseABC", value, "0");
    ctx->enableABC  = atoi(value) ? true : false;

    // Initializing boot anim completed check to false
    ctx->mBootAnimCompleted = false;

    // Read the system property to determine if windowboxing feature is enabled.
    ctx->mWindowboxFeature = false;
    if(property_get("sys.hwc.windowbox_feature", value, "false")
            && !strcmp(value, "true")) {
        ctx->mWindowboxFeature = true;
    }

    ctx->mUseMetaDataRefreshRate = true;
    if(property_get("persist.metadata_dynfps.disable", value, "false")
            && !strcmp(value, "true")) {
        ctx->mUseMetaDataRefreshRate = false;
    }

    memset(&(ctx->mPtorInfo), 0, sizeof(ctx->mPtorInfo));
    ctx->mHPDEnabled = false;
    ctx->triggerRefresh = false;
    ALOGI("Initializing Qualcomm Hardware Composer");
    ALOGI("MDP version: %d", ctx->mMDP.version);

    return 0;

OpenFBError:
    ALOGE("%s: Fatal Error: FB Open failed!!!", __FUNCTION__);
    delete ctx->mHDMIDisplay;
    return error;
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
    char sysfsPath[qdutils::MAX_SYSFS_FILE_PATH];
    snprintf (sysfsPath, sizeof(sysfsPath),
            "/sys/devices/virtual/graphics/fb%d/dynamic_fps", fbNum);

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
    if(ctx->dpyAttr[dpy].mMDPScalingMode) {
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
    float asW = (float)fbWidth;
    float asH = (float)fbHeight;

    // based on the action safe ratio, get the Action safe rectangle
    asW = ((float)fbWidth * (1.0f -  (float)asWidthRatio / 100.0f));
    asH = ((float)fbHeight * (1.0f -  (float)asHeightRatio / 100.0f));
    asX = ((float)fbWidth - asW) / 2;
    asY = ((float)fbHeight - asH) / 2;

    // calculate the position ratio
    xRatio = (float)x/(float)fbWidth;
    yRatio = (float)y/(float)fbHeight;
    wRatio = (float)w/(float)fbWidth;
    hRatio = (float)h/(float)fbHeight;

    //Calculate the position...
    x = int((xRatio * asW) + asX);
    y = int((yRatio * asH) + asY);
    w = int(wRatio * asW);
    h = int(hRatio * asH);

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
    float fbWidth  = (float)ctx->dpyAttr[dpy].xres;
    float fbHeight = (float)ctx->dpyAttr[dpy].yres;
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

    Whf whf((uint32_t)fbWidth, (uint32_t)fbHeight, 0);
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
        width = float(rect.right - rect.left);
        height = float(rect.bottom - rect.top);
    }
    xRatio = (float)((float)inPos.x/actualWidth);
    yRatio = (float)((float)inPos.y/actualHeight);
    wRatio = (float)((float)inPos.w/actualWidth);
    hRatio = (float)((float)inPos.h/actualHeight);

    //Calculate the pos9ition...
    outPos.x = uint32_t((xRatio * width) + (float)xPos);
    outPos.y = uint32_t((yRatio * height) + (float)yPos);
    outPos.w = uint32_t(wRatio * width);
    outPos.h = uint32_t(hRatio * height);
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
        xRatio = (float)(outPos.x - xPos)/width;
        // GetaspectRatio -- tricky to get the correct aspect ratio
        // But we need to do this.
        qdutils::getAspectRatioPosition((int)width, (int)height,
                               (int)width,(int)height, r);
        xPos = r.left;
        yPos = r.top;
        float tempHeight = float(r.bottom - r.top);
        yRatio = (float)yPos/height;
        wRatio = (float)outPos.w/width;
        hRatio = tempHeight/height;

        //Map the coordinates back to Framebuffer domain
        outPos.x = uint32_t(xRatio * fbWidth);
        outPos.y = uint32_t(yRatio * fbHeight);
        outPos.w = uint32_t(wRatio * fbWidth);
        outPos.h = uint32_t(hRatio * fbHeight);

        ALOGD_IF(HWC_UTILS_DEBUG, "%s: Calculated AspectRatio for device in"
                 "portrait: x = %d,y = %d w = %d h = %d", __FUNCTION__,
                 outPos.x, outPos.y,
                 outPos.w, outPos.h);
    }
    if(ctx->dpyAttr[dpy].mMDPScalingMode) {
        uint32_t extW = 0, extH = 0;
        if(dpy == HWC_DISPLAY_EXTERNAL) {
            ctx->mHDMIDisplay->getAttributes(extW, extH);
        } else if(dpy == HWC_DISPLAY_VIRTUAL) {
            extW = ctx->mHWCVirtual->getScalingWidth();
            extH = ctx->mHWCVirtual->getScalingHeight();
        }
        ALOGD_IF(HWC_UTILS_DEBUG, "%s: Scaling mode extW=%d extH=%d",
                __FUNCTION__, extW, extH);

        fbWidth  = (float)ctx->dpyAttr[dpy].xres;
        fbHeight = (float)ctx->dpyAttr[dpy].yres;
        //Calculate the position...
        xRatio = (float)outPos.x/fbWidth;
        yRatio = (float)outPos.y/fbHeight;
        wRatio = (float)outPos.w/fbWidth;
        hRatio = (float)outPos.h/fbHeight;

        outPos.x = uint32_t(xRatio * (float)extW);
        outPos.y = uint32_t(yRatio * (float)extH);
        outPos.w = uint32_t(wRatio * (float)extW);
        outPos.h = uint32_t(hRatio * (float)extH);
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
    if(dpy && ctx->mOverlay->isUIScalingOnExternalSupported()) {
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
            if(ctx->dpyAttr[dpy].mMDPScalingMode) {
                uint32_t extW = 0, extH = 0;
                // if MDP scaling mode is enabled, map the co-ordinates to new
                // domain(downscaled)
                float fbWidth  = (float)ctx->dpyAttr[dpy].xres;
                float fbHeight = (float)ctx->dpyAttr[dpy].yres;
                // query MDP configured attributes
                if(dpy == HWC_DISPLAY_EXTERNAL) {
                    ctx->mHDMIDisplay->getAttributes(extW, extH);
                } else if(dpy == HWC_DISPLAY_VIRTUAL) {
                    extW = ctx->mHWCVirtual->getScalingWidth();
                    extH = ctx->mHWCVirtual->getScalingHeight();
                }
                ALOGD_IF(HWC_UTILS_DEBUG, "%s: Scaling mode extW=%d extH=%d",
                        __FUNCTION__, extW, extH);

                //Calculate the ratio...
                float wRatio = ((float)extW)/fbWidth;
                float hRatio = ((float)extH)/fbHeight;

                //convert Dim to hwc_rect_t
                displayFrame.left = int(wRatio*(float)displayFrame.left);
                displayFrame.top = int(hRatio*(float)displayFrame.top);
                displayFrame.right = int(wRatio*(float)displayFrame.right);
                displayFrame.bottom = int(hRatio*(float)displayFrame.bottom);
                ALOGD_IF(DEBUG_MDPDOWNSCALE, "Calculated external display frame"
                         " for MDPDownscale feature [%d %d %d %d]",
                         displayFrame.left, displayFrame.top,
                         displayFrame.right, displayFrame.bottom);
            }
        }else {
            if(extOrient || ctx->dpyAttr[dpy].mMDPScalingMode) {
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

    if(layer->transform & HWC_TRANSFORM_ROT_90)
        swap(src_w, src_h);

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
        layer->sourceCropf.left = (float)crop.left;
        layer->sourceCropf.right = (float)crop.right;
        layer->sourceCropf.top = (float)crop.top;
        layer->sourceCropf.bottom = (float)crop.bottom;
    }
}

void setListStats(hwc_context_t *ctx,
        hwc_display_contents_1_t *list, int dpy) {
    const int prevYuvCount = ctx->listStats[dpy].yuvCount;
    memset(&ctx->listStats[dpy], 0, sizeof(ListStats));
    ctx->listStats[dpy].numAppLayers = (int)list->numHwLayers - 1;
    ctx->listStats[dpy].fbLayerIndex = (int)list->numHwLayers - 1;
    ctx->listStats[dpy].skipCount = 0;
    ctx->listStats[dpy].preMultipliedAlpha = false;
    ctx->listStats[dpy].isSecurePresent = false;
    ctx->listStats[dpy].yuvCount = 0;
    char property[PROPERTY_VALUE_MAX];
    ctx->listStats[dpy].isDisplayAnimating = false;
    ctx->listStats[dpy].secureUI = false;
    ctx->listStats[dpy].yuv4k2kCount = 0;
    ctx->dpyAttr[dpy].mActionSafePresent = isActionSafePresent(ctx, dpy);
    ctx->listStats[dpy].renderBufIndexforABC = -1;
    ctx->listStats[dpy].secureRGBCount = 0;
    ctx->listStats[dpy].refreshRateRequest = ctx->dpyAttr[dpy].refreshRate;
    ctx->listStats[dpy].cursorLayerPresent = false;
    uint32_t refreshRate = 0;
#ifdef DYNAMIC_FPS
    qdutils::MDPVersion& mdpHw = qdutils::MDPVersion::getInstance();
#endif
    int s3dFormat = HAL_NO_3D;
    int s3dLayerCount = 0;

    ctx->listStats[dpy].mAIVVideoMode = false;
    resetROI(ctx, dpy);

    trimList(ctx, list, dpy);
    optimizeLayerRects(list);
    for (size_t i = 0; i < (size_t)ctx->listStats[dpy].numAppLayers; i++) {
        hwc_layer_1_t const* layer = &list->hwLayers[i];
        private_handle_t *hnd = (private_handle_t *)layer->handle;

#ifdef QTI_BSP
        // Window boxing feature is applicable obly for external display, So
        // enable mAIVVideoMode only for external display
        if(ctx->mWindowboxFeature && dpy && isAIVVideoLayer(layer)) {
            ctx->listStats[dpy].mAIVVideoMode = true;
        }
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

        // Valid cursor must be the top most layer
        if((int)i == (ctx->listStats[dpy].numAppLayers - 1) &&
                     isCursorLayer(&list->hwLayers[i])) {
            ctx->listStats[dpy].cursorLayerPresent = true;
        }

        //reset yuv indices
        ctx->listStats[dpy].yuvIndices[i] = -1;
        ctx->listStats[dpy].yuv4k2kIndices[i] = -1;

        if (isSecureBuffer(hnd) || isProtectedBuffer(hnd)) {
            // Protected Buffer must be treated as Secure Layer
            ctx->listStats[dpy].isSecurePresent = true;
            if(not isYuvBuffer(hnd)) {
                // cache secureRGB layer parameters like we cache for YUV layers
                int& secureRGBCount = ctx->listStats[dpy].secureRGBCount;
                ctx->listStats[dpy].secureRGBIndices[secureRGBCount] = (int)i;
                secureRGBCount++;
            }
        }

        if (isSkipLayer(&list->hwLayers[i])) {
            ctx->listStats[dpy].skipCount++;
        }

        if (UNLIKELY(isYuvBuffer(hnd))) {
            int& yuvCount = ctx->listStats[dpy].yuvCount;
            ctx->listStats[dpy].yuvIndices[yuvCount] = (int)i;
            yuvCount++;

            if(UNLIKELY(isYUVSplitNeeded(hnd))){
                int& yuv4k2kCount = ctx->listStats[dpy].yuv4k2kCount;
                ctx->listStats[dpy].yuv4k2kIndices[yuv4k2kCount] = (int)i;
                yuv4k2kCount++;
            }

            // Gets set if one YUV layer is 3D
            if (displaySupports3D(ctx,dpy)) {
                s3dFormat = get3DFormat(hnd);
                if(s3dFormat != HAL_NO_3D)
                    s3dLayerCount++;
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

    //Set the TV's 3D mode based on format if it was not forced
    //Only one 3D YUV layer is supported on external
    //If there is more than one 3D YUV layer, the switch to 3D cannot occur.
    if( !ctx->dpyAttr[dpy].s3dModeForced && (s3dLayerCount <= 1)) {
        //XXX: Rapidly going in and out of 3D mode in some cases such
        // as rotation might cause flickers. The OEMs are recommended to disable
        // rotation on HDMI globally or in the app that plays 3D video
        setup3DMode(ctx, dpy, convertS3DFormatToMode(s3dFormat));
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

bool isRotatorSupportedFormat(private_handle_t *hnd) {
    // Following rotator src formats are supported by mdp driver
    // TODO: Add more formats in future, if mdp driver adds support
    if(hnd != NULL) {
        switch(hnd->format) {
            case HAL_PIXEL_FORMAT_RGBA_8888:
            case HAL_PIXEL_FORMAT_RGBA_5551:
            case HAL_PIXEL_FORMAT_RGBA_4444:
            case HAL_PIXEL_FORMAT_RGB_565:
            case HAL_PIXEL_FORMAT_RGB_888:
            case HAL_PIXEL_FORMAT_BGRA_8888:
                return true;
            default:
                return false;
        }
    }
    return false;
}

bool isRotationDoable(hwc_context_t *ctx, private_handle_t *hnd) {
    // Rotate layers, if it is not secure display buffer and not
    // for the MDP versions below MDP5
    if((!isSecureDisplayBuffer(hnd) && isRotatorSupportedFormat(hnd) &&
        !(ctx->mMDP.version < qdutils::MDSS_V5))|| isYuvBuffer(hnd)) {
        return true;
    }
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

bool isSameRect(const hwc_rect& rect1, const hwc_rect& rect2)
{
   return ((rect1.left == rect2.left) && (rect1.top == rect2.top) &&
           (rect1.right == rect2.right) && (rect1.bottom == rect2.bottom));
}

bool isValidRect(const hwc_rect& rect)
{
   return ((rect.bottom > rect.top) && (rect.right > rect.left)) ;
}

bool operator ==(const hwc_rect_t& lhs, const hwc_rect_t& rhs) {
    if(lhs.left == rhs.left && lhs.top == rhs.top &&
       lhs.right == rhs.right &&  lhs.bottom == rhs.bottom )
          return true ;
    return false;
}

bool layerUpdating(const hwc_layer_1_t* layer) {
    hwc_region_t surfDamage = layer->surfaceDamage;
    return ((surfDamage.numRects == 0) ||
            isValidRect(layer->surfaceDamage.rects[0]));
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

void optimizeLayerRects(const hwc_display_contents_1_t *list) {
    int i= (int)list->numHwLayers-2;
    while(i > 0) {
        //see if there is no blending required.
        //If it is opaque see if we can substract this region from below
        //layers.
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
                  // layer has valid surfaceDamage -cant optimize
                  if(isValidRect(irect) && layer->surfaceDamage.numRects < 1) {
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
    size_t last = list->numHwLayers - 1;
    hwc_rect_t fbDisplayFrame = list->hwLayers[last].displayFrame;
    //Initiliaze nwr to first frame
    nwr.left =  list->hwLayers[0].displayFrame.left;
    nwr.top =  list->hwLayers[0].displayFrame.top;
    nwr.right =  list->hwLayers[0].displayFrame.right;
    nwr.bottom =  list->hwLayers[0].displayFrame.bottom;

    for (size_t i = 1; i < last; i++) {
        hwc_rect_t displayFrame = list->hwLayers[i].displayFrame;
        nwr = getUnion(nwr, displayFrame);
    }

    //Intersect with the framebuffer
    nwr = getIntersection(nwr, fbDisplayFrame);
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
        if(currLayer->acquireFenceFd >= 0) {
            rotData.acq_fen_fd_cnt = 1; //1 ioctl call per rot session
        }
        int ret = 0;
        if(LIKELY(!swapzero) and (not ctx->mLayerRotMap[dpy]->isRotCached(i)))
            ret = ioctl(rotFd, MSMFB_BUFFER_SYNC, &rotData);

        if(ret < 0) {
            ALOGE("%s: ioctl MSMFB_BUFFER_SYNC failed for rot sync, err=%s",
                    __FUNCTION__, strerror(errno));
            close(rotReleaseFd);
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

    //Accumulate acquireFenceFds for MDP Overlays
    if(list->outbufAcquireFenceFd >= 0) {
        //Writeback output buffer
        if(LIKELY(!swapzero) )
            acquireFd[count++] = list->outbufAcquireFenceFd;
    }

    for(uint32_t i = 0; i < list->numHwLayers; i++) {
        if(((isAbcInUse(ctx)== true ) ||
          (list->hwLayers[i].compositionType == HWC_OVERLAY)) &&
                        list->hwLayers[i].acquireFenceFd >= 0) {
            if(LIKELY(!swapzero) ) {
                // if ABC is enabled for more than one layer.
                // renderBufIndexforABC will work as FB.Hence
                // set the acquireFD from fd - which is coming from copybit
                if(fd >= 0 && (isAbcInUse(ctx) == true)) {
                    if(ctx->listStats[dpy].renderBufIndexforABC ==(int32_t)i)
                        acquireFd[count++] = fd;
                    else
                        continue;
                } else
                    acquireFd[count++] = list->hwLayers[i].acquireFenceFd;
            }
        }
        if(list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
            if(LIKELY(!swapzero) ) {
                if(fd >= 0) {
                    //set the acquireFD from fd - which is coming from c2d
                    acquireFd[count++] = fd;
                    // Buffer sync IOCTL should be async when using c2d fence is
                    // used
                    data.flags &= ~MDP_BUF_SYNC_FLAG_WAIT;
                } else if(list->hwLayers[i].acquireFenceFd >= 0)
                    acquireFd[count++] = list->hwLayers[i].acquireFenceFd;
            }
        }
    }

    if ((fd >= 0) && !dpy && ctx->mPtorInfo.isActive()) {
        // Acquire c2d fence of Overlap render buffer
        if(LIKELY(!swapzero) )
            acquireFd[count++] = fd;
    }

    data.acq_fen_fd_cnt = count;
    fbFd = ctx->dpyAttr[dpy].fd;

    //Waits for acquire fences, returns a release fence
    if(LIKELY(!swapzero)) {
        ret = ioctl(fbFd, MSMFB_BUFFER_SYNC, &data);
    }

    if(ret < 0) {
        ALOGE("%s: ioctl MSMFB_BUFFER_SYNC failed, err=%s",
                  __FUNCTION__, strerror(errno));
        ALOGE("%s: acq_fen_fd_cnt=%d flags=%d fd=%d dpy=%d numHwLayers=%zu",
              __FUNCTION__, data.acq_fen_fd_cnt, data.flags, fbFd,
              dpy, list->numHwLayers);
        close(releaseFd);
        releaseFd = -1;
        close(retireFd);
        retireFd = -1;
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

void setMdpFlags(hwc_context_t *ctx, hwc_layer_1_t *layer,
        ovutils::eMdpFlags &mdpFlags,
        int rotDownscale, int transform) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    MetaData_t *metadata = hnd ? (MetaData_t *)hnd->base_metadata : NULL;

    if(layer->blending == HWC_BLENDING_PREMULT) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_BLEND_FG_PREMULT);
    }

    if(metadata && (metadata->operation & PP_PARAM_INTERLACED) &&
            metadata->interlaced) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_DEINTERLACE);
    }

    // Mark MDP flags with SECURE_OVERLAY_SESSION for driver
    if(isSecureBuffer(hnd)) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_SMP_FORCE_ALLOC);
    }

    if(isProtectedBuffer(hnd)) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_SMP_FORCE_ALLOC);
    }

    if(isSecureDisplayBuffer(hnd)) {
        // Mark MDP flags with SECURE_DISPLAY_OVERLAY_SESSION for driver
        ovutils::setMdpFlags(mdpFlags,
                             ovutils::OV_MDP_SECURE_DISPLAY_OVERLAY_SESSION);
    }

    //Pre-rotation will be used using rotator.
    if(has90Transform(layer) && isRotationDoable(ctx, hnd)) {
        ovutils::setMdpFlags(mdpFlags,
                ovutils::OV_MDP_SOURCE_ROTATED_90);
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
        const eDest& dest) {

    hwc_rect_t dst = layer->displayFrame;
    trimLayer(ctx, dpy, 0, dst, dst);

    int w = ctx->dpyAttr[dpy].xres;
    int h = ctx->dpyAttr[dpy].yres;
    int dst_w = dst.right - dst.left;
    int dst_h = dst.bottom - dst.top;
    uint32_t color = layer->transform;
    Whf whf(w, h, getMdpFormat(HAL_PIXEL_FORMAT_RGBA_8888));

    ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_SOLID_FILL);
    if (layer->blending == HWC_BLENDING_PREMULT)
        ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_BLEND_FG_PREMULT);

    PipeArgs parg(mdpFlags, whf, z, static_cast<eRotFlags>(0),
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

bool configHwCursor(const int fd, int dpy, hwc_layer_1_t* layer) {
    if(dpy > HWC_DISPLAY_PRIMARY) {
        // HWCursor not supported on secondary displays
        return false;
    }
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    hwc_rect dst = layer->displayFrame;
    hwc_rect src = integerizeSourceCrop(layer->sourceCropf);
    int srcW = src.right - src.left;
    int srcH = src.bottom - src.top;
    int dstW = dst.right - dst.left;
    int dstH = dst.bottom - dst.top;
    bool updating = layerUpdating(layer);

    Whf whf(getWidth(hnd), getHeight(hnd), hnd->format);
    Dim crop(src.left, src.top, srcW, srcH);
    Dim dest(dst.left, dst.top, dstW, dstH);

    ovutils::PipeArgs pargs(ovutils::OV_MDP_FLAGS_NONE,
                            whf,
                            Z_SYSTEM_ALLOC,
                            ovutils::ROT_FLAGS_NONE,
                            layer->planeAlpha,
                            (ovutils::eBlending)
                            getBlending(layer->blending));

    ALOGD_IF(HWC_UTILS_DEBUG, "%s: CursorInfo: w = %d h = %d "
        "crop [%d, %d, %d, %d] dst [%d, %d, %d, %d]", __FUNCTION__,
        getWidth(hnd), getHeight(hnd), src.left, src.top, srcW, srcH,
        dst.left, dst.top, dstW, dstH);

    return HWCursor::getInstance()->config(fd, (void*)hnd->base, pargs,
                crop, dest, updating);
}

void freeHwCursor(const int fd, int dpy) {
    if (dpy == HWC_DISPLAY_PRIMARY) {
        HWCursor::getInstance()->free(fd);
    }
}

int getRotDownscale(hwc_context_t *ctx, const hwc_layer_1_t *layer) {
    if(not qdutils::MDPVersion::getInstance().isRotDownscaleEnabled()) {
        return 0;
    }

    int downscale = 0;
    hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);
    hwc_rect_t dst = layer->displayFrame;
    private_handle_t *hnd = (private_handle_t *)layer->handle;

    if(not hnd) {
        return 0;
    }

    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
    bool isInterlaced = metadata && (metadata->operation & PP_PARAM_INTERLACED)
                && metadata->interlaced;
    int transform = layer->transform;
    uint32_t format = ovutils::getMdpFormat(hnd->format, hnd->flags);

    if(isYuvBuffer(hnd)) {
        if(ctx->mMDP.version >= qdutils::MDP_V4_2 &&
                ctx->mMDP.version < qdutils::MDSS_V5) {
            downscale = Rotator::getDownscaleFactor(crop.right - crop.left,
                    crop.bottom - crop.top, dst.right - dst.left,
                    dst.bottom - dst.top, format, isInterlaced);
        } else {
            Dim adjCrop(crop.left, crop.top, crop.right - crop.left,
                    crop.bottom - crop.top);
            Dim pos(dst.left, dst.top, dst.right - dst.left,
                    dst.bottom - dst.top);
            if(transform & HAL_TRANSFORM_ROT_90) {
                swap(adjCrop.w, adjCrop.h);
            }
            downscale = Rotator::getDownscaleFactor(adjCrop.w, adjCrop.h, pos.w,
                    pos.h, format, isInterlaced);
        }
    }
    return downscale;
}

bool isZoomModeEnabled(hwc_rect_t crop) {
    // This does not work for zooming in top left corner of the image
    return(crop.top > 0 || crop.left > 0);
}

void updateCropAIVVideoMode(hwc_context_t *ctx, hwc_rect_t& crop, int dpy) {
    ALOGD_IF(HWC_UTILS_DEBUG, "dpy %d Source crop [%d %d %d %d]", dpy,
             crop.left, crop.top, crop.right, crop.bottom);
    if(isZoomModeEnabled(crop)) {
        Dim srcCrop(crop.left, crop.top,
                crop.right - crop.left,
                crop.bottom - crop.top);
        int extW = ctx->dpyAttr[dpy].xres;
        int extH = ctx->dpyAttr[dpy].yres;
        //Crop the original video in order to fit external display aspect ratio
        if(srcCrop.w * extH < extW * srcCrop.h) {
            int offset = (srcCrop.h - ((srcCrop.w * extH) / extW)) / 2;
            crop.top += offset;
            crop.bottom -= offset;
        } else {
            int offset = (srcCrop.w - ((extW * srcCrop.h) / extH)) / 2;
            crop.left += offset;
            crop.right -= offset;
        }
        ALOGD_IF(HWC_UTILS_DEBUG, "External Resolution [%d %d] dpy %d Modified"
                 " source crop [%d %d %d %d]", extW, extH, dpy,
                 crop.left, crop.top, crop.right, crop.bottom);
    }
}

void updateDestAIVVideoMode(hwc_context_t *ctx, hwc_rect_t crop,
                           hwc_rect_t& dst, int dpy) {
    ALOGD_IF(HWC_UTILS_DEBUG, "dpy %d Destination position [%d %d %d %d]", dpy,
             dst.left, dst.top, dst.right, dst.bottom);
    Dim srcCrop(crop.left, crop.top,
            crop.right - crop.left,
            crop.bottom - crop.top);
    int extW = ctx->dpyAttr[dpy].xres;
    int extH = ctx->dpyAttr[dpy].yres;
    // Set the destination coordinates of external display to full screen,
    // when zoom in mode is enabled or the ratio between video aspect ratio
    // and external display aspect ratio is below the minimum tolerance level
    // and above maximum tolerance level
    float videoAspectRatio = ((float)srcCrop.w / (float)srcCrop.h);
    float extDisplayAspectRatio = ((float)extW / (float)extH);
    float videoToExternalRatio = videoAspectRatio / extDisplayAspectRatio;
    if((fabs(1.0f - videoToExternalRatio) <= ctx->mAspectRatioToleranceLevel) ||
        (isZoomModeEnabled(crop))) {
        dst.left = 0;
        dst.top = 0;
        dst.right = extW;
        dst.bottom = extH;
    }
    ALOGD_IF(HWC_UTILS_DEBUG, "External Resolution [%d %d] dpy %d Modified"
             " Destination position [%d %d %d %d] Source crop [%d %d %d %d]",
             extW, extH, dpy, dst.left, dst.top, dst.right, dst.bottom,
             crop.left, crop.top, crop.right, crop.bottom);
}

void updateCoordinates(hwc_context_t *ctx, hwc_rect_t& crop,
                           hwc_rect_t& dst, int dpy) {
    updateCropAIVVideoMode(ctx, crop, dpy);
    updateDestAIVVideoMode(ctx, crop, dst, dpy);
}

int configureNonSplit(hwc_context_t *ctx, hwc_layer_1_t *layer,
        const int& dpy, eMdpFlags& mdpFlags, eZorder& z,
        const eDest& dest, Rotator **rot) {

    private_handle_t *hnd = (private_handle_t *)layer->handle;

    if(!hnd) {
        if (layer->flags & HWC_COLOR_FILL) {
            // Configure Color layer
            return configColorLayer(ctx, layer, dpy, mdpFlags, z, dest);
        }
        ALOGE("%s: layer handle is NULL", __FUNCTION__);
        return -1;
    }

    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;

    hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);
    hwc_rect_t dst = layer->displayFrame;
    int transform = layer->transform;
    eTransform orient = static_cast<eTransform>(transform);
    int rotFlags = ovutils::ROT_FLAGS_NONE;
    uint32_t format = ovutils::getMdpFormat(hnd->format, hnd->flags);
    Whf whf(getWidth(hnd), getHeight(hnd), format, (uint32_t)hnd->size);

    // Handle R/B swap
    if (layer->flags & HWC_FORMAT_RB_SWAP) {
        if (hnd->format == HAL_PIXEL_FORMAT_RGBA_8888)
            whf.format = getMdpFormat(HAL_PIXEL_FORMAT_BGRA_8888);
        else if (hnd->format == HAL_PIXEL_FORMAT_RGBX_8888)
            whf.format = getMdpFormat(HAL_PIXEL_FORMAT_BGRX_8888);
    }
    // update source crop and destination position of AIV video layer.
    if(ctx->listStats[dpy].mAIVVideoMode && isYuvBuffer(hnd)) {
        updateCoordinates(ctx, crop, dst, dpy);
    }
    calcExtDisplayPosition(ctx, hnd, dpy, crop, dst, transform, orient);
    int downscale = getRotDownscale(ctx, layer);
    setMdpFlags(ctx, layer, mdpFlags, downscale, transform);

    //if 90 component or downscale, use rot
    if((has90Transform(layer) or downscale) and isRotationDoable(ctx, hnd)) {
        *rot = ctx->mRotMgr->getNext();
        if(*rot == NULL) return -1;
        ctx->mLayerRotMap[dpy]->add(layer, *rot);
        BwcPM::setBwc(ctx, dpy, hnd, crop, dst, transform, downscale,
                mdpFlags);
        //Configure rotator for pre-rotation
        if(configRotator(*rot, whf, crop, mdpFlags, orient, downscale) < 0) {
            ALOGE("%s: configRotator failed!", __FUNCTION__);
            return -1;
        }
        updateSource(orient, whf, crop, *rot);
        rotFlags |= ROT_PREROTATED;
    }

    //For the mdp, since either we are pre-rotating or MDP does flips
    orient = OVERLAY_TRANSFORM_0;
    transform = 0;
    PipeArgs parg(mdpFlags, whf, z,
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
        const eDest& lDest, const eDest& rDest,
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
    int rotFlags = ROT_FLAGS_NONE;
    uint32_t format = ovutils::getMdpFormat(hnd->format, hnd->flags);
    Whf whf(getWidth(hnd), getHeight(hnd), format, (uint32_t)hnd->size);

    // Handle R/B swap
    if (layer->flags & HWC_FORMAT_RB_SWAP) {
        if (hnd->format == HAL_PIXEL_FORMAT_RGBA_8888)
            whf.format = getMdpFormat(HAL_PIXEL_FORMAT_BGRA_8888);
        else if (hnd->format == HAL_PIXEL_FORMAT_RGBX_8888)
            whf.format = getMdpFormat(HAL_PIXEL_FORMAT_BGRX_8888);
    }

    // update source crop and destination position of AIV video layer.
    if(ctx->listStats[dpy].mAIVVideoMode && isYuvBuffer(hnd)) {
        updateCoordinates(ctx, crop, dst, dpy);
    }

    /* Calculate the external display position based on MDP downscale,
       ActionSafe, and extorientation features. */
    calcExtDisplayPosition(ctx, hnd, dpy, crop, dst, transform, orient);
    int downscale = getRotDownscale(ctx, layer);
    setMdpFlags(ctx, layer, mdpFlagsL, downscale, transform);

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

    if((has90Transform(layer) or downscale) and isRotationDoable(ctx, hnd)) {
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
        PipeArgs pargL(mdpFlagsL, whf, z,
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
        PipeArgs pargR(mdpFlagsR, whf, z,
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

int configure3DVideo(hwc_context_t *ctx, hwc_layer_1_t *layer,
        const int& dpy, eMdpFlags& mdpFlagsL, eZorder& z,
        const eDest& lDest, const eDest& rDest,
        Rotator **rot) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(!hnd) {
        ALOGE("%s: layer handle is NULL", __FUNCTION__);
        return -1;
    }
    //Both pipes are configured to the same mixer
    eZorder lz = z;
    eZorder rz = (eZorder)(z + 1);

    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;

    hwc_rect_t crop = integerizeSourceCrop(layer->sourceCropf);
    hwc_rect_t dst = layer->displayFrame;
    int transform = layer->transform;
    eTransform orient = static_cast<eTransform>(transform);
    int rotFlags = ROT_FLAGS_NONE;
    uint32_t format = ovutils::getMdpFormat(hnd->format, hnd->flags);
    Whf whf(getWidth(hnd), getHeight(hnd), format, (uint32_t)hnd->size);

    int downscale = getRotDownscale(ctx, layer);
    setMdpFlags(ctx, layer, mdpFlagsL, downscale, transform);

    //XXX: Check if rotation is supported and valid for 3D
    if((has90Transform(layer) or downscale) and isRotationDoable(ctx, hnd)) {
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

    hwc_rect_t cropL = crop, dstL = dst;
    hwc_rect_t cropR = crop, dstR = dst;
    int hw_w = ctx->dpyAttr[dpy].xres;
    int hw_h = ctx->dpyAttr[dpy].yres;

    if(get3DFormat(hnd) == HAL_3D_SIDE_BY_SIDE_L_R ||
            get3DFormat(hnd) == HAL_3D_SIDE_BY_SIDE_R_L) {
        // Calculate Left rects
        // XXX: This assumes crop.right/2 is the center point of the video
        cropL.right = crop.right/2;
        dstL.left = dst.left/2;
        dstL.right = dst.right/2;

        // Calculate Right rects
        cropR.left = crop.right/2;
        dstR.left = hw_w/2 + dst.left/2;
        dstR.right = hw_w/2 + dst.right/2;
    } else if(get3DFormat(hnd) == HAL_3D_TOP_BOTTOM) {
        // Calculate Left rects
        cropL.bottom = crop.bottom/2;
        dstL.top = dst.top/2;
        dstL.bottom = dst.bottom/2;

        // Calculate Right rects
        cropR.top = crop.bottom/2;
        dstR.top = hw_h/2 + dst.top/2;
        dstR.bottom = hw_h/2 + dst.bottom/2;
    } else {
        ALOGE("%s: Unsupported 3D mode ", __FUNCTION__);
        return -1;
    }

    //For the mdp, since either we are pre-rotating or MDP does flips
    orient = OVERLAY_TRANSFORM_0;
    transform = 0;

    //configure left pipe
    if(lDest != OV_INVALID) {
        PipeArgs pargL(mdpFlagsL, whf, lz,
                       static_cast<eRotFlags>(rotFlags), layer->planeAlpha,
                       (ovutils::eBlending) getBlending(layer->blending));

        if(configMdp(ctx->mOverlay, pargL, orient,
                cropL, dstL, metadata, lDest) < 0) {
            ALOGE("%s: commit failed for left mixer config", __FUNCTION__);
            return -1;
        }
    }

    //configure right pipe
    if(rDest != OV_INVALID) {
        PipeArgs pargR(mdpFlagsR, whf, rz,
                       static_cast<eRotFlags>(rotFlags),
                       layer->planeAlpha,
                       (ovutils::eBlending) getBlending(layer->blending));
        if(configMdp(ctx->mOverlay, pargR, orient,
                cropR, dstR, metadata, rDest) < 0) {
            ALOGE("%s: commit failed for right mixer config", __FUNCTION__);
            return -1;
        }
    }

    return 0;
}

int configureSourceSplit(hwc_context_t *ctx, hwc_layer_1_t *layer,
        const int& dpy, eMdpFlags& mdpFlagsL, eZorder& z,
        const eDest& lDest, const eDest& rDest,
        Rotator **rot) {
    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(!hnd) {
        ALOGE("%s: layer handle is NULL", __FUNCTION__);
        return -1;
    }

    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;

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
            getMdpFormat(hnd->format), (uint32_t)hnd->size);

    // update source crop and destination position of AIV video layer.
    if(ctx->listStats[dpy].mAIVVideoMode && isYuvBuffer(hnd)) {
        updateCoordinates(ctx, crop, dst, dpy);
    }

    /* Calculate the external display position based on MDP downscale,
       ActionSafe, and extorientation features. */
    calcExtDisplayPosition(ctx, hnd, dpy, crop, dst, transform, orient);

    setMdpFlags(ctx, layer, mdpFlagsL, 0, transform);
    trimLayer(ctx, dpy, transform, crop, dst);

    if(has90Transform(layer) && isRotationDoable(ctx, hnd)) {
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
        PipeArgs pargL(mdpFlagsL, whf, lz,
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
        PipeArgs pargR(mdpFlagsR, whf, rz,
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
    if(ctx->mOverlay->isDMAMultiplexingSupported() &&
            isSecondaryConnected(ctx) &&
            !ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].isPause) {
        /* mdss driver on certain targets support multiplexing of DMA pipe
         * in LINE and BLOCK modes for writeback panels.
         */
        if(dpy == HWC_DISPLAY_PRIMARY)
            return false;
    }
    if((ctx->mMDP.version == qdutils::MDP_V3_0_4)
          ||(ctx->mMDP.version == qdutils::MDP_V3_0_5))
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
    qdutils::MDPVersion& mdpHw = qdutils::MDPVersion::getInstance();
    if(ctx->dpyAttr[dpy].xres > mdpHw.getMaxMixerWidth()) {
        return true;
    }
    if(dpy == HWC_DISPLAY_PRIMARY && mdpHw.getRightSplit()) {
        return true;
    }
    return false;
}

//clear prev layer prop flags and realloc for current frame
void reset_layer_prop(hwc_context_t* ctx, int dpy, int numAppLayers) {
    if(ctx->layerProp[dpy]) {
       delete[] ctx->layerProp[dpy];
       ctx->layerProp[dpy] = NULL;
    }
    ctx->layerProp[dpy] = new LayerProp[numAppLayers];
}

bool isAbcInUse(hwc_context_t *ctx){
  return (ctx->enableABC && ctx->listStats[0].renderBufIndexforABC == 0);
}

void dumpBuffer(private_handle_t *ohnd, char *bufferName) {
    if (ohnd != NULL && ohnd->base) {
        char dumpFilename[PATH_MAX];
        bool bResult = false;
        int width = getWidth(ohnd);
        int height = getHeight(ohnd);
        int format = ohnd->format;
        //dummy aligned w & h.
        int alW = 0, alH = 0;
        int size = getBufferSizeAndDimensions(width, height, format, alW, alH);
        snprintf(dumpFilename, sizeof(dumpFilename), "/data/%s.%s.%dx%d.raw",
            bufferName,
            overlay::utils::getFormatString(utils::getMdpFormat(format)),
            width, height);
        FILE* fp = fopen(dumpFilename, "w+");
        if (NULL != fp) {
            bResult = (bool) fwrite((void*)ohnd->base, size, 1, fp);
            fclose(fp);
        }
        ALOGD("Buffer[%s] Dump to %s: %s",
        bufferName, dumpFilename, bResult ? "Success" : "Fail");
    }
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

void processBootAnimCompleted(hwc_context_t *ctx) {
    char value[PROPERTY_VALUE_MAX];
    int boot_finished = 0, ret = -1;
    int (*applyMode)(int) = NULL;
    void *modeHandle = NULL;

    // Reading property set on boot finish in SF
    property_get("service.bootanim.exit", value, "0");
    boot_finished = atoi(value);
    if (!boot_finished)
        return;

    modeHandle = dlopen("libmm-qdcm.so", RTLD_NOW);
    if (modeHandle) {
        *(void **)&applyMode = dlsym(modeHandle, "applyDefaults");
        if (applyMode) {
            ret = applyMode(HWC_DISPLAY_PRIMARY);
            if (ret)
                ALOGD("%s: Not able to apply default mode", __FUNCTION__);
        } else {
            ALOGE("%s: No symbol applyDefaults found", __FUNCTION__);
        }
        dlclose(modeHandle);
    } else {
        ALOGE("%s: Not able to load libmm-qdcm.so", __FUNCTION__);
    }

    ctx->mBootAnimCompleted = true;
}

void BwcPM::setBwc(const hwc_context_t *ctx, const int& dpy,
        const private_handle_t *hnd,
        const hwc_rect_t& crop, const hwc_rect_t& dst,
        const int& transform,const int& downscale,
        ovutils::eMdpFlags& mdpFlags) {
    //Target doesnt support Bwc
    qdutils::MDPVersion& mdpHw = qdutils::MDPVersion::getInstance();
    if(not mdpHw.supportsBWC()) {
        return;
    }
    //Disabled at runtime
    if(not ctx->mBWCEnabled) return;
    //BWC not supported with rot-downscale
    if(downscale) return;
    //Not enabled for secondary displays
    if(dpy) return;
    //Not enabled for non-video buffers
    if(not isYuvBuffer(hnd)) return;

    int src_w = crop.right - crop.left;
    int src_h = crop.bottom - crop.top;
    int dst_w = dst.right - dst.left;
    int dst_h = dst.bottom - dst.top;
    if(transform & HAL_TRANSFORM_ROT_90) {
        swap(src_w, src_h);
    }
    //src width > MAX mixer supported dim
    if(src_w > (int) qdutils::MDPVersion::getInstance().getMaxPipeWidth()) {
        return;
    }
    //H/w requirement for BWC only. Pipe can still support 4096
    if(src_h > 4092) {
        return;
    }
    //Decimation necessary, cannot use BWC. H/W requirement.
    if(qdutils::MDPVersion::getInstance().supportsDecimation()) {
        uint8_t horzDeci = 0;
        uint8_t vertDeci = 0;
        ovutils::getDecimationFactor(src_w, src_h, dst_w, dst_h, horzDeci,
                vertDeci);
        if(horzDeci || vertDeci) return;
    }

    ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDSS_MDP_BWC_EN);
}

void LayerRotMap::add(hwc_layer_1_t* layer, Rotator *rot) {
    if(mCount >= RotMgr::MAX_ROT_SESS) return;
    mLayer[mCount] = layer;
    mRot[mCount] = rot;
    mCount++;
}

void LayerRotMap::reset() {
    for (int i = 0; i < RotMgr::MAX_ROT_SESS; i++) {
        mLayer[i] = 0;
        mRot[i] = 0;
    }
    mCount = 0;
}

void LayerRotMap::clear() {
    RotMgr::getInstance()->markUnusedTop(mCount);
    reset();
}

bool LayerRotMap::isRotCached(uint32_t index) const {
    overlay::Rotator* rot = getRot(index);
    hwc_layer_1_t* layer =  getLayer(index);

    if(rot and layer and layer->handle) {
        private_handle_t *hnd = (private_handle_t *)(layer->handle);
        return (rot->isRotCached(hnd->fd,(uint32_t)(hnd->offset)));
    }
    return false;
}

void LayerRotMap::setReleaseFd(const int& fence) {
    for(uint32_t i = 0; i < mCount; i++) {
        if(mRot[i] and mLayer[i] and mLayer[i]->handle) {
            /* Ensure that none of the above (Rotator-instance,
             * layer and layer-handle) are NULL*/
            if(isRotCached(i))
                mRot[i]->setPrevBufReleaseFd(dup(fence));
            else
                mRot[i]->setCurrBufReleaseFd(dup(fence));
        }
    }
}

hwc_rect expandROIFromMidPoint(hwc_rect roi, hwc_rect fullFrame) {
    int lRoiWidth = 0, rRoiWidth = 0;
    int half_frame_width = fullFrame.right/2;

    hwc_rect lFrame = fullFrame;
    hwc_rect rFrame = fullFrame;
    lFrame.right = (lFrame.right - lFrame.left)/2;
    rFrame.left = lFrame.right;

    hwc_rect lRoi = getIntersection(roi, lFrame);
    hwc_rect rRoi = getIntersection(roi, rFrame);

    lRoiWidth = lRoi.right - lRoi.left;
    rRoiWidth = rRoi.right - rRoi.left;

    if(lRoiWidth && rRoiWidth) {
        if(lRoiWidth < rRoiWidth)
            roi.left = half_frame_width - rRoiWidth;
        else
            roi.right = half_frame_width + lRoiWidth;
    }
    return roi;
}

void resetROI(hwc_context_t *ctx, const int dpy) {
    const int fbXRes = (int)ctx->dpyAttr[dpy].xres;
    const int fbYRes = (int)ctx->dpyAttr[dpy].yres;

    /* When source split is enabled, both the panels are calibrated
     * in a single coordinate system. So only one ROI is generated
     * for the whole panel extending equally from the midpoint and
     * populated for the left side. */
    if(!qdutils::MDPVersion::getInstance().isSrcSplit() &&
            isDisplaySplit(ctx, dpy)) {
        const int lSplit = getLeftSplit(ctx, dpy);
        ctx->listStats[dpy].lRoi = (struct hwc_rect){0, 0, lSplit, fbYRes};
        ctx->listStats[dpy].rRoi = (struct hwc_rect){lSplit, 0, fbXRes, fbYRes};
    } else  {
        ctx->listStats[dpy].lRoi = (struct hwc_rect){0, 0,fbXRes, fbYRes};
        ctx->listStats[dpy].rRoi = (struct hwc_rect){0, 0, 0, 0};
    }
}

hwc_rect_t getSanitizeROI(struct hwc_rect roi, hwc_rect boundary)
{
   if(!isValidRect(roi))
      return roi;

   struct hwc_rect t_roi = roi;

   const int LEFT_ALIGN = qdutils::MDPVersion::getInstance().getLeftAlign();
   const int WIDTH_ALIGN = qdutils::MDPVersion::getInstance().getWidthAlign();
   const int TOP_ALIGN = qdutils::MDPVersion::getInstance().getTopAlign();
   const int HEIGHT_ALIGN = qdutils::MDPVersion::getInstance().getHeightAlign();
   const int MIN_WIDTH = qdutils::MDPVersion::getInstance().getMinROIWidth();
   const int MIN_HEIGHT = qdutils::MDPVersion::getInstance().getMinROIHeight();

   /* Align to minimum width recommended by the panel */
   if((t_roi.right - t_roi.left) < MIN_WIDTH) {
       if((t_roi.left + MIN_WIDTH) > boundary.right)
           t_roi.left = t_roi.right - MIN_WIDTH;
       else
           t_roi.right = t_roi.left + MIN_WIDTH;
   }

  /* Align to minimum height recommended by the panel */
   if((t_roi.bottom - t_roi.top) < MIN_HEIGHT) {
       if((t_roi.top + MIN_HEIGHT) > boundary.bottom)
           t_roi.top = t_roi.bottom - MIN_HEIGHT;
       else
           t_roi.bottom = t_roi.top + MIN_HEIGHT;
   }

   /* Align left and width to meet panel restrictions */
   if(LEFT_ALIGN)
       t_roi.left = t_roi.left - (t_roi.left % LEFT_ALIGN);

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

void handle_pause(hwc_context_t* ctx, int dpy) {
    if(ctx->dpyAttr[dpy].connected) {
        ctx->mDrawLock.lock();
        ctx->dpyAttr[dpy].isActive = true;
        ctx->dpyAttr[dpy].isPause = true;
        ctx->mDrawLock.unlock();
        ctx->proc->invalidate(ctx->proc);

        usleep(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period
               * 2 / 1000);

        // At this point all the pipes used by External have been
        // marked as UNSET.
        ctx->mDrawLock.lock();
        // Perform commit to unstage the pipes.
        if (!Overlay::displayCommit(ctx->dpyAttr[dpy].fd)) {
            ALOGE("%s: display commit fail! for %d dpy",
                  __FUNCTION__, dpy);
        }
        ctx->mDrawLock.unlock();
        ctx->proc->invalidate(ctx->proc);
    }
    return;
}

void handle_resume(hwc_context_t* ctx, int dpy) {
    if(ctx->dpyAttr[dpy].connected) {
        ctx->mDrawLock.lock();
        ctx->dpyAttr[dpy].isConfiguring = true;
        ctx->dpyAttr[dpy].isActive = true;
        ctx->mDrawLock.unlock();
        ctx->proc->invalidate(ctx->proc);

        usleep(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period
               * 2 / 1000);

        //At this point external has all the pipes it would need.
        ctx->mDrawLock.lock();
        ctx->dpyAttr[dpy].isPause = false;
        ctx->mDrawLock.unlock();
        ctx->proc->invalidate(ctx->proc);
    }
    return;
}

void clearPipeResources(hwc_context_t* ctx, int dpy) {
    if(ctx->mOverlay) {
        ctx->mOverlay->configBegin();
        ctx->mOverlay->configDone();
    }
    if(ctx->mRotMgr) {
        ctx->mRotMgr->clear();
    }
    // Call a display commit to ensure that pipes and associated
    // fd's are cleaned up.
    if(!Overlay::displayCommit(ctx->dpyAttr[dpy].fd)) {
        ALOGE("%s: display commit failed for  %d", __FUNCTION__, dpy);
    }
}

// Handles online events when HDMI is the primary display. In particular,
// online events for hdmi connected before AND after boot up and HWC init.
void handle_online(hwc_context_t* ctx, int dpy) {
    // Close the current fd if it was opened earlier on when HWC
    // was initialized.
    if (ctx->dpyAttr[dpy].fd >= 0) {
        close(ctx->dpyAttr[dpy].fd);
        ctx->dpyAttr[dpy].fd = -1;
    }
    // TODO: If HDMI is connected after the display has booted up,
    // and the best configuration is different from the default
    // then we need to deal with this appropriately.
    int error = ctx->mHDMIDisplay->configure();
    if (error < 0) {
        ALOGE("Error opening FrameBuffer");
        return;
    }
    updateDisplayInfo(ctx, dpy);
    initCompositionResources(ctx, dpy);
    ctx->dpyAttr[dpy].connected = true;
}

// Handles offline events for HDMI. This can be used for offline events
// initiated by the HDMI driver and the CEC framework.
void handle_offline(hwc_context_t* ctx, int dpy) {
    destroyCompositionResources(ctx, dpy);
    // Clear all pipe resources and call a display commit to ensure
    // that all the fd's are closed. This will ensure that the HDMI
    // core turns off and that we receive an event the next time the
    // cable is connected.
    if (ctx->mHDMIDisplay->isHDMIPrimaryDisplay()) {
        clearPipeResources(ctx, dpy);
    }
    ctx->mHDMIDisplay->teardown();
    resetDisplayInfo(ctx, dpy);
    ctx->dpyAttr[dpy].connected = false;
    ctx->dpyAttr[dpy].isActive = false;
}

int convertS3DFormatToMode(int s3DFormat) {
    int ret;
    switch(s3DFormat) {
    case HAL_3D_SIDE_BY_SIDE_L_R:
    case HAL_3D_SIDE_BY_SIDE_R_L:
        ret = HDMI_S3D_SIDE_BY_SIDE;
        break;
    case HAL_3D_TOP_BOTTOM:
        ret = HDMI_S3D_TOP_AND_BOTTOM;
        break;
    default:
        ret = HDMI_S3D_NONE;
    }
    return ret;
}

bool needs3DComposition(hwc_context_t* ctx, int dpy) {
    return (displaySupports3D(ctx, dpy) && ctx->dpyAttr[dpy].connected &&
            ctx->dpyAttr[dpy].s3dMode != HDMI_S3D_NONE);
}

void setup3DMode(hwc_context_t *ctx, int dpy, int s3dMode) {
    if (ctx->dpyAttr[dpy].s3dMode != s3dMode) {
        ALOGD("%s: setup 3D mode: %d", __FUNCTION__, s3dMode);
        if(ctx->mHDMIDisplay->configure3D(s3dMode)) {
            ctx->dpyAttr[dpy].s3dMode = s3dMode;
        }
    }
}

bool displaySupports3D(hwc_context_t* ctx, int dpy) {
    return ((dpy == HWC_DISPLAY_EXTERNAL) ||
            ((dpy == HWC_DISPLAY_PRIMARY) &&
             ctx->mHDMIDisplay->isHDMIPrimaryDisplay()));
}


};//namespace qhwc
