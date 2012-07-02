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
    int yuvBufCount = 0;
    int layersNotUpdatingCount = 0;
    for (size_t i=0 ; i<list->numHwLayers; i++) {
        private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
        if (isYuvBuffer(hnd)) {
            yuvBufCount++;
        }
    }
    // Number of video/camera layers drawable with overlay
    ctx->yuvBufferCount = yuvBufCount;
    ctx->numHwLayers = list->numHwLayers;
    return;
}

void handleYUV(hwc_context_t *ctx, hwc_layer_t *layer)
{
    private_handle_t *hnd =
                   (private_handle_t *)layer->handle;
    //XXX: Handle targets not using overlay
    if(prepareOverlay(ctx, layer)) {
        layer->compositionType = HWC_OVERLAY;
        layer->hints |= HWC_HINT_CLEAR_FB;
    }
}
};//namespace
