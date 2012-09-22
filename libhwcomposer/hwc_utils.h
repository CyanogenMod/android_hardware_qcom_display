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

#include <hardware/hwcomposer.h>
#include <gralloc_priv.h>

#define ALIGN_TO(x, align)     (((x) + ((align)-1)) & ~((align)-1))
#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

//Fwrd decls
struct hwc_context_t;
struct framebuffer_device_t;

namespace hwcService {
class HWComposerService;
}

namespace overlay {
class Overlay;
}

namespace qhwc {
//fwrd decl
class QueuedBufferStore;
class ExternalDisplay;
class CopybitEngine;

struct MDPInfo {
    int version;
    char panel;
    bool hasOverlay;
};

enum external_display_type {
    EXT_TYPE_NONE,
    EXT_TYPE_HDMI,
    EXT_TYPE_WIFI
};
enum HWCCompositionType {
    HWC_USE_GPU = HWC_FRAMEBUFFER, // This layer is to be handled by
                                   // Surfaceflinger
    HWC_USE_OVERLAY = HWC_OVERLAY, // This layer is to be handled by the overlay
    HWC_USE_BACKGROUND
                = HWC_BACKGROUND,  // This layer is to be handled by TBD
    HWC_USE_COPYBIT                // This layer is to be handled by copybit
};

enum {
    HWC_MDPCOMP = 0x00000002,
    HWC_LAYER_RESERVED_0 = 0x00000004,
    HWC_LAYER_RESERVED_1 = 0x00000008
};


// -----------------------------------------------------------------------------
// Utility functions - implemented in hwc_utils.cpp
void dumpLayer(hwc_layer_t const* l);
void getLayerStats(hwc_context_t *ctx, const hwc_layer_list_t *list);
void initContext(hwc_context_t *ctx);
void closeContext(hwc_context_t *ctx);
//Crops source buffer against destination and FB boundaries
void calculate_crop_rects(hwc_rect_t& crop, hwc_rect_t& dst,
        const int fbWidth, const int fbHeight);

// Waits for the fb_post to be called
void wait4fbPost(hwc_context_t* ctx);

// Waits for the fb_post to finish PAN (primary commit)
void wait4Pan(hwc_context_t* ctx);

// Inline utility functions
static inline bool isSkipLayer(const hwc_layer_t* l) {
    return (UNLIKELY(l && (l->flags & HWC_SKIP_LAYER)));
}

// Returns true if the buffer is yuv
static inline bool isYuvBuffer(const private_handle_t* hnd) {
    return (hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO));
}

// Returns true if the buffer is secure
static inline bool isSecureBuffer(const private_handle_t* hnd) {
    return (hnd && (private_handle_t::PRIV_FLAGS_SECURE_BUFFER & hnd->flags));
}
//Return true if buffer is marked locked
static inline bool isBufferLocked(const private_handle_t* hnd) {
    return (hnd && (private_handle_t::PRIV_FLAGS_HWC_LOCK & hnd->flags));
}

//Return true if buffer is for external display only
static inline bool isExtOnly(const private_handle_t* hnd) {
    return (hnd && (hnd->flags & private_handle_t::PRIV_FLAGS_EXTERNAL_ONLY));
}

//Return true if buffer is for external display only with a BLOCK flag.
static inline bool isExtBlock(const private_handle_t* hnd) {
    return (hnd && (hnd->flags & private_handle_t::PRIV_FLAGS_EXTERNAL_BLOCK));
}

//Return true if buffer is for external display only with a Close Caption flag.
static inline bool isExtCC(const private_handle_t* hnd) {
    return (hnd && (hnd->flags & private_handle_t::PRIV_FLAGS_EXTERNAL_CC));
}

// Initialize uevent thread
void init_uevent_thread(hwc_context_t* ctx);

// Initialize vsync thread
void init_vsync_thread(hwc_context_t* ctx);

inline void getLayerResolution(const hwc_layer_t* layer,
                                         int& width, int& height)
{
    hwc_rect_t displayFrame  = layer->displayFrame;
    width = displayFrame.right - displayFrame.left;
    height = displayFrame.bottom - displayFrame.top;
}
}; //qhwc namespace

struct vsync_state {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    bool enable;
};

// -----------------------------------------------------------------------------
// HWC context
// This structure contains overall state
struct hwc_context_t {
    hwc_composer_device_t device;
    int numHwLayers;
    int overlayInUse;
    int deviceOrientation;
    int swapInterval;
    double dynThreshold;

    //Framebuffer device
    framebuffer_device_t *mFbDev;

    //Copybit Engine
    qhwc::CopybitEngine* mCopybitEngine;

    //Overlay object - NULL for non overlay devices
    overlay::Overlay *mOverlay;

    //QueuedBufferStore to hold buffers for overlay
    qhwc::QueuedBufferStore *qbuf;

    //HWComposerService object
    hwcService::HWComposerService *mHwcService;

    // External display related information
    qhwc::ExternalDisplay *mExtDisplay;

    qhwc::MDPInfo mMDP;

    //Vsync
    struct vsync_state vstate;

    // flag that indicate secure session status
    bool mSecure;

    // flag that indicate whether secure/desecure session in progress
    bool mSecureConfig;
    bool hdmi_pending;
    char  mHDMIEvent[512];
};

#endif //HWC_UTILS_H
