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
    ctx->mOverlay = overlay::Overlay::getInstance();
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
    if(ctx->mOverlay) {
        delete ctx->mOverlay;
        ctx->mOverlay = NULL;
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

//Crops source buffer against destination and FB boundaries
void calculate_crop_rects(hwc_rect_t& crop, hwc_rect_t& dst,
        const int fbWidth, const int fbHeight) {

    int& crop_x = crop.left;
    int& crop_y = crop.top;
    int& crop_r = crop.right;
    int& crop_b = crop.bottom;
    int crop_w = crop.right - crop.left;
    int crop_h = crop.bottom - crop.top;

    int& dst_x = dst.left;
    int& dst_y = dst.top;
    int& dst_r = dst.right;
    int& dst_b = dst.bottom;
    int dst_w = dst.right - dst.left;
    int dst_h = dst.bottom - dst.top;

    if(dst_x < 0) {
        float scale_x =  crop_w * 1.0f / dst_w;
        float diff_factor = (scale_x * abs(dst_x));
        crop_x = crop_x + (int)diff_factor;
        crop_w = crop_r - crop_x;

        dst_x = 0;
        dst_w = dst_r - dst_x;;
    }
    if(dst_r > fbWidth) {
        float scale_x = crop_w * 1.0f / dst_w;
        float diff_factor = scale_x * (dst_r - fbWidth);
        crop_r = crop_r - diff_factor;
        crop_w = crop_r - crop_x;

        dst_r = fbWidth;
        dst_w = dst_r - dst_x;
    }
    if(dst_y < 0) {
        float scale_y = crop_h * 1.0f / dst_h;
        float diff_factor = scale_y * abs(dst_y);
        crop_y = crop_y + diff_factor;
        crop_h = crop_b - crop_y;

        dst_y = 0;
        dst_h = dst_b - dst_y;
    }
    if(dst_b > fbHeight) {
        float scale_y = crop_h * 1.0f / dst_h;
        float diff_factor = scale_y * (dst_b - fbHeight);
        crop_b = crop_b - diff_factor;
        crop_h = crop_b - crop_y;

        dst_b = fbHeight;
        dst_h = dst_b - dst_y;
    }
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
    data.flags = 0;
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

    if (count) {
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
    }
#endif
    return ret;
}

};//namespace
