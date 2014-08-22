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
#define HWC_UTILS_DEBUG 0
#include <sys/ioctl.h>
#include <binder/IServiceManager.h>
#include <EGL/egl.h>
#include <cutils/properties.h>
#include <gralloc_priv.h>
#include <overlay.h>
#include <overlayRotator.h>
#include "hwc_utils.h"
#include "hwc_mdpcomp.h"
#include "hwc_fbupdate.h"
#include "mdp_version.h"
#include "hwc_copybit.h"
#include "hwc_dump_layers.h"
#include "external.h"
#include "virtual.h"
#include "hwc_qclient.h"
#include "QService.h"
#include "comptype.h"

using namespace qClient;
using namespace qService;
using namespace android;
using namespace overlay;
using namespace overlay::utils;
namespace ovutils = overlay::utils;

namespace qhwc {

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

    return 0;
}

void initContext(hwc_context_t *ctx)
{
    if(openFramebufferDevice(ctx) < 0) {
        ALOGE("%s: failed to open framebuffer!!", __FUNCTION__);
    }

    ctx->mMDP.version = qdutils::MDPVersion::getInstance().getMDPVersion();
    ctx->mMDP.hasOverlay = qdutils::MDPVersion::getInstance().hasOverlay();
    ctx->mMDP.panel = qdutils::MDPVersion::getInstance().getPanelType();
    overlay::Overlay::initOverlay();
    ctx->mOverlay = overlay::Overlay::getInstance();
    ctx->mRotMgr = RotMgr::getInstance();

    //Is created and destroyed only once for primary
    //For external it could get created and destroyed multiple times depending
    //on what external we connect to.
    ctx->mFBUpdate[HWC_DISPLAY_PRIMARY] =
        IFBUpdate::getObject(ctx, ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres,
        HWC_DISPLAY_PRIMARY);

    // Check if the target supports copybit compostion (dyn/mdp/c2d) to
    // decide if we need to open the copybit module.
    int compositionType =
        qdutils::QCCompositionType::getInstance().getCompositionType();

    if (compositionType & (qdutils::COMPOSITION_TYPE_DYN |
                           qdutils::COMPOSITION_TYPE_MDP |
                           qdutils::COMPOSITION_TYPE_C2D)) {
            ctx->mCopyBit[HWC_DISPLAY_PRIMARY] = new CopyBit(ctx,
                    HWC_DISPLAY_PRIMARY);
    }

    ctx->mExtDisplay = new ExternalDisplay(ctx);
    ctx->mVirtualDisplay = new VirtualDisplay(ctx);
    ctx->mVirtualonExtActive = false;
    ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].isActive = false;
    ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].connected = false;
    ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].isActive = false;
    ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].connected = false;
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].mDownScaleMode= false;
    ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].mDownScaleMode = false;
    ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].mDownScaleMode = false;

    ctx->mMDPComp[HWC_DISPLAY_PRIMARY] =
         MDPComp::getObject(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres,
         HWC_DISPLAY_PRIMARY);
    ctx->dpyAttr[HWC_DISPLAY_PRIMARY].connected = true;

    for (uint32_t i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        ctx->mLayerRotMap[i] = new LayerRotMap();
        ctx->mAnimationState[i] = ANIMATION_STOPPED;
        ctx->mHwcDebug[i] = new HwcDebug(i);
        ctx->mPrevHwLayerCount[i] = 0;
    }

    MDPComp::init(ctx);

    ctx->vstate.enable = false;
    ctx->vstate.fakevsync = false;
    ctx->mBasePipeSetup = false;
    ctx->mExtOrientation = 0;

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
    ctx->mSocId = getSocIdFromSystem();
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

    if(ctx->mExtDisplay) {
        delete ctx->mExtDisplay;
        ctx->mExtDisplay = NULL;
    }

    for(int i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        if(ctx->mFBUpdate[i]) {
            delete ctx->mFBUpdate[i];
            ctx->mFBUpdate[i] = NULL;
        }
        if(ctx->mMDPComp[i]) {
            delete ctx->mMDPComp[i];
            ctx->mMDPComp[i] = NULL;
        }
        if(ctx->mLayerRotMap[i]) {
            delete ctx->mLayerRotMap[i];
            ctx->mLayerRotMap[i] = NULL;
        }
        if(ctx->mHwcDebug[i]) {
            delete ctx->mHwcDebug[i];
            ctx->mHwcDebug[i] = NULL;
        }
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

    // if external supports underscan, do nothing
    // it will be taken care in the driver
    if(ctx->mExtDisplay->isCEUnderscanSupported())
        return;

    char value[PROPERTY_VALUE_MAX];
    // Read action safe properties
    property_get("persist.sys.actionsafe.width", value, "0");
    int asWidthRatio = atoi(value);
    property_get("persist.sys.actionsafe.height", value, "0");
    int asHeightRatio = atoi(value);

    if(!asWidthRatio && !asHeightRatio) {
        //No action safe ratio set, return
        return;
    }

    float wRatio = 1.0;
    float hRatio = 1.0;
    float xRatio = 1.0;
    float yRatio = 1.0;

    int fbWidth = ctx->dpyAttr[dpy].xres;
    int fbHeight = ctx->dpyAttr[dpy].yres;
    if(ctx->dpyAttr[dpy].mDownScaleMode) {
        // if downscale Mode is enabled for external, need to query
        // the actual width and height, as that is the physical w & h
         ctx->mExtDisplay->getAttributes(fbWidth, fbHeight);
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

/* Calculates the aspect ratio for based on src & dest */
void getAspectRatioPosition(int destWidth, int destHeight, int srcWidth,
                                int srcHeight, hwc_rect_t& rect) {
   int x =0, y =0;

   if (srcWidth * destHeight > destWidth * srcHeight) {
        srcHeight = destWidth * srcHeight / srcWidth;
        srcWidth = destWidth;
    } else if (srcWidth * destHeight < destWidth * srcHeight) {
        srcWidth = destHeight * srcWidth / srcHeight;
        srcHeight = destHeight;
    } else {
        srcWidth = destWidth;
        srcHeight = destHeight;
    }
    if (srcWidth > destWidth) srcWidth = destWidth;
    if (srcHeight > destHeight) srcHeight = destHeight;
    x = (destWidth - srcWidth) / 2;
    y = (destHeight - srcHeight) / 2;
    ALOGD_IF(HWC_UTILS_DEBUG, "%s: AS Position: x = %d, y = %d w = %d h = %d",
             __FUNCTION__, x, y, srcWidth , srcHeight);
    // Convert it back to hwc_rect_t
    rect.left = x;
    rect.top = y;
    rect.right = srcWidth + rect.left;
    rect.bottom = srcHeight + rect.top;
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
        getAspectRatioPosition(fbWidth, fbHeight, (int)actualWidth,
                               (int)actualHeight, rect);
        xPos = rect.left;
        yPos = rect.top;
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
    }

    //Calculate the position...
    xRatio = inPos.x/actualWidth;
    yRatio = inPos.y/actualHeight;
    wRatio = inPos.w/actualWidth;
    hRatio = inPos.h/actualHeight;

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
        hwc_rect_t r;
        //Calculate the position
        xRatio = (outPos.x - xPos)/width;
        // GetaspectRatio -- tricky to get the correct aspect ratio
        // But we need to do this.
        getAspectRatioPosition(width, height, width, height, r);
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
        int extW, extH;
        if(dpy == HWC_DISPLAY_EXTERNAL)
            ctx->mExtDisplay->getAttributes(extW, extH);
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
    if(dpy) {
        if(!isYuvBuffer(hnd)) {
            if(extOrient & HWC_TRANSFORM_ROT_90) {
                int dstWidth = ctx->dpyAttr[dpy].xres;
                int dstHeight = ctx->dpyAttr[dpy].yres;;
                int srcWidth = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
                int srcHeight = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres;
                if(!isPrimaryPortrait(ctx)) {
                    swap(srcWidth, srcHeight);
                }                    // Get Aspect Ratio for external
                getAspectRatioPosition(dstWidth, dstHeight, srcWidth,
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
                int extW, extH;
                // if downscale is enabled, map the co-ordinates to new
                // domain(downscaled)
                float fbWidth  = ctx->dpyAttr[dpy].xres;
                float fbHeight = ctx->dpyAttr[dpy].yres;
                // query MDP configured attributes
                if(dpy == HWC_DISPLAY_EXTERNAL)
                    ctx->mExtDisplay->getAttributes(extW, extH);
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

bool needsScaling(hwc_context_t* ctx, hwc_layer_1_t const* layer,
        const int& dpy) {
    int dst_w, dst_h, src_w, src_h;

    hwc_rect_t displayFrame  = layer->displayFrame;
    hwc_rect_t sourceCrop = integerizeSourceCrop(layer->sourceCropf);
    trimLayer(ctx, dpy, layer->transform, sourceCrop, displayFrame);

    dst_w = displayFrame.right - displayFrame.left;
    dst_h = displayFrame.bottom - displayFrame.top;
    src_w = sourceCrop.right - sourceCrop.left;
    src_h = sourceCrop.bottom - sourceCrop.top;

    if(((src_w != dst_w) || (src_h != dst_h)))
        return true;

    return false;
}

bool isAlphaScaled(hwc_context_t* ctx, hwc_layer_1_t const* layer,
        const int& dpy) {
    if(needsScaling(ctx, layer, dpy) && isAlphaPresent(layer)) {
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

// Let CABL know we have a YUV layer
static void setYUVProp(int yuvCount) {
    static char property[PROPERTY_VALUE_MAX];
    if(yuvCount > 0) {
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
}

void setListStats(hwc_context_t *ctx,
        const hwc_display_contents_1_t *list, int dpy) {

    memset(&ctx->listStats[dpy], 0, sizeof(ListStats));
    ctx->listStats[dpy].numAppLayers = list->numHwLayers - 1;
    ctx->listStats[dpy].fbLayerIndex = list->numHwLayers - 1;
    ctx->listStats[dpy].skipCount = 0;
    ctx->listStats[dpy].needsAlphaScale = false;
    ctx->listStats[dpy].preMultipliedAlpha = false;
    ctx->listStats[dpy].planeAlpha = false;
    ctx->listStats[dpy].yuvCount = 0;
    ctx->listStats[dpy].extOnlyLayerIndex = -1;
    ctx->listStats[dpy].isDisplayAnimating = false;
    ctx->listStats[dpy].secureUI = false;
    optimizeLayerRects(ctx, list, dpy);

    for (size_t i = 0; i < (size_t)ctx->listStats[dpy].numAppLayers; i++) {
        hwc_layer_1_t const* layer = &list->hwLayers[i];
        private_handle_t *hnd = (private_handle_t *)layer->handle;

#ifdef QCOM_BSP
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

        if (isSkipLayer(&list->hwLayers[i])) {
            ctx->listStats[dpy].skipCount++;
        }

        if (UNLIKELY(isYuvBuffer(hnd))) {
            int& yuvCount = ctx->listStats[dpy].yuvCount;
            ctx->listStats[dpy].yuvIndices[yuvCount] = i;
            yuvCount++;

            if(layer->transform & HWC_TRANSFORM_ROT_90)
                ctx->mNeedsRotator = true;
        }
        if(layer->blending == HWC_BLENDING_PREMULT)
            ctx->listStats[dpy].preMultipliedAlpha = true;
        if(layer->planeAlpha < 0xFF)
            ctx->listStats[dpy].planeAlpha = true;
        if(!ctx->listStats[dpy].needsAlphaScale)
            ctx->listStats[dpy].needsAlphaScale =
                    isAlphaScaled(ctx, layer, dpy);

        if(UNLIKELY(isExtOnly(hnd))){
            ctx->listStats[dpy].extOnlyLayerIndex = i;
        }
    }

    if (ctx->listStats[dpy].yuvCount != 1) {
        ctx->mPrevWHF[dpy].w = 0;
        ctx->mPrevWHF[dpy].h = 0;
    }

    setYUVProp(ctx->listStats[dpy].yuvCount);
    if(dpy) {
        //uncomment the below code for testing purpose.
        /* char value[PROPERTY_VALUE_MAX];
        property_get("sys.ext_orientation", value, "0");
        // Assuming the orientation value is in terms of HAL_TRANSFORM,
        // This needs mapping to HAL, if its in different convention
        ctx->mExtOrientation = atoi(value); */
        // Assuming the orientation value is in terms of HAL_TRANSFORM,
        // This needs mapping to HAL, if its in different convention
        if(ctx->mExtOrientation || ctx->mBufferMirrorMode) {
            ALOGD_IF(HWC_UTILS_DEBUG, "%s: ext orientation = %d"
                     "BufferMirrorMode = %d", __FUNCTION__,
                     ctx->mExtOrientation, ctx->mBufferMirrorMode);
        }
    }

}


static inline void calc_cut(double& leftCutRatio, double& topCutRatio,
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
    //  On A-Family, Secure policy is applied system wide and not on
    //  buffers.
    if (isSecureModePolicy(ctx->mMDP.version)) {
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(ctx->mSecureMode) {
            if (! isSecureBuffer(hnd)) {
                // This code path executes for the following usecase:
                // Some Apps in which first few seconds, framework
                // sends non-secure buffer and with out destroying
                // surfaces, switches to secure buffer thereby exposing
                // vulnerability on A-family devices. Catch this situation
                // and handle it gracefully by allowing it to be composed by
                // GPU.
                ALOGD_IF(HWC_UTILS_DEBUG, "%s: Handle non-secure video layer"
                         "during secure playback gracefully", __FUNCTION__);
                return true;
            }
        } else {
            if (isSecureBuffer(hnd)) {
                // This code path executes for the following usecase:
                // For some Apps, when User terminates playback, Framework
                // doesnt destroy video surface and video surface still
                // comes to Display HAL. This exposes vulnerability on
                // A-family. Catch this situation and handle it gracefully
                // by allowing it to be composed by GPU.
                ALOGD_IF(HWC_UTILS_DEBUG, "%s: Handle secure video layer"
                         "during non-secure playback gracefully", __FUNCTION__);
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
    crop_l += crop_w * leftCutRatio;
    crop_t += crop_h * topCutRatio;
    crop_r -= crop_w * rightCutRatio;
    crop_b -= crop_h * bottomCutRatio;
}

bool isValidRect(const hwc_rect& rect)
{
   return ((rect.bottom > rect.top) && (rect.right > rect.left)) ;
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
        if(list->hwLayers[i].blending == HWC_BLENDING_NONE) {
            int j= i-1;
            hwc_rect_t& topframe =
                (hwc_rect_t&)list->hwLayers[i].displayFrame;
            while(j >= 0) {
               if(!needsScaling(ctx, &list->hwLayers[j], dpy)) {
                  hwc_layer_1_t* layer = (hwc_layer_1_t*)&list->hwLayers[j];
                  hwc_rect_t& bottomframe = layer->displayFrame;
                  hwc_rect_t& bottomCrop = layer->sourceCrop;
                  int transform =layer->transform;

                  hwc_rect_t irect = getIntersection(bottomframe, topframe);
                  if(isValidRect(irect)) {
                     hwc_rect_t dest_rect;
                     //if intersection is valid rect, deduct it
                     dest_rect  = deductRect(bottomframe, irect);
                     qhwc::calculate_crop_rects(bottomCrop, bottomframe,
                                                dest_rect, transform);

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
    }
}

int hwc_sync(hwc_context_t *ctx, hwc_display_contents_1_t* list, int dpy,
        int fd) {
    int ret = 0;
    int acquireFd[MAX_NUM_APP_LAYERS];
    int count = 0;
    int releaseFd = -1;
#ifdef USE_RETIRE_FENCE
    int retireFd = -1;
#endif
    int fbFd = -1;
    int rotFd = -1;
    bool swapzero = false;
    int mdpVersion = qdutils::MDPVersion::getInstance().getMDPVersion();
    LayerProp *layerProp = ctx->layerProp[dpy];

    struct mdp_buf_sync data;
    memset(&data, 0, sizeof(data));
    //Until B-family supports sync for rotator
    if(mdpVersion >= qdutils::MDSS_V5) {
        data.flags = MDP_BUF_SYNC_FLAG_WAIT;
    }
    data.acq_fen_fd = acquireFd;
    data.rel_fen_fd = &releaseFd;
#ifdef USE_RETIRE_FENCE
    data.retire_fen_fd = &retireFd;
#endif

    char property[PROPERTY_VALUE_MAX];
    if(property_get("debug.egl.swapinterval", property, "1") > 0) {
        if(atoi(property) == 0)
            swapzero = true;
    }
    bool isExtAnimating = false;
    if(dpy)
       isExtAnimating = ctx->listStats[dpy].isDisplayAnimating;

#ifndef MDSS_TARGET
    //Send acquireFenceFds to rotator
    if(mdpVersion < qdutils::MDSS_V5) {
        //A-family
        int rotFd = ctx->mRotMgr->getRotDevFd();
        struct msm_rotator_buf_sync rotData;

        for(uint32_t i = 0; i < ctx->mLayerRotMap[dpy]->getCount(); i++) {
            memset(&rotData, 0, sizeof(rotData));
            int& acquireFenceFd =
                ctx->mLayerRotMap[dpy]->getLayer(i)->acquireFenceFd;
            rotData.acq_fen_fd = acquireFenceFd;
            rotData.session_id = ctx->mLayerRotMap[dpy]->getRot(i)->getSessId();
            ioctl(rotFd, MSM_ROTATOR_IOCTL_BUFFER_SYNC, &rotData);
            close(acquireFenceFd);
            //For MDP to wait on.
            acquireFenceFd = dup(rotData.rel_fen_fd);
            //A buffer is free to be used by producer as soon as its copied to
            //rotator.
            ctx->mLayerRotMap[dpy]->getLayer(i)->releaseFenceFd =
                rotData.rel_fen_fd;
        }
    } else {
        //TODO B-family
    }
#endif

    //Accumulate acquireFenceFds for MDP
    for(uint32_t i = 0; i < list->numHwLayers; i++) {
        if((list->hwLayers[i].compositionType == HWC_OVERLAY &&
                        (layerProp[i].mFlags & HWC_MDPCOMP)) &&
                        list->hwLayers[i].acquireFenceFd >= 0) {
            if(UNLIKELY(swapzero))
                acquireFd[count++] = -1;
            else
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
        if(((list->hwLayers[i].compositionType == HWC_OVERLAY) &&
                (layerProp[i].mFlags & HWC_MDPCOMP)) ||
                list->hwLayers[i].compositionType == HWC_BLIT ||
                list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
            //Populate releaseFenceFds.
            if(UNLIKELY(swapzero)) {
                list->hwLayers[i].releaseFenceFd = -1;
            } else if(isExtAnimating) {
                // Release all the app layer fds immediately,
                // if animation is in progress.
                list->hwLayers[i].releaseFenceFd = -1;
            } else if(list->hwLayers[i].releaseFenceFd < 0) {
                //If rotator has not already populated this field.
                if(list->hwLayers[i].compositionType == HWC_BLIT) {
                    //For Blit, the app layers should be released when the Blit is
                    //complete. This fd was passed from copybit->draw
                    list->hwLayers[i].releaseFenceFd = dup(fd);
                } else {
                    list->hwLayers[i].releaseFenceFd = dup(releaseFd);
                }
            }
        }
    }

    if(fd >= 0) {
        close(fd);
        fd = -1;
    }

    if (ctx->mCopyBit[dpy])
        ctx->mCopyBit[dpy]->setReleaseFd(releaseFd);

    //A-family
    if(mdpVersion < qdutils::MDSS_V5) {
        //Signals when MDP finishes reading rotator buffers.
        ctx->mLayerRotMap[dpy]->setReleaseFd(releaseFd);
    }

    // if external is animating, close the relaseFd
    if(isExtAnimating) {
        close(releaseFd);
        releaseFd = -1;
    }

#ifdef USE_RETIRE_FENCE
    close(releaseFd);
    if(UNLIKELY(swapzero))
        list->retireFenceFd = -1;
    else
        list->retireFenceFd = retireFd;
#else
    if(UNLIKELY(swapzero)) {
        list->retireFenceFd = -1;
        close(releaseFd);
    } else {
        list->retireFenceFd = releaseFd;
    }
#endif
    return ret;
}

void trimLayer(hwc_context_t *ctx, const int& dpy, const int& transform,
        hwc_rect_t& crop, hwc_rect_t& dst) {
    int hw_w = ctx->dpyAttr[dpy].xres;
    int hw_h = ctx->dpyAttr[dpy].yres;
    if(dst.left < 0 || dst.top < 0 ||
            dst.right > hw_w || dst.bottom > hw_h) {
        hwc_rect_t scissor = {0, 0, hw_w, hw_h };
        qhwc::calculate_crop_rects(crop, dst, scissor, transform);
    }
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

inline int configRotator(Rotator *rot, Whf& whf,
        const Whf& origWhf, const eMdpFlags& mdpFlags,
        const eTransform& orient,
        const int& downscale) {
    // Fix alignments for TILED format
    if(whf.format == MDP_Y_CRCB_H2V2_TILE ||
                            whf.format == MDP_Y_CBCR_H2V2_TILE) {
        whf.w =  utils::alignup(whf.w, 64);
        whf.h = utils::alignup(whf.h, 32);
    }
    rot->setSource(whf, origWhf);
    rot->setFlags(mdpFlags);
    rot->setTransform(orient);
    rot->setDownscale(downscale);
    if(!rot->commit()) return -1;
    return 0;
}

/*
 * Sets up BORDERFILL as default base pipe and detaches RGB0.
 * Framebuffer is always updated using PLAY ioctl.
 */
bool setupBasePipe(hwc_context_t *ctx) {
    const int dpy = HWC_DISPLAY_PRIMARY;
    int fb_stride = ctx->dpyAttr[dpy].stride;
    int fb_width = ctx->dpyAttr[dpy].xres;
    int fb_height = ctx->dpyAttr[dpy].yres;
    int fb_fd = ctx->dpyAttr[dpy].fd;

    mdp_overlay ovInfo;
    msmfb_overlay_data ovData;
    memset(&ovInfo, 0, sizeof(mdp_overlay));
    memset(&ovData, 0, sizeof(msmfb_overlay_data));

    ovInfo.src.format = MDP_RGB_BORDERFILL;
    ovInfo.src.width  = fb_width;
    ovInfo.src.height = fb_height;
    ovInfo.src_rect.w = fb_width;
    ovInfo.src_rect.h = fb_height;
    ovInfo.dst_rect.w = fb_width;
    ovInfo.dst_rect.h = fb_height;
    ovInfo.id = MSMFB_NEW_REQUEST;

    if (ioctl(fb_fd, MSMFB_OVERLAY_SET, &ovInfo) < 0) {
        ALOGE("Failed to call ioctl MSMFB_OVERLAY_SET err=%s",
                strerror(errno));
        return false;
    }

    ovData.id = ovInfo.id;
    if (ioctl(fb_fd, MSMFB_OVERLAY_PLAY, &ovData) < 0) {
        ALOGE("Failed to call ioctl MSMFB_OVERLAY_PLAY err=%s",
                strerror(errno));
        return false;
    }
    ctx->mBasePipeSetup = true;
    return true;
}

ovutils::eDest getPipeForFb(hwc_context_t *ctx, int dpy) {
    ovutils::eDest dest = ovutils::OV_INVALID;
    overlay::Overlay& ov = *ctx->mOverlay;

    dest = ov.nextPipe(ovutils::OV_MDP_PIPE_RGB, dpy);
    if(dest != ovutils::OV_INVALID) {
        return dest;
    }

    return ov.nextPipe(ovutils::OV_MDP_PIPE_VG, dpy);
}

inline int configMdp(Overlay *ov, const PipeArgs& parg,
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

inline void updateSource(eTransform& orient, Whf& whf,
        hwc_rect_t& crop) {
    Dim srcCrop(crop.left, crop.top,
            crop.right - crop.left,
            crop.bottom - crop.top);
    //getMdpOrient will switch the flips if the source is 90 rotated.
    //Clients in Android dont factor in 90 rotation while deciding the flip.
    orient = static_cast<eTransform>(ovutils::getMdpOrient(orient));
    preRotateSource(orient, whf, srcCrop);
    crop.left = srcCrop.x;
    crop.top = srcCrop.y;
    crop.right = srcCrop.x + srcCrop.w;
    crop.bottom = srcCrop.y + srcCrop.h;
}

bool needToForceRotator(hwc_context_t *ctx, const int& dpy,
         uint32_t w, uint32_t h, int transform) {
    int nYuvCount = ctx->listStats[dpy].yuvCount;
    bool forceRot = false;
    //Force rotator for resolution change only if 1 yuv layer on primary
    if(nYuvCount == 1 && (!((transform & HWC_TRANSFORM_FLIP_H) ||
            (transform & HWC_TRANSFORM_FLIP_V)))) {
        uint32_t& prevWidth = ctx->mPrevWHF[dpy].w;
        uint32_t& prevHeight = ctx->mPrevWHF[dpy].h;
        if((prevWidth != w) || (prevHeight != h)) {
            uint32_t prevBufArea = prevWidth * prevHeight;
            if(prevBufArea) {
                forceRot = true;
            }
            prevWidth = w;
            prevHeight = h;
        }
    }
    return forceRot;
}

int configureLowRes(hwc_context_t *ctx, hwc_layer_1_t *layer,
        const int& dpy, eMdpFlags& mdpFlags, eZorder& z,
        eIsFg& isFg, const eDest& dest, Rotator **rot) {

    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(!hnd) {
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

    calcExtDisplayPosition(ctx, hnd, dpy, crop, dst, transform, orient);

    bool forceRot = false;
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

        forceRot = needToForceRotator(ctx, dpy, (uint32_t)getWidth(hnd),
                (uint32_t)getHeight(hnd), transform);
    }

    setMdpFlags(layer, mdpFlags, downscale, transform);
    trimLayer(ctx, dpy, transform, crop, dst);

    if(isYuvBuffer(hnd) && //if 90 component or downscale, use rot
            ((transform & HWC_TRANSFORM_ROT_90) || downscale || forceRot)) {
        *rot = ctx->mRotMgr->getNext();
        if(*rot == NULL) return -1;
        Whf origWhf(hnd->width, hnd->height,
                    getMdpFormat(hnd->format), hnd->size);
        if(configRotator(*rot, whf, origWhf,  mdpFlags, orient, downscale) < 0) {
        //Configure rotator for pre-rotation
            ALOGE("%s: configRotator failed!", __FUNCTION__);
            return -1;
        }
        ctx->mLayerRotMap[dpy]->add(layer, *rot);
        whf.format = (*rot)->getDstFormat();
        updateSource(orient, whf, crop);
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

int configureHighRes(hwc_context_t *ctx, hwc_layer_1_t *layer,
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

    bool forceRot = false;
    if(isYuvBuffer(hnd)) {
        forceRot = needToForceRotator(ctx, dpy, (uint32_t)getWidth(hnd),
                (uint32_t)getHeight(hnd), transform);
    }

    setMdpFlags(layer, mdpFlagsL, 0, transform);
    trimLayer(ctx, dpy, transform, crop, dst);

    if(isYuvBuffer(hnd) && ((transform & HWC_TRANSFORM_ROT_90) || forceRot)) {
        (*rot) = ctx->mRotMgr->getNext();
        if((*rot) == NULL) return -1;
        Whf origWhf(hnd->width, hnd->height,
                    getMdpFormat(hnd->format), hnd->size);
        if(configRotator(*rot, whf, origWhf, mdpFlagsL, orient, downscale) < 0) {
        //Configure rotator for pre-rotation
            ALOGE("%s: configRotator failed!", __FUNCTION__);
            return -1;
        }
        ctx->mLayerRotMap[dpy]->add(layer, *rot);
        whf.format = (*rot)->getDstFormat();
        updateSource(orient, whf, crop);
        rotFlags |= ROT_PREROTATED;
    }

    eMdpFlags mdpFlagsR = mdpFlagsL;
    setMdpFlags(mdpFlagsR, OV_MDSS_MDP_RIGHT_MIXER);

    hwc_rect_t tmp_cropL, tmp_dstL;
    hwc_rect_t tmp_cropR, tmp_dstR;

    if(lDest != OV_INVALID) {
        tmp_cropL = crop;
        tmp_dstL = dst;
        hwc_rect_t scissor = {0, 0, hw_w/2, hw_h };
        qhwc::calculate_crop_rects(tmp_cropL, tmp_dstL, scissor, 0);
    }
    if(rDest != OV_INVALID) {
        tmp_cropR = crop;
        tmp_dstR = dst;
        hwc_rect_t scissor = {hw_w/2, 0, hw_w, hw_h };
        qhwc::calculate_crop_rects(tmp_cropR, tmp_dstR, scissor, 0);
    }

    //When buffer is flipped, contents of mixer config also needs to swapped.
    //Not needed if the layer is confined to one half of the screen.
    //If rotator has been used then it has also done the flips, so ignore them.
    if((orient & OVERLAY_TRANSFORM_FLIP_V) && lDest != OV_INVALID
            && rDest != OV_INVALID && rot == NULL) {
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
                static_cast<eRotFlags>(rotFlags), layer->planeAlpha,
                (ovutils::eBlending) getBlending(layer->blending));

        tmp_dstR.right = tmp_dstR.right - tmp_dstR.left;
        tmp_dstR.left = 0;
        if(configMdp(ctx->mOverlay, pargR, orient,
                tmp_cropR, tmp_dstR, metadata, rDest) < 0) {
            ALOGE("%s: commit failed for right mixer config", __FUNCTION__);
            return -1;
        }
    }

    return 0;
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

int getSocIdFromSystem() {
    FILE *device = NULL;
    int soc_id = 0;
    char  buffer[10];
    int result;
    device = fopen("/sys/devices/system/soc/soc0/id","r");
    if(device != NULL) {
        result = fread (buffer,1,4,device);
        soc_id = atoi(buffer);
        fclose(device);
    }
    return soc_id;
}

};//namespace qhwc
