/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
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
#ifndef HWC_VIDEO_H
#define HWC_VIDEO_H

#include "hwc_utils.h"
#include "overlayUtils.h"

#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

namespace overlay {
    class Rotator;
}

namespace qhwc {
namespace ovutils = overlay::utils;

class IVideoOverlay {
public:
    explicit IVideoOverlay(const int& dpy) : mDpy(dpy), mModeOn(false),
            mRot(NULL) {}
    virtual ~IVideoOverlay() {};
    virtual bool prepare(hwc_context_t *ctx,
            hwc_display_contents_1_t *list) = 0;
    virtual bool draw(hwc_context_t *ctx,
            hwc_display_contents_1_t *list) = 0;
    virtual void reset() = 0;
    virtual bool isModeOn() = 0;
    //Factory method that returns a low-res or high-res version
    static IVideoOverlay *getObject(const int& width, const int& dpy);

protected:
    const int mDpy; // display to update
    bool mModeOn; // if prepare happened
    overlay::Rotator *mRot;
};

class VideoOverlayLowRes : public IVideoOverlay {
public:
    explicit VideoOverlayLowRes(const int& dpy);
    virtual ~VideoOverlayLowRes() {};
    bool prepare(hwc_context_t *ctx, hwc_display_contents_1_t *list);
    bool draw(hwc_context_t *ctx, hwc_display_contents_1_t *list);
    void reset();
    bool isModeOn();
private:
    //Configures overlay for video prim and ext
    bool configure(hwc_context_t *ctx, hwc_layer_1_t *yuvlayer);
    //Marks layer flags if this feature is used
    void markFlags(hwc_layer_1_t *yuvLayer);
    //Flags if this feature is on.
    bool mModeOn;
    ovutils::eDest mDest;
};

class VideoOverlayHighRes : public IVideoOverlay {
public:
    explicit VideoOverlayHighRes(const int& dpy);
    virtual ~VideoOverlayHighRes() {};
    bool prepare(hwc_context_t *ctx, hwc_display_contents_1_t *list);
    bool draw(hwc_context_t *ctx, hwc_display_contents_1_t *list);
    void reset();
    bool isModeOn();
private:
    //Configures overlay for video prim and ext
    bool configure(hwc_context_t *ctx, hwc_layer_1_t *yuvlayer);
    //Marks layer flags if this feature is used
    void markFlags(hwc_layer_1_t *yuvLayer);
    //Flags if this feature is on.
    bool mModeOn;
    ovutils::eDest mDestL;
    ovutils::eDest mDestR;
};

//=================Inlines======================
inline void VideoOverlayLowRes::reset() {
    mModeOn = false;
    mDest = ovutils::OV_INVALID;
    mRot = NULL;
}

inline void VideoOverlayHighRes::reset() {
    mModeOn = false;
    mDestL = ovutils::OV_INVALID;
    mDestR = ovutils::OV_INVALID;
    mRot = NULL;
}

}; //namespace qhwc

#endif //HWC_VIDEO_H
