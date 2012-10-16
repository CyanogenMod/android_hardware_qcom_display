/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, The Linux Foundation All rights reserved.
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

#include <EGL/egl.h>
#include <overlay.h>
#include <cutils/properties.h>
#include <gralloc_priv.h>
#include <fb_priv.h>
#include "hwc_utils.h"
#include "mdp_version.h"
#include "hwc_video.h"
#include "external.h"
#include "hwc_mdpcomp.h"
#include "QService.h"

namespace qhwc {

// Opens Framebuffer device
static void openFramebufferDevice(hwc_context_t *ctx)
{
    hw_module_t const *module;
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0) {
        framebuffer_open(module, &(ctx->mFbDev));
        private_module_t* m = reinterpret_cast<private_module_t*>(
                ctx->mFbDev->common.module);
        //xres, yres may not be 32 aligned
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres = m->info.xres;
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres = m->info.yres;
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xdpi = ctx->mFbDev->xdpi;
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].ydpi = ctx->mFbDev->ydpi;
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period =
                1000000000l / ctx->mFbDev->fps;
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].fd = openFb(HWC_DISPLAY_PRIMARY);
    }
}

void initContext(hwc_context_t *ctx)
{
    openFramebufferDevice(ctx);
    overlay::Overlay::initOverlay();
    for(uint32_t i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        ctx->mOverlay[i] = overlay::Overlay::getInstance(i);
    }
    ctx->mQService = qService::QService::getInstance(ctx);
    ctx->mMDP.version = qdutils::MDPVersion::getInstance().getMDPVersion();
    ctx->mMDP.hasOverlay = qdutils::MDPVersion::getInstance().hasOverlay();
    ctx->mMDP.panel = qdutils::MDPVersion::getInstance().getPanelType();
    ctx->mExtDisplay = new ExternalDisplay(ctx);
    MDPComp::init(ctx);

    pthread_mutex_init(&(ctx->vstate.lock), NULL);
    pthread_cond_init(&(ctx->vstate.cond), NULL);
    ctx->vstate.enable = false;

    ALOGI("Initializing Qualcomm Hardware Composer");
    ALOGI("MDP version: %d", ctx->mMDP.version);
}

void closeContext(hwc_context_t *ctx)
{
    for(uint32_t i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        if(ctx->mOverlay[i]) {
            delete ctx->mOverlay[i];
            ctx->mOverlay[i] = NULL;
        }
    }

    if(ctx->mFbDev) {
        framebuffer_close(ctx->mFbDev);
        ctx->mFbDev = NULL;
        close(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].fd);
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].fd = -1;
    }

    if(ctx->mExtDisplay) {
        delete ctx->mExtDisplay;
        ctx->mExtDisplay = NULL;
    }

    pthread_mutex_destroy(&(ctx->vstate.lock));
    pthread_cond_destroy(&(ctx->vstate.cond));

}

void dumpLayer(hwc_layer_1_t const* l)
{
    ALOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}"
          ", {%d,%d,%d,%d}",
          l->compositionType, l->flags, l->handle, l->transform, l->blending,
          l->sourceCrop.left,
          l->sourceCrop.top,
          l->sourceCrop.right,
          l->sourceCrop.bottom,
          l->displayFrame.left,
          l->displayFrame.top,
          l->displayFrame.right,
          l->displayFrame.bottom);
}

void setListStats(hwc_context_t *ctx,
        const hwc_display_contents_1_t *list, int dpy) {

    ctx->listStats[dpy].numAppLayers = list->numHwLayers - 1;
    ctx->listStats[dpy].fbLayerIndex = list->numHwLayers - 1;
    ctx->listStats[dpy].yuvCount = 0;
    ctx->listStats[dpy].yuvIndex = -1;
    ctx->listStats[dpy].skipCount = 0;

    for (size_t i = 0; i < list->numHwLayers; i++) {
        private_handle_t *hnd =
            (private_handle_t *)list->hwLayers[i].handle;

        if(list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) {
            continue;
        //We disregard FB being skip for now! so the else if
        } else if (isSkipLayer(&list->hwLayers[i])) {
            ctx->listStats[dpy].skipCount++;
        }

        if (UNLIKELY(isYuvBuffer(hnd))) {
            ctx->listStats[dpy].yuvCount++;
            ctx->listStats[dpy].yuvIndex = i;
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

//Crops source buffer against destination and FB boundaries
void calculate_crop_rects(hwc_rect_t& crop, hwc_rect_t& dst,
        const int fbWidth, const int fbHeight, int orient) {
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

    float leftCutRatio = 0.0f, rightCutRatio = 0.0f, topCutRatio = 0.0f,
            bottomCutRatio = 0.0f;

    if(dst_l < 0) {
        leftCutRatio = (float)(0.0f - dst_l) / (float)dst_w;
        dst_l = 0;
    }
    if(dst_r > fbWidth) {
        rightCutRatio = (float)(dst_r - fbWidth) / (float)dst_w;
        dst_r = fbWidth;
    }
    if(dst_t < 0) {
        topCutRatio = (float)(0 - dst_t) / (float)dst_h;
        dst_t = 0;
    }
    if(dst_b > fbHeight) {
        bottomCutRatio = (float)(dst_b - fbHeight) / (float)dst_h;
        dst_b = fbHeight;
    }

    calc_cut(leftCutRatio, topCutRatio, rightCutRatio, bottomCutRatio, orient);
    crop_l += crop_w * leftCutRatio;
    crop_t += crop_h * topCutRatio;
    crop_r -= crop_w * rightCutRatio;
    crop_b -= crop_h * bottomCutRatio;
}

bool isExternalActive(hwc_context_t* ctx) {
    return ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].isActive;
}

int hwc_sync(hwc_context_t *ctx, hwc_display_contents_1_t* list, int dpy) {
    int ret = 0;
#ifdef USE_FENCE_SYNC
    struct mdp_buf_sync data;
    int acquireFd[4];
    int count = 0;
    int releaseFd = -1;
    int fbFd = -1;
    data.flags = MDP_BUF_SYNC_FLAG_WAIT;
    data.acq_fen_fd = acquireFd;
    data.rel_fen_fd = &releaseFd;
    //Accumulate acquireFenceFds
    for(uint32_t i = 0; i < list->numHwLayers; i++) {
        if((list->hwLayers[i].compositionType == HWC_OVERLAY ||
            list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET) &&
            list->hwLayers[i].acquireFenceFd != -1) {
            acquireFd[count++] = list->hwLayers[i].acquireFenceFd;
        }
    }

    data.acq_fen_fd_cnt = count;
    fbFd = ctx->dpyAttr[dpy].fd;

    //Waits for acquire fences, returns a release fence
    ret = ioctl(fbFd, MSMFB_BUFFER_SYNC, &data);
    if(ret < 0) {
        ALOGE("ioctl MSMFB_BUFFER_SYNC failed, err=%s",
                strerror(errno));
    }

    for(uint32_t i = 0; i < list->numHwLayers; i++) {
        if((list->hwLayers[i].compositionType == HWC_OVERLAY ||
            list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET)) {
            //Close the acquireFenceFds
            if(list->hwLayers[i].acquireFenceFd > 0) {
                close(list->hwLayers[i].acquireFenceFd);
                list->hwLayers[i].acquireFenceFd = -1;
            }
            //Populate releaseFenceFds.
            list->hwLayers[i].releaseFenceFd = dup(releaseFd);
        }
    }
    list->retireFenceFd = releaseFd;
#endif
    return ret;
}

};//namespace
