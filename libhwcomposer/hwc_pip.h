/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
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
#ifndef HWC_PIP_H
#define HWC_PIP_H
#include "hwc_utils.h"

#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

namespace qhwc {
//Feature for using overlay to display videos.
class VideoPIP {
public:
    //Sets up members and prepares overlay if conditions are met
    static bool prepare(hwc_context_t *ctx, hwc_layer_list_t *list);
    //Draws layer if this feature is on
    static bool draw(hwc_context_t *ctx, hwc_layer_list_t *list);
    //Receives data from hwc
    static void setStats(int yuvCount, int yuvLayerIndex, bool isYuvLayerSkip,
            int pipLayerIndex);
    //resets values
    static void reset();
private:
    //Choose an appropriate overlay state based on conditions
    static void chooseState(hwc_context_t *ctx);
    //Configures overlay for video prim and ext
    static bool configure(hwc_context_t *ctx, hwc_layer_t *yuvlayer,
            hwc_layer_t *pipLayer);
    //Marks layer flags if this feature is used
    static void markFlags(hwc_layer_t *layer);
    //returns yuv count
    static int getYuvCount();

    //The chosen overlay state.
    static ovutils::eOverlayState sState;
    //Number of yuv layers in this drawing round
    static int sYuvCount;
    //Index of YUV layer, relevant only if count is 1
    static int sYuvLayerIndex;
    //Flags if a yuv layer is animating or below something that is animating
    static bool sIsYuvLayerSkip;
    //Holds the PIP layer index in case of two videos, -1 by default
    static int sPIPLayerIndex;
    //Flags if this feature is on.
    static bool sIsModeOn;
};

inline void VideoPIP::setStats(int yuvCount, int yuvLayerIndex,
        bool isYuvLayerSkip, int pipLayerIndex) {
    sYuvCount = yuvCount;
    sYuvLayerIndex = yuvLayerIndex;
    sIsYuvLayerSkip = isYuvLayerSkip;
    sPIPLayerIndex = pipLayerIndex;
}

inline int VideoPIP::getYuvCount() { return sYuvCount; }
inline void VideoPIP::reset() {
    sYuvCount = 0;
    sYuvLayerIndex = -1;
    sIsYuvLayerSkip = false;
    sPIPLayerIndex = -1;
    sIsModeOn = false;
    sState = ovutils::OV_CLOSED;
}
}; //namespace qhwc

#endif //HWC_PIP_H
