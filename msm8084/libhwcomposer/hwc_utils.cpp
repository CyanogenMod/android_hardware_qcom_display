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
#include <linux/fb.h>
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

    return 0;
}

void initContext(hwc_context_t *ctx)
{
    openFramebufferDevice(ctx);
    ctx->mMDP.version = qdutils::MDPVersion::getInstance().getMDPVersion();
    ctx->mMDP.hasOverlay = qdutils::MDPVersion::getInstance().hasOverlay();
    ctx->mMDP.panel = qdutils::MDPVersion::getInstance().getPanelType();
    overlay::Overlay::initOverlay();
    ctx->mOverlay = overlay::Overlay::getInstance();
    ctx->mRotMgr = new RotMgr();

    //Is created and destroyed only once for primary
    //For external it could get created and destroyed multiple times depending
    //on what external we connect to.
    ctx->mFBUpdate[HWC_DISPLAY_PRIMARY] =
        IFBUpdate::getObject(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres,
        HWC_DISPLAY_PRIMARY);

    // Check if the target supports copybit compostion (dyn/mdp/c2d) to
    // decide if we need to open the copybit module.
    int compositionType =
        qdutils::QCCompositionType::getInstance().getCompositionType();

    if (compositionType & (qdutils::COMPOSITION_TYPE_DYN |
                           qdutils::COMPOSITION_TYPE_MDP |
                           qdutils::COMPOSITION_TYPE_C2D)) {
            ctx->mCopyBit[HWC_DISPLAY_PRIMARY] = new CopyBit();
    }

    ctx->mExtDisplay = new ExternalDisplay(ctx);

    ctx->mMDPComp[HWC_DISPLAY_PRIMARY] =
         MDPComp::getObject(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres,
         HWC_DISPLAY_PRIMARY);

    for (uint32_t i = 0; i < MAX_DISPLAYS; i++) {
        ctx->mHwcDebug[i] = new HwcDebug(i);
    }
    MDPComp::init(ctx);

    pthread_mutex_init(&(ctx->vstate.lock), NULL);
    pthread_cond_init(&(ctx->vstate.cond), NULL);
    ctx->vstate.enable = false;
    ctx->vstate.fakevsync = false;
    ctx->mExtDispConfiguring = false;
    ctx->mExtOrientation = 0;

    //Right now hwc starts the service but anybody could do it, or it could be
    //independent process as well.
    QService::init();
    sp<IQClient> client = new QClient(ctx);
    interface_cast<IQService>(
            defaultServiceManager()->getService(
            String16("display.qservice")))->connect(client);

    // Initialize "No animation on external display" related  parameters.
    ctx->deviceOrientation = 0;
    ctx->mPrevCropVideo.left = ctx->mPrevCropVideo.top =
        ctx->mPrevCropVideo.right = ctx->mPrevCropVideo.bottom = 0;
    ctx->mPrevDestVideo.left = ctx->mPrevDestVideo.top =
        ctx->mPrevDestVideo.right = ctx->mPrevDestVideo.bottom = 0;
    ctx->mPrevTransformVideo = 0;

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

    for(int i = 0; i < MAX_DISPLAYS; i++) {
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

    for(int i = 0; i < MAX_DISPLAYS; i++) {
        if(ctx->mFBUpdate[i]) {
            delete ctx->mFBUpdate[i];
            ctx->mFBUpdate[i] = NULL;
        }
        if(ctx->mMDPComp[i]) {
            delete ctx->mMDPComp[i];
            ctx->mMDPComp[i] = NULL;
        }
        if(ctx->mHwcDebug[i]) {
            delete ctx->mHwcDebug[i];
            ctx->mHwcDebug[i] = NULL;
        }
    }


    pthread_mutex_destroy(&(ctx->vstate.lock));
    pthread_cond_destroy(&(ctx->vstate.cond));
}


void dumpsys_log(android::String8& buf, const char* fmt, ...)
{
    va_list varargs;
    va_start(varargs, fmt);
    buf.appendFormatV(fmt, varargs);
    va_end(varargs);
}

/* Calculates the destination position based on the action safe rectangle */
void getActionSafePosition(hwc_context_t *ctx, int dpy, uint32_t& x,
                           uint32_t& y, uint32_t& w, uint32_t& h) {

    // if external supports underscan, do nothing
    // it will be taken care in the driver
    if(ctx->mExtDisplay->isCEUnderscanSupported())
        return;

    float wRatio = 1.0;
    float hRatio = 1.0;
    float xRatio = 1.0;
    float yRatio = 1.0;

    float fbWidth = ctx->dpyAttr[dpy].xres;
    float fbHeight = ctx->dpyAttr[dpy].yres;

    float asX = 0;
    float asY = 0;
    float asW = fbWidth;
    float asH= fbHeight;
    char value[PROPERTY_VALUE_MAX];

    // Apply action safe parameters
    property_get("persist.sys.actionsafe.width", value, "0");
    int asWidthRatio = atoi(value);
    property_get("persist.sys.actionsafe.height", value, "0");
    int asHeightRatio = atoi(value);
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

    return;
}

/* Calculates the aspect ratio for external based on the primary */
void getAspectRatioPosition(hwc_context_t *ctx, int dpy, int orientation,
                        uint32_t& x, uint32_t& y, uint32_t& w, uint32_t& h) {
    int fbWidth  = ctx->dpyAttr[dpy].xres;
    int fbHeight = ctx->dpyAttr[dpy].yres;

    switch(orientation) {
        case HAL_TRANSFORM_ROT_90:
        case HAL_TRANSFORM_ROT_270:
            x = (fbWidth - (ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres
                        * fbHeight/ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres))/2;
            y = 0;
            w = (ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres *
                    fbHeight/ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres);
            h = fbHeight;
            break;
        default:
            //Do nothing
            break;
     }
    ALOGD_IF(HWC_UTILS_DEBUG, "%s: Position: x = %d, y = %d w = %d h = %d",
                    __FUNCTION__, x, y, w ,h);
}


bool needsScaling(hwc_layer_1_t const* layer) {
    int dst_w, dst_h, src_w, src_h;

    hwc_rect_t displayFrame  = layer->displayFrame;
    hwc_rect_t sourceCrop = layer->sourceCrop;

    dst_w = displayFrame.right - displayFrame.left;
    dst_h = displayFrame.bottom - displayFrame.top;

    src_w = sourceCrop.right - sourceCrop.left;
    src_h = sourceCrop.bottom - sourceCrop.top;

    if(((src_w != dst_w) || (src_h != dst_h)))
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

void setListStats(hwc_context_t *ctx,
        const hwc_display_contents_1_t *list, int dpy) {

    ctx->listStats[dpy].numAppLayers = list->numHwLayers - 1;
    ctx->listStats[dpy].fbLayerIndex = list->numHwLayers - 1;
    ctx->listStats[dpy].skipCount = 0;
    ctx->listStats[dpy].needsAlphaScale = false;
    ctx->listStats[dpy].preMultipliedAlpha = false;
    ctx->listStats[dpy].yuvCount = 0;
    char property[PROPERTY_VALUE_MAX];
    ctx->listStats[dpy].extOnlyLayerIndex = -1;
    ctx->listStats[dpy].isDisplayAnimating = false;

    for (size_t i = 0; i < list->numHwLayers; i++) {
        hwc_layer_1_t const* layer = &list->hwLayers[i];
        private_handle_t *hnd = (private_handle_t *)layer->handle;

        //reset stored yuv index
        ctx->listStats[dpy].yuvIndices[i] = -1;

        if(list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
            continue;
        //We disregard FB being skip for now! so the else if
        } else if (isSkipLayer(&list->hwLayers[i])) {
            ctx->listStats[dpy].skipCount++;
        }

        if (UNLIKELY(isYuvBuffer(hnd))) {
            int& yuvCount = ctx->listStats[dpy].yuvCount;
            ctx->listStats[dpy].yuvIndices[yuvCount] = i;
            yuvCount++;

            if(layer->transform & HWC_TRANSFORM_ROT_90) {
                if(ctx->mOverlay->isPipeTypeAttached(OV_MDP_PIPE_DMA)) {
                    ctx->isPaddingRound = true;
                }
                Overlay::setDMAMode(Overlay::DMA_BLOCK_MODE);
            }
        }
        if(layer->blending == HWC_BLENDING_PREMULT)
            ctx->listStats[dpy].preMultipliedAlpha = true;

        if(!ctx->listStats[dpy].needsAlphaScale)
            ctx->listStats[dpy].needsAlphaScale = isAlphaScaled(layer);

        if(UNLIKELY(isExtOnly(hnd))){
            ctx->listStats[dpy].extOnlyLayerIndex = i;
        }
        if (layer->flags & HWC_SCREENSHOT_ANIMATOR_LAYER) {
            ctx->listStats[dpy].isDisplayAnimating = true;
        }
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
    if(dpy) {
        //uncomment the below code for testing purpose.
        /* char value[PROPERTY_VALUE_MAX];
        property_get("sys.ext_orientation", value, "0");
        // Assuming the orientation value is in terms of HAL_TRANSFORM,
        // This needs mapping to HAL, if its in different convention
        ctx->mExtOrientation = atoi(value); */
        // Assuming the orientation value is in terms of HAL_TRANSFORM,
        // This needs mapping to HAL, if its in different convention
        if(ctx->mExtOrientation) {
            ALOGD_IF(HWC_UTILS_DEBUG, "%s: ext orientation = %d",
                     __FUNCTION__, ctx->mExtOrientation);
            if(ctx->mOverlay->isPipeTypeAttached(OV_MDP_PIPE_DMA)) {
                ctx->isPaddingRound = true;
            }
            Overlay::setDMAMode(Overlay::DMA_BLOCK_MODE);
        }
    }
}


static inline void calc_cut(float& leftCutRatio, float& topCutRatio,
        float& rightCutRatio, float& bottomCutRatio, int orient) {
    if(orient & HAL_TRANSFORM_FLIP_H) {
        swap(leftCutRatio, rightCutRatio);
    }
    if(orient & HAL_TRANSFORM_FLIP_V) {
        swap(topCutRatio, bottomCutRatio);
    }
    if(orient & HAL_TRANSFORM_ROT_90) {
        //Anti clock swapping
        float tmpCutRatio = leftCutRatio;
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

    float leftCutRatio = 0.0f, rightCutRatio = 0.0f, topCutRatio = 0.0f,
            bottomCutRatio = 0.0f;

    if(dst_l < sci_l) {
        leftCutRatio = (float)(sci_l - dst_l) / (float)dst_w;
        dst_l = sci_l;
    }

    if(dst_r > sci_r) {
        rightCutRatio = (float)(dst_r - sci_r) / (float)dst_w;
        dst_r = sci_r;
    }

    if(dst_t < sci_t) {
        topCutRatio = (float)(sci_t - dst_t) / (float)dst_h;
        dst_t = sci_t;
    }

    if(dst_b > sci_b) {
        bottomCutRatio = (float)(dst_b - sci_b) / (float)dst_h;
        dst_b = sci_b;
    }

    calc_cut(leftCutRatio, topCutRatio, rightCutRatio, bottomCutRatio, orient);
    crop_l += crop_w * leftCutRatio;
    crop_t += crop_h * topCutRatio;
    crop_r -= crop_w * rightCutRatio;
    crop_b -= crop_h * bottomCutRatio;
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
        nwr.left   = min(nwr.left, displayFrame.left);
        nwr.top    = min(nwr.top, displayFrame.top);
        nwr.right  = max(nwr.right, displayFrame.right);
        nwr.bottom = max(nwr.bottom, displayFrame.bottom);
    }

    //Intersect with the framebuffer
    nwr.left   = max(nwr.left, fbDisplayFrame.left);
    nwr.top    = max(nwr.top, fbDisplayFrame.top);
    nwr.right  = min(nwr.right, fbDisplayFrame.right);
    nwr.bottom = min(nwr.bottom, fbDisplayFrame.bottom);

}

bool isExternalActive(hwc_context_t* ctx) {
    return ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].isActive;
}

void closeAcquireFds(hwc_display_contents_1_t* list) {
    for(uint32_t i = 0; list && i < list->numHwLayers; i++) {
        //Close the acquireFenceFds
        //HWC_FRAMEBUFFER are -1 already by SF, rest we close.
        if(list->hwLayers[i].acquireFenceFd >= 0) {
            close(list->hwLayers[i].acquireFenceFd);
            list->hwLayers[i].acquireFenceFd = -1;
        }
    }
}

int hwc_sync(hwc_context_t *ctx, hwc_display_contents_1_t* list, int dpy,
                                                        int fd) {
    int ret = 0;
    struct mdp_buf_sync data;
    int acquireFd[MAX_NUM_LAYERS];
    int count = 0;
    int releaseFd = -1;
    int fbFd = -1;
    memset(&data, 0, sizeof(data));
    bool swapzero = false;
    data.flags = MDP_BUF_SYNC_FLAG_WAIT;
    data.acq_fen_fd = acquireFd;
    data.rel_fen_fd = &releaseFd;
    char property[PROPERTY_VALUE_MAX];
    if(property_get("debug.egl.swapinterval", property, "1") > 0) {
        if(atoi(property) == 0)
            swapzero = true;
    }
    bool isExtAnimating = false;
    if(dpy)
       isExtAnimating = ctx->listStats[dpy].isDisplayAnimating;

    //Accumulate acquireFenceFds
    for(uint32_t i = 0; i < list->numHwLayers; i++) {
        if(list->hwLayers[i].compositionType == HWC_OVERLAY &&
                        list->hwLayers[i].acquireFenceFd != -1) {
            if(UNLIKELY(swapzero))
                acquireFd[count++] = -1;
            else
                acquireFd[count++] = list->hwLayers[i].acquireFenceFd;
        }
        if(list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
            if(UNLIKELY(swapzero))
                acquireFd[count++] = -1;
            else if(fd != -1) {
                //set the acquireFD from fd - which is coming from c2d
                acquireFd[count++] = fd;
                // Buffer sync IOCTL should be async when using c2d fence is
                // used
                data.flags &= ~MDP_BUF_SYNC_FLAG_WAIT;
            } else if(list->hwLayers[i].acquireFenceFd != -1)
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
        if(list->hwLayers[i].compositionType == HWC_OVERLAY ||
           list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
            //Populate releaseFenceFds.
            if(UNLIKELY(swapzero))
                list->hwLayers[i].releaseFenceFd = -1;
            else if(isExtAnimating) {
                // Release all the app layer fds immediately,
                // if animation is in progress.
                hwc_layer_1_t const* layer = &list->hwLayers[i];
                private_handle_t *hnd = (private_handle_t *)layer->handle;
                if(isYuvBuffer(hnd)) {
                    list->hwLayers[i].releaseFenceFd = dup(releaseFd);
                } else
                    list->hwLayers[i].releaseFenceFd = -1;
            } else
                list->hwLayers[i].releaseFenceFd = dup(releaseFd);
        }
    }

    if(fd >= 0) {
        close(fd);
        fd = -1;
    }

    if (ctx->mCopyBit[dpy])
        ctx->mCopyBit[dpy]->setReleaseFd(releaseFd);
    // if external is animating, close the relaseFd
    if(isExtAnimating) {
        close(releaseFd);
        releaseFd = -1;
    }
    if(UNLIKELY(swapzero)){
        list->retireFenceFd = -1;
        close(releaseFd);
    } else {
        list->retireFenceFd = releaseFd;
    }

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
    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;

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
            // enable bandwidth compression only if src width < 2048
            if(qdutils::MDPVersion::getInstance().supportsBWC() &&
                hnd->width < qdutils::MAX_DISPLAY_DIM) {
                ovutils::setMdpFlags(mdpFlags,
                                 ovutils::OV_MDSS_MDP_BWC_EN);
            }
        }
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

inline int configRotator(Rotator *rot, const Whf& whf,
        const hwc_rect_t& crop, const eMdpFlags& mdpFlags,
        const eTransform& orient, const int& downscale) {
    Dim rotCrop(crop.left, crop.top, (crop.right - crop.left),
        (crop.bottom - crop.top));
    rot->setSource(whf);
    rot->setCrop(rotCrop);
    rot->setFlags(mdpFlags);
    rot->setTransform(orient);
    rot->setDownscale(downscale);
    if(!rot->commit()) return -1;
    return 0;
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
    orient = static_cast<eTransform>(ovutils::getMdpOrient(orient));
    preRotateSource(orient, whf, srcCrop);
    if (qdutils::MDPVersion::getInstance().getMDPVersion() >=
        qdutils::MDSS_V5) {
        // Source for overlay will be the cropped (and rotated)
        crop.left = 0;
        crop.top = 0;
        crop.right = srcCrop.w;
        crop.bottom = srcCrop.h;
        // Set width & height equal to sourceCrop w & h
        whf.w = srcCrop.w;
        whf.h = srcCrop.h;
    } else {
        crop.left = srcCrop.x;
        crop.top = srcCrop.y;
        crop.right = srcCrop.x + srcCrop.w;
        crop.bottom = srcCrop.y + srcCrop.h;
    }
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

    hwc_rect_t crop = layer->sourceCrop;
    hwc_rect_t dst = layer->displayFrame;
    int transform = layer->transform;
    eTransform orient = static_cast<eTransform>(transform);
    int downscale = 0;
    int rotFlags = ovutils::ROT_FLAGS_NONE;
    Whf whf(hnd->width, hnd->height,
            getMdpFormat(hnd->format), hnd->size);

    if(dpy && isYuvBuffer(hnd)) {
        if(!ctx->listStats[dpy].isDisplayAnimating) {
            ctx->mPrevCropVideo = crop;
            ctx->mPrevDestVideo = dst;
            ctx->mPrevTransformVideo = transform;
        } else {
            // Restore the previous crop, dest rect and transform values, during
            // animation to avoid displaying videos at random coordinates.
            crop = ctx->mPrevCropVideo;
            dst = ctx->mPrevDestVideo;
            transform = ctx->mPrevTransformVideo;
            //In you tube use case when a device rotated from landscape to
            // portrait, set the isFg flag and zOrder to avoid displaying UI on
            // hdmi during animation
            if(ctx->deviceOrientation) {
                isFg = ovutils::IS_FG_SET;
                z = ZORDER_1;
            }
        }
    }

    uint32_t x = dst.left, y  = dst.right;
    uint32_t w = dst.right - dst.left;
    uint32_t h = dst.bottom - dst.top;

    if(dpy && ctx->mExtOrientation) {
        // Just need to set the position to portrait as the transformation
        // will already be set to required orientation on TV
        getAspectRatioPosition(ctx, dpy, ctx->mExtOrientation, x, y, w, h);
        // Convert position to hwc_rect_t
        dst.left = x;
        dst.top = y;
        dst.right = w + dst.left;
        dst.bottom = h + dst.top;
    }

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
    trimLayer(ctx, dpy, transform, crop, dst);

    if(isYuvBuffer(hnd) && //if 90 component or downscale, use rot
            ((transform & HWC_TRANSFORM_ROT_90) || downscale)) {
        *rot = ctx->mRotMgr->getNext();
        if(*rot == NULL) return -1;
        //Configure rotator for pre-rotation
        if(configRotator(*rot, whf, crop, mdpFlags, orient, downscale) < 0) {
            ALOGE("%s: configRotator failed!", __FUNCTION__);
            return -1;
        }
        whf.format = (*rot)->getDstFormat();
        updateSource(orient, whf, crop);
        rotFlags |= ovutils::ROT_PREROTATED;
    }

    //For the mdp, since either we are pre-rotating or MDP does flips
    orient = OVERLAY_TRANSFORM_0;
    transform = 0;
    PipeArgs parg(mdpFlags, whf, z, isFg, static_cast<eRotFlags>(rotFlags));
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
    hwc_rect_t crop = layer->sourceCrop;
    hwc_rect_t dst = layer->displayFrame;
    int transform = layer->transform;
    eTransform orient = static_cast<eTransform>(transform);
    const int downscale = 0;
    int rotFlags = ROT_FLAGS_NONE;

    Whf whf(hnd->width, hnd->height,
            getMdpFormat(hnd->format), hnd->size);

    if(dpy && isYuvBuffer(hnd)) {
        if(!ctx->listStats[dpy].isDisplayAnimating) {
            ctx->mPrevCropVideo = crop;
            ctx->mPrevDestVideo = dst;
            ctx->mPrevTransformVideo = transform;
        } else {
            // Restore the previous crop, dest rect and transform values, during
            // animation to avoid displaying videos at random coordinates.
            crop = ctx->mPrevCropVideo;
            dst = ctx->mPrevDestVideo;
            transform = ctx->mPrevTransformVideo;
            //In you tube use case when a device rotated from landscape to
            // portrait, set the isFg flag and zOrder to avoid displaying UI on
            // hdmi during animation
            if(ctx->deviceOrientation) {
                isFg = ovutils::IS_FG_SET;
                z = ZORDER_1;
            }
        }
    }


    setMdpFlags(layer, mdpFlagsL);
    trimLayer(ctx, dpy, transform, crop, dst);

    if(isYuvBuffer(hnd) && (transform & HWC_TRANSFORM_ROT_90)) {
        (*rot) = ctx->mRotMgr->getNext();
        if((*rot) == NULL) return -1;
        //Configure rotator for pre-rotation
        if(configRotator(*rot, whf, crop, mdpFlagsL, orient, downscale) < 0) {
            ALOGE("%s: configRotator failed!", __FUNCTION__);
            return -1;
        }
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

    //configure left mixer
    if(lDest != OV_INVALID) {
        PipeArgs pargL(mdpFlagsL, whf, z, isFg,
                static_cast<eRotFlags>(rotFlags));
        if(configMdp(ctx->mOverlay, pargL, orient,
                tmp_cropL, tmp_dstL, metadata, lDest) < 0) {
            ALOGE("%s: commit failed for left mixer config", __FUNCTION__);
            return -1;
        }
    }

    //configure right mixer
    if(rDest != OV_INVALID) {
        PipeArgs pargR(mdpFlagsR, whf, z, isFg,
                static_cast<eRotFlags>(rotFlags));
        tmp_dstR.right = tmp_dstR.right - hw_w/2;
        tmp_dstR.left = tmp_dstR.left - hw_w/2;
        if(configMdp(ctx->mOverlay, pargR, orient,
                tmp_cropR, tmp_dstR, metadata, rDest) < 0) {
            ALOGE("%s: commit failed for right mixer config", __FUNCTION__);
            return -1;
        }
    }

    return 0;
}

};//namespace qhwc
