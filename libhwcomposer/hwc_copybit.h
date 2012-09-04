/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
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
#ifndef HWC_COPYBIT_H
#define HWC_COPYBIT_H
#include "hwc_utils.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gralloc_priv.h>
#include <gr.h>
#include <dlfcn.h>

#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

namespace qhwc {

class CopyBit {
public:
    //Sets up members and prepares copybit if conditions are met
    static bool prepare(hwc_context_t *ctx, hwc_layer_list_t *list);
    //Draws layer if the layer is set for copybit in prepare
    static bool draw(hwc_context_t *ctx, hwc_layer_list_t *list, EGLDisplay dpy,
                                                                EGLSurface sur);
    //Receives data from hwc
    static void setStats(int skipCount);

    static void updateEglHandles(void*);
    static int  drawLayerUsingCopybit(hwc_context_t *dev, hwc_layer_t *layer,
                                          EGLDisplay dpy, EGLSurface surface,
                                       android_native_buffer_t *renderBuffer);
    static bool canUseCopybitForYUV (hwc_context_t *ctx);
    static bool canUseCopybitForRGB (hwc_context_t *ctx,
                                     hwc_layer_list_t *list);
    static bool validateParams (hwc_context_t *ctx,
                                const hwc_layer_list_t *list);
#ifdef QCOM_BSP
    static bool canUseContiguousMemory(const hwc_layer_list_t* list);
#endif
    static void closeEglLib();
    static void openEglLibAndGethandle();
private:
    //Marks layer flags if this feature is used
    static void markFlags(hwc_layer_t *layer);
    //Flags on animation
    static bool sIsSkipLayerPresent;
    //Flags if this feature is on.
    static bool sIsModeOn;
    // flag that indicates whether CopyBit is enabled or not
    static bool sCopyBitDraw;

    static  unsigned int getRGBRenderingArea (const hwc_layer_list_t *list);

    static void getLayerResolution(const hwc_layer_t* layer,
                                   unsigned int &width, unsigned int& height);
};

class CopybitEngine {
public:
    ~CopybitEngine();
    // API to get copybit engine(non static)
    struct copybit_device_t *getEngine();
    // API to get singleton
    static CopybitEngine* getInstance();
private:
    CopybitEngine();
    struct copybit_device_t *sEngine;
    static CopybitEngine* sInstance; // singleton
};

inline void CopyBit::setStats(int skipCount) {
    sIsSkipLayerPresent = (skipCount != 0);
}
}; //namespace qhwc

#endif //HWC_COPYBIT_H
