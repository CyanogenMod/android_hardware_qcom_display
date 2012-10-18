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
#ifndef HWC_VIDEO_H
#define HWC_VIDEO_H
#include "hwc_utils.h"

#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

namespace qhwc {
//Feature for using overlay to display videos.
class VideoOverlay {
public:
    //Sets up members and prepares overlay if conditions are met
    static bool prepare(hwc_context_t *ctx, hwc_display_contents_1_t *list,
            int dpy);
    //Draws layer if this feature is on
    static bool draw(hwc_context_t *ctx, hwc_display_contents_1_t *list,
            int dpy);
    //resets values
    static void reset();
private:
    //Choose an appropriate overlay state based on conditions
    static void chooseState(hwc_context_t *ctx, int dpy,
        hwc_layer_1_t *yuvLayer);
    //Configures overlay for video prim and ext
    static bool configure(hwc_context_t *ctx, int dpy,
            hwc_layer_1_t *yuvlayer);
    //Marks layer flags if this feature is used
    static void markFlags(hwc_layer_1_t *yuvLayer);
    //The chosen overlay state.
    static ovutils::eOverlayState sState[HWC_NUM_DISPLAY_TYPES];
    //Flags if this feature is on.
    static bool sIsModeOn[HWC_NUM_DISPLAY_TYPES];
};

inline void VideoOverlay::reset() {
    for(uint32_t i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        sIsModeOn[i] = false;
        sState[i] = ovutils::OV_CLOSED;
    }
}
}; //namespace qhwc

#endif //HWC_VIDEO_H
