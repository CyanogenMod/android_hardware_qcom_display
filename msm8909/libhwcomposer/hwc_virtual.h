/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2014, The Linux Foundation. All rights reserved.
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

#ifndef HWC_VIRTUAL
#define HWC_VIRTUAL

#include <hwc_utils.h>

namespace qhwc {

class HWCVirtualVDS {
public:
    HWCVirtualVDS(){};
    ~HWCVirtualVDS(){};
    // Chooses composition type and configures pipe for each layer in virtual
    // display list
    int prepare(hwc_composer_device_1 *dev,
                          hwc_display_contents_1_t* list);
    // Queues the buffer for each layer in virtual display list and call display
    // commit.
    int set(hwc_context_t *ctx, hwc_display_contents_1_t *list);
    // instantiates mdpcomp, copybit and fbupdate objects and initialize those
    // objects for virtual display during virtual display connect.
    void init(hwc_context_t *ctx);
    // Destroys mdpcomp, copybit and fbupdate objects and for virtual display
    // during virtual display disconnect.
    void destroy(hwc_context_t *ctx, size_t numDisplays,
                       hwc_display_contents_1_t** displays);
    int getScalingHeight() const { return mScalingHeight; };
    int getScalingWidth() const { return mScalingWidth; };
    // We can dump the frame buffer and WB
    // output buffer by dynamically enabling
    // dumping via a binder call:
    // adb shell service call display.qservice 15 i32 3 i32 1
    static bool sVDDumpEnabled;
    static void dynamicDebug(bool enable) {sVDDumpEnabled = enable;};

private:
    // These variables store the resolution that WB is being configured to
    // in the current draw cycle.
    int mScalingWidth, mScalingHeight;
    void setMDPScalingMode(hwc_context_t* ctx,
            private_handle_t* ohnd, int dpy);
};

}; //namespace
#endif
