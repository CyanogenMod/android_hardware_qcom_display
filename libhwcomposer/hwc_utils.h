/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C)2012-2013, The Linux Foundation. All rights reserved.
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

#ifndef HWC_UTILS_H
#define HWC_UTILS_H

#define HWC_REMOVE_DEPRECATED_VERSIONS 1
#include <fcntl.h>
#include <math.h>
#include <hardware/hwcomposer.h>
#include <gr.h>
#include <gralloc_priv.h>
#include <utils/String8.h>
#include <linux/fb.h>
#include "qdMetaData.h"
#include <overlayUtils.h>

#define ALIGN_TO(x, align)     (((x) + ((align)-1)) & ~((align)-1))
#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))
#define MAX_NUM_APP_LAYERS 32
#define MAX_DISPLAY_DIM 2048

//Fwrd decls
struct hwc_context_t;

namespace ovutils = overlay::utils;

namespace overlay {
class Overlay;
class Rotator;
class RotMgr;
}

namespace qhwc {
//fwrd decl
class QueuedBufferStore;
class ExternalDisplay;
class VirtualDisplay;
class IFBUpdate;
class IVideoOverlay;
class MDPComp;
class CopyBit;
class HwcDebug;


struct MDPInfo {
    int version;
    char panel;
    bool hasOverlay;
};

struct DisplayAttributes {
    uint32_t vsync_period; //nanos
    uint32_t xres;
    uint32_t yres;
    uint32_t stride;
    float xdpi;
    float ydpi;
    int fd;
    bool connected; //Applies only to pluggable disp.
    //Connected does not mean it ready to use.
    //It should be active also. (UNBLANKED)
    bool isActive;
    // In pause state, composition is bypassed
    // used for WFD displays only
    bool isPause;
    // To trigger padding round to clean up mdp
    // pipes
    bool isConfiguring;
    // External Display is in MDP Downscale mode indicator
    bool mDownScaleMode;
    // Ext dst Rect
    hwc_rect_t mDstRect;
};

struct ListStats {
    int numAppLayers; //Total - 1, excluding FB layer.
    int skipCount;
    int fbLayerIndex; //Always last for now. = numAppLayers
    //Video specific
    int yuvCount;
    int yuvIndices[MAX_NUM_APP_LAYERS];
    int extOnlyLayerIndex;
    bool needsAlphaScale;
    bool preMultipliedAlpha;
    bool planeAlpha;
    // Notifies hwcomposer about the start and end of animation
    // This will be set to true during animation, otherwise false.
    bool isDisplayAnimating;
    bool secureUI; // Secure display layer
};

struct LayerProp {
    uint32_t mFlags; //qcom specific layer flags
    LayerProp():mFlags(0) {};
};

struct VsyncState {
    bool enable;
    bool fakevsync;
};

// LayerProp::flag values
enum {
    HWC_MDPCOMP = 0x00000001,
    HWC_COPYBIT = 0x00000002,
};

class LayerRotMap {
public:
    LayerRotMap() { reset(); }
    enum { MAX_SESS = 3 };
    void add(hwc_layer_1_t* layer, overlay::Rotator *rot);
    //Resets the mapping of layer to rotator
    void reset();
    //Clears mappings and existing rotator fences
    //Intended to be used during errors
    void clear();
    uint32_t getCount() const;
    hwc_layer_1_t* getLayer(uint32_t index) const;
    overlay::Rotator* getRot(uint32_t index) const;
    void setReleaseFd(const int& fence);
private:
    hwc_layer_1_t* mLayer[MAX_SESS];
    overlay::Rotator* mRot[MAX_SESS];
    uint32_t mCount;
};

inline uint32_t LayerRotMap::getCount() const {
    return mCount;
}

inline hwc_layer_1_t* LayerRotMap::getLayer(uint32_t index) const {
    if(index >= mCount) return NULL;
    return mLayer[index];
}

inline overlay::Rotator* LayerRotMap::getRot(uint32_t index) const {
    if(index >= mCount) return NULL;
    return mRot[index];
}

inline hwc_rect_t integerizeSourceCrop(const hwc_frect_t& cropF) {
    hwc_rect_t cropI = {0};
    cropI.left = int(ceilf(cropF.left));
    cropI.top = int(ceilf(cropF.top));
    cropI.right = int(floorf(cropF.right));
    cropI.bottom = int(floorf(cropF.bottom));
    return cropI;
}

inline bool isNonIntegralSourceCrop(const hwc_frect_t& cropF) {
    if(cropF.left - roundf(cropF.left)     ||
       cropF.top - roundf(cropF.top)       ||
       cropF.right - roundf(cropF.right)   ||
       cropF.bottom - roundf(cropF.bottom))
        return true;
    else
        return false;
}

// -----------------------------------------------------------------------------
// Utility functions - implemented in hwc_utils.cpp
void dumpLayer(hwc_layer_1_t const* l);
void setListStats(hwc_context_t *ctx, const hwc_display_contents_1_t *list,
        int dpy);
void initContext(hwc_context_t *ctx);
void closeContext(hwc_context_t *ctx);
//Crops source buffer against destination and FB boundaries
void calculate_crop_rects(hwc_rect_t& crop, hwc_rect_t& dst,
                         const hwc_rect_t& scissor, int orient);
void getNonWormholeRegion(hwc_display_contents_1_t* list,
                              hwc_rect_t& nwr);
bool isSecuring(hwc_context_t* ctx, hwc_layer_1_t const* layer);
bool isSecureModePolicy(int mdpVersion);
bool isExternalActive(hwc_context_t* ctx);
bool needsScaling(hwc_context_t* ctx, hwc_layer_1_t const* layer, const int& dpy);
bool isAlphaPresent(hwc_layer_1_t const* layer);
bool setupBasePipe(hwc_context_t *ctx);
int hwc_vsync_control(hwc_context_t* ctx, int dpy, int enable);
int getBlending(int blending);

//Helper function to dump logs
void dumpsys_log(android::String8& buf, const char* fmt, ...);

int getExtOrientation(hwc_context_t* ctx);

bool isValidRect(const hwc_rect_t& rect);
hwc_rect_t deductRect(const hwc_rect_t& rect1, const hwc_rect_t& rect2);
hwc_rect_t getIntersection(const hwc_rect_t& rect1, const hwc_rect_t& rect2);
hwc_rect_t getUnion(const hwc_rect_t& rect1, const hwc_rect_t& rect2);
void optimizeLayerRects(hwc_context_t *ctx,
                        const hwc_display_contents_1_t *list, const int& dpy);

/* Calculates the destination position based on the action safe rectangle */
void getActionSafePosition(hwc_context_t *ctx, int dpy, hwc_rect_t& dst);

void getAspectRatioPosition(int destWidth, int destHeight, int srcWidth,
                                int srcHeight, hwc_rect_t& rect);

void getAspectRatioPosition(hwc_context_t* ctx, int dpy, int extOrientation,
                                hwc_rect_t& inRect, hwc_rect_t& outRect);

bool isPrimaryPortrait(hwc_context_t *ctx);

bool isOrientationPortrait(hwc_context_t *ctx);

void calcExtDisplayPosition(hwc_context_t *ctx,
                               private_handle_t *hnd,
                               int dpy,
                               hwc_rect_t& sourceCrop,
                               hwc_rect_t& displayFrame,
                               int& transform,
                               ovutils::eTransform& orient);

// Returns the orientation that needs to be set on external for
// BufferMirrirMode(Sidesync)
int getMirrorModeOrientation(hwc_context_t *ctx);

//Close acquireFenceFds of all layers of incoming list
void closeAcquireFds(hwc_display_contents_1_t* list);

//Sync point impl.
int hwc_sync(hwc_context_t *ctx, hwc_display_contents_1_t* list, int dpy,
        int fd);

//Trims a layer's source crop which is outside of screen boundary.
void trimLayer(hwc_context_t *ctx, const int& dpy, const int& transform,
        hwc_rect_t& crop, hwc_rect_t& dst);

//Sets appropriate mdp flags for a layer.
void setMdpFlags(hwc_layer_1_t *layer,
        ovutils::eMdpFlags &mdpFlags,
        int rotDownscale, int transform);

int configRotator(overlay::Rotator *rot, ovutils::Whf& whf,
        const ovutils::Whf& origWhf, const ovutils::eMdpFlags& mdpFlags,
        const ovutils::eTransform& orient, const int& downscale);

int configMdp(overlay::Overlay *ov, const ovutils::PipeArgs& parg,
        const ovutils::eTransform& orient, const hwc_rect_t& crop,
        const hwc_rect_t& pos, const MetaData_t *metadata,
        const ovutils::eDest& dest);

void updateSource(ovutils::eTransform& orient, ovutils::Whf& whf,
        hwc_rect_t& crop);

bool needToForceRotator(hwc_context_t *ctx, const int& dpy,
         uint32_t w, uint32_t h, int transform);

//Routine to configure low resolution panels (<= 2048 width)
int configureLowRes(hwc_context_t *ctx, hwc_layer_1_t *layer, const int& dpy,
        ovutils::eMdpFlags& mdpFlags, ovutils::eZorder& z,
        ovutils::eIsFg& isFg, const ovutils::eDest& dest,
        overlay::Rotator **rot);

//Routine to configure high resolution panels (> 2048 width)
int configureHighRes(hwc_context_t *ctx, hwc_layer_1_t *layer, const int& dpy,
        ovutils::eMdpFlags& mdpFlags, ovutils::eZorder& z,
        ovutils::eIsFg& isFg, const ovutils::eDest& lDest,
        const ovutils::eDest& rDest, overlay::Rotator **rot);

// Inline utility functions
static inline bool isSkipLayer(const hwc_layer_1_t* l) {
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

// Returns true if the buffer is non contiguous
static inline bool isNonContigBuffer(const private_handle_t* hnd) {
    return (hnd && (private_handle_t::PRIV_FLAGS_NONCONTIGUOUS_MEM & hnd->flags));
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

static inline int getWidth(const private_handle_t* hnd) {
    if(isYuvBuffer(hnd)) {
        MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
        if(metadata && metadata->operation & UPDATE_BUFFER_GEOMETRY) {
            return metadata->bufferDim.sliceWidth;
        }
    }
    return hnd->width;
}

static inline int getHeight(const private_handle_t* hnd) {
    if(isYuvBuffer(hnd)) {
        MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
        if(metadata && metadata->operation & UPDATE_BUFFER_GEOMETRY) {
            return metadata->bufferDim.sliceHeight;
        }
    }
    return hnd->height;
}

//Return true if the buffer is intended for Secure Display
static inline bool isSecureDisplayBuffer(const private_handle_t* hnd) {
    return (hnd && (hnd->flags & private_handle_t::PRIV_FLAGS_SECURE_DISPLAY));
}

template<typename T> inline T max(T a, T b) { return (a > b) ? a : b; }
template<typename T> inline T min(T a, T b) { return (a < b) ? a : b; }

// Initialize uevent thread
void init_uevent_thread(hwc_context_t* ctx);
// Initialize vsync thread
void init_vsync_thread(hwc_context_t* ctx);

inline void getLayerResolution(const hwc_layer_1_t* layer,
                               int& width, int& height)
{
    hwc_rect_t displayFrame  = layer->displayFrame;
    width = displayFrame.right - displayFrame.left;
    height = displayFrame.bottom - displayFrame.top;
}

static inline int openFb(int dpy) {
    int fd = -1;
    const char *devtmpl = "/dev/graphics/fb%u";
    char name[64] = {0};
    snprintf(name, 64, devtmpl, dpy);
    fd = open(name, O_RDWR);
    return fd;
}

template <class T>
inline void swap(T& a, T& b) {
    T tmp = a;
    a = b;
    b = tmp;
}
int getSocIdFromSystem();
}; //qhwc namespace

enum eAnimationState{
    ANIMATION_STOPPED,
    ANIMATION_STARTED,
};

// -----------------------------------------------------------------------------
// HWC context
// This structure contains overall state
struct hwc_context_t {
    hwc_composer_device_1_t device;
    const hwc_procs_t* proc;

    //CopyBit objects
    qhwc::CopyBit *mCopyBit[HWC_NUM_DISPLAY_TYPES];

    //Overlay object - NULL for non overlay devices
    overlay::Overlay *mOverlay;
    //Holds a few rot objects
    overlay::RotMgr *mRotMgr;

    //Primary and external FB updater
    qhwc::IFBUpdate *mFBUpdate[HWC_NUM_DISPLAY_TYPES];
    // External display related information
    qhwc::ExternalDisplay *mExtDisplay;
    qhwc::VirtualDisplay *mVirtualDisplay;
    qhwc::MDPInfo mMDP;
    qhwc::VsyncState vstate;
    qhwc::DisplayAttributes dpyAttr[HWC_NUM_DISPLAY_TYPES];
    qhwc::ListStats listStats[HWC_NUM_DISPLAY_TYPES];
    qhwc::LayerProp *layerProp[HWC_NUM_DISPLAY_TYPES];
    qhwc::MDPComp *mMDPComp[HWC_NUM_DISPLAY_TYPES];
    qhwc::HwcDebug *mHwcDebug[HWC_NUM_DISPLAY_TYPES];
    hwc_rect_t mViewFrame[HWC_NUM_DISPLAY_TYPES];
    // stores the #numHwLayers of the previous frame
    // for each display device
    int mPrevHwLayerCount[HWC_NUM_DISPLAY_TYPES];
    eAnimationState mAnimationState[HWC_NUM_DISPLAY_TYPES];
    // stores the primary device orientation
    int deviceOrientation;
    //Securing in progress indicator
    bool mSecuring;
    //WFD on proprietary stack
    bool mVirtualonExtActive;
    //Display in secure mode indicator
    bool mSecureMode;
    //DMA used for rotator
    bool mDMAInUse;
    //MDP rotater needed
    bool mNeedsRotator;
    //Check if base pipe is set up
    bool mBasePipeSetup;
    //Lock to protect drawing data structures
    mutable Locker mDrawLock;
    //Drawing round when we use GPU
    bool isPaddingRound;
    // External Orientation
    int mExtOrientation;
    //Used for SideSync feature
    //which overrides the mExtOrientation
    bool mBufferMirrorMode;
    //used for enabling C2D Feature only for 8960 Non Pro Device
    int mSocId;
    qhwc::LayerRotMap *mLayerRotMap[HWC_NUM_DISPLAY_TYPES];
    //previous Width & Height
    overlay::utils::Whf mPrevWHF[HWC_NUM_DISPLAY_TYPES];
    // Panel reset flag will be set if BTA check fails
    bool mPanelResetStatus;
};

namespace qhwc {
static inline bool isSkipPresent (hwc_context_t *ctx, int dpy) {
    return  ctx->listStats[dpy].skipCount;
}

static inline bool isYuvPresent (hwc_context_t *ctx, int dpy) {
    return  ctx->listStats[dpy].yuvCount;
}

static inline bool has90Transform(hwc_layer_1_t *layer) {
    return (layer->transform & HWC_TRANSFORM_ROT_90);
}

};

#endif //HWC_UTILS_H
