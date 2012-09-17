/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are
 * retained for attribution purposes only.
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
#ifndef HWC_UIMIRROR_H
#define HWC_UIMIRROR_H
#include "hwc_utils.h"
#include "overlay.h"

#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

namespace qhwc {
//Feature for Mirroring UI on the External display
class UIMirrorOverlay {
    public:
        // Sets up members and prepares overlay if conditions are met
        static bool prepare(hwc_context_t *ctx, hwc_layer_1_t *fblayer);
        // Draws layer if this feature is on
        static bool draw(hwc_context_t *ctx, hwc_layer_1_t *fblayer);
        //Reset values
        static void reset();
    private:
        //Configures overlay
        static bool configure(hwc_context_t *ctx, hwc_layer_1_t *fblayer);
        //The chosen overlay state.
        static ovutils::eOverlayState sState;
        //Flags if this feature is on.
        static bool sIsUiMirroringOn;
};

}; //namespace qhwc

#endif //HWC_UIMIRROR_H
