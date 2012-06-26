/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
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

#include "hwc_utils.h"
#include "mdp_version.h"
#include "hwc_video.h"
#include "hwc_ext_observer.h"
namespace qhwc {
void initContext(hwc_context_t *ctx)
{
    //XXX: target specific initializations here
    openFramebufferDevice(ctx);
    ctx->mOverlay = overlay::Overlay::getInstance();
    ctx->qbuf = new QueuedBufferStore();
    ctx->mdpVersion = qdutils::MDPVersion::getInstance().getMDPVersion();
    ctx->hasOverlay = qdutils::MDPVersion::getInstance().hasOverlay();
    ALOGI("MDP version: %d",ctx->mdpVersion);

    ctx->mExtDisplayObserver = ExtDisplayObserver::getInstance();
    ctx->mExtDisplayObserver->setHwcContext(ctx);
}

void closeContext(hwc_context_t *ctx)
{
    if(ctx->mOverlay) {
        delete ctx->mOverlay;
        ctx->mOverlay = NULL;
    }

    if(ctx->fbDev) {
        framebuffer_close(ctx->fbDev);
        ctx->fbDev = NULL;
    }

    if(ctx->qbuf) {
        delete ctx->qbuf;
        ctx->qbuf = NULL;
    }
}

// Opens Framebuffer device
void openFramebufferDevice(hwc_context_t *ctx) {
    hw_module_t const *module;
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0) {
        framebuffer_open(module, &(ctx->fbDev));
    }
}

void dumpLayer(hwc_layer_t const* l)
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

void getLayerStats(hwc_context_t *ctx, const hwc_layer_list_t *list)
{
    //Video specific stats
    int yuvCount = 0;
    int yuvLayerIndex = -1;
    bool isYuvLayerSkip = false;

    for (size_t i = 0; i < list->numHwLayers; i++) {
        private_handle_t *hnd =
            (private_handle_t *)list->hwLayers[i].handle;

        if (isYuvBuffer(hnd)) {
            yuvCount++;
            yuvLayerIndex = i;
            //Animating
            if (isSkipLayer(&list->hwLayers[i])) {
                isYuvLayerSkip = true;
            }
        } else if (isSkipLayer(&list->hwLayers[i])) { //Popups
            //If video layer is below a skip layer
            if(yuvLayerIndex != -1 && yuvLayerIndex < (ssize_t)i) {
                isYuvLayerSkip = true;
            }
        }
    }

    VideoOverlay::setStats(yuvCount, yuvLayerIndex, isYuvLayerSkip);

    ctx->numHwLayers = list->numHwLayers;
    return;
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

};//namespace
