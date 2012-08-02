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
#ifndef HWC_EXTONLY_H
#define HWC_EXTONLY_H

#include <overlay.h>
#include "hwc_utils.h"

#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

namespace qhwc {
//Feature for using overlay to display external-only layers on HDTV
class ExtOnly {
public:
    //Sets up members and prepares overlay if conditions are met
    static bool prepare(hwc_context_t *ctx, hwc_display_contents_1_t *list);
    //Draws layer if this feature is on
    static bool draw(hwc_context_t *ctx, hwc_display_contents_1_t *list);
    //Receives data from hwc
    static void setStats(int extCount, int extIndex, bool isExtBlock);
    //resets values
    static void reset();
private:
    //Choose an appropriate overlay state based on conditions
    static void chooseState(hwc_context_t *ctx);
    //Configures overlay
    static bool configure(hwc_context_t *ctx, hwc_layer_1_t *layer);
    //Marks layer flags if this feature is used
    static void markFlags(hwc_layer_1_t *layer);
    //returns ext-only count
    static int getExtCount();

    //The chosen overlay state.
    static ovutils::eOverlayState sState;
    //Number of ext-only layers in this drawing round. Used for stats/debugging.
    //This does not reflect the closed caption layer count even though its
    //ext-only.
    static int sExtCount;
    //Index of ext-only layer. If there are 2 such layers with 1 marked as BLOCK
    //then this will hold the index of BLOCK layer.
    static int sExtIndex;
    //Flags if ext-only layer is BLOCK, which means only this layer (sExtIndex)
    //is displayed even if other ext-only layers are present to block their
    //content. This is used for stats / debugging only.
    static bool sIsExtBlock;
    //Flags if this feature is on.
    static bool sIsModeOn;
};

inline void ExtOnly::setStats(int extCount, int extIndex, bool isExtBlock) {
    sExtCount = extCount;
    sExtIndex = extIndex;
    sIsExtBlock = isExtBlock;
}

inline int ExtOnly::getExtCount() { return sExtCount; }
inline void ExtOnly::reset() {
    sExtCount = 0;
    sExtIndex = -1;
    sIsExtBlock = false;
    sIsModeOn = false;
    sState = ovutils::OV_CLOSED;
}

}; //namespace qhwc

#endif //HWC_EXTONLY_H
