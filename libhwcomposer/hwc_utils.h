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

#ifndef HWC_UTILS_H
#define HWC_UTILS_H
#include <cutils/log.h>
#include <gralloc_priv.h>
#include <hardware/hwcomposer.h>
#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <stdlib.h>
#include <string.h>
#include <fb_priv.h>
#include <overlay.h>
#include <genlock.h>
#include "hwc_qbuf.h"

#define ALIGN(x, align)     (((x) + ((align)-1)) & ~((align)-1))
#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

struct hwc_context_t;
namespace qhwc {

enum external_display_type {
    EXT_TYPE_NONE,
    EXT_TYPE_HDMI,
    EXT_TYPE_WIFI
};


// -----------------------------------------------------------------------------
// Utility functions - implemented in hwc_utils.cpp
void dumpLayer(hwc_layer_t const* l);
void getLayerStats(hwc_context_t *ctx, const hwc_layer_list_t *list);
void handleYUV(hwc_context_t *ctx, hwc_layer_t *layer);
void initContext(hwc_context_t *ctx);
void closeContext(hwc_context_t *ctx);
void openFramebufferDevice(hwc_context_t *ctx);

// Inline utility functions
static inline bool isSkipLayer(const hwc_layer_t* l) {
    return (UNLIKELY(l && (l->flags & HWC_SKIP_LAYER)));
}

// Returns true if the buffer is yuv
static inline bool isYuvBuffer(const private_handle_t* hnd) {
    return (hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO));
}

//Return true if buffer is marked locked
static inline bool isBufferLocked(const private_handle_t* hnd) {
    return (hnd && (private_handle_t::PRIV_FLAGS_HWC_LOCK & hnd->flags));
}

// -----------------------------------------------------------------------------
// Overlay specific functions - inline or implemented in hwc_overlay.cpp
bool prepareOverlay(hwc_context_t *ctx, hwc_layer_t *layer);
//XXX: Refine draw functions
bool drawLayerUsingOverlay(hwc_context_t *ctx, hwc_layer_t *layer);
//XXX: Refine
void cleanOverlays(hwc_context_t *ctx );
void setOverlayState(hwc_context_t* ctx, ovutils::eOverlayState state);

// -----------------------------------------------------------------------------
// Copybit specific functions - inline or implemented in hwc_copybit.cpp



// -----------------------------------------------------------------------------
// HDMI specific functions - inline or implemented in hwc_hdmi.cpp



} //qhwc namespace



// -----------------------------------------------------------------------------
// HWC context
// This structure contains overall state
struct hwc_context_t {
    hwc_composer_device_t device;
    // Layer variables
    int yuvBufferCount;
    int hdmiEnabled;
    int numHwLayers;
    int mdpVersion;
    bool hasOverlay;
    bool skipComposition;

    //Framebuffer device
    framebuffer_device_t *fbDev;

    //Overlay object - NULL for non overlay devices
    overlay::Overlay *mOverlay;

    //QueuedBufferStore to hold buffers for overlay
    qhwc::QueuedBufferStore *qbuf;
};




#endif //HWC_UTILS_H
