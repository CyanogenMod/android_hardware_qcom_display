/*
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

#ifndef HWC_MDP_COMP
#define HWC_MDP_COMP

#include <hwc_utils.h>
#include <idle_invalidator.h>
#include <cutils/properties.h>
#include <overlay.h>

#define DEFAULT_IDLE_TIME 2000

namespace qhwc {
namespace ovutils = overlay::utils;

class MDPComp {
    enum eState {
        MDPCOMP_ON = 0,
        MDPCOMP_OFF,
    };

    enum ePipeType {
        MDPCOMP_OV_RGB = ovutils::OV_MDP_PIPE_RGB,
        MDPCOMP_OV_VG = ovutils::OV_MDP_PIPE_VG,
        MDPCOMP_OV_ANY,
    };

    struct MdpPipeInfo {
        int index;
        int zOrder;
    };

    struct PipeLayerPair {
        MdpPipeInfo pipeIndex;
        native_handle_t* handle;
    };

    struct FrameInfo {
        int count;
        struct PipeLayerPair* pipeLayer;

    };

    static eState sMDPCompState;
    static IdleInvalidator *idleInvalidator;
    static struct FrameInfo sCurrentFrame;
    static bool sEnabled;
    static bool sDebugLogs;
    static bool sIdleFallBack;
    static int sActiveMax;
    static bool sSecuredVid;

public:
    /* Handler to invoke frame redraw on Idle Timer expiry */
    static void timeout_handler(void *udata);

    /* configure/tear-down MDPComp params*/
    static bool init(hwc_context_t *ctx);
    static bool deinit();
    static bool isUsed() { return (sMDPCompState == MDPCOMP_ON); };

    /*sets up mdp comp for the current frame */
    static bool configure(hwc_context_t *ctx,
                            hwc_display_contents_1_t* list);

    /* draw */
    static bool draw(hwc_context_t *ctx, hwc_display_contents_1_t *list);

private:
    /* set/reset flags for MDPComp */
    static void setMDPCompLayerFlags(hwc_context_t *ctx,
                                       hwc_display_contents_1_t* list);
    static void unsetMDPCompLayerFlags(hwc_context_t* ctx,
                                       hwc_display_contents_1_t* list);

    static void print_info(hwc_layer_1_t* layer);

    /* configure's overlay pipes for the frame */
    static int  prepare(hwc_context_t *ctx, hwc_layer_1_t *layer,
                        MdpPipeInfo& mdp_info);

    /* checks for conditions where mdpcomp is not possible */
    static bool isDoable(hwc_context_t *ctx, hwc_display_contents_1_t* list);

    static bool setup(hwc_context_t* ctx, hwc_display_contents_1_t* list);

    /* allocates pipes to selected candidates */
    static bool allocLayerPipes(hwc_context_t *ctx,
            hwc_display_contents_1_t* list,
            FrameInfo& current_frame);

    /* get/set states */
    static eState getState() { return sMDPCompState; };

    /* reset state */
    static void reset( hwc_context_t *ctx, hwc_display_contents_1_t* list );

    /* Is feature enabled */
    static bool isEnabled() { return sEnabled; };

    /* Is debug enabled */
    static bool isDebug() { return sDebugLogs ? true : false; };

    /* check layer state */
    static bool isSkipPresent (hwc_context_t *ctx);
    static bool isYuvPresent (hwc_context_t *ctx);

    /* configure MDP flags for video buffers */
    static void setVidInfo(hwc_layer_1_t *layer, ovutils::eMdpFlags &mdpFlags);

    /* set up Border fill as Base pipe */
    static bool setupBasePipe(hwc_context_t*);

    /* allocate MDP pipes from overlay */
    static int getMdpPipe(hwc_context_t *ctx, ePipeType type);
};
}; //namespace
#endif
