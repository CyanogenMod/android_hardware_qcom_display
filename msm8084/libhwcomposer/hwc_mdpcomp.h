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

#define MAX_STATIC_PIPES 3
#define MDPCOMP_INDEX_OFFSET 4
#define DEFAULT_IDLE_TIME 2000

#define MAX_VG 2
#define MAX_RGB 2
#define VAR_INDEX 3
#define MAX_PIPES (MAX_VG + MAX_RGB)
#define HWC_MDPCOMP_INDEX_MASK 0x00000030


//struct hwc_context_t;

namespace qhwc {

// pipe status
enum {
    PIPE_UNASSIGNED = 0,
    PIPE_IN_FB_MODE,
    PIPE_IN_COMP_MODE,
};

// pipe request
enum {
    PIPE_NONE = 0,
    PIPE_REQ_VG,
    PIPE_REQ_RGB,
    PIPE_REQ_FB,
};

// MDP Comp Status
enum {
    MDPCOMP_SUCCESS = 0,
    MDPCOMP_FAILURE,
    MDPCOMP_ABORT,
};

//This class manages the status of 4 MDP pipes and keeps
//track of Variable pipe mode.
class PipeMgr {

public:
    PipeMgr() { reset();}
    //reset pipemgr params
    void reset();

    //Based on the preference received, pipe mgr
    //allocates the best available pipe to handle
    //the case
    int req_for_pipe(int pipe_req);

    //Allocate requested pipe and update availablity
    int assign_pipe(int pipe_pref);

    // Get/Set pipe status
    void setStatus(int pipe_index, int pipe_status) {
        mStatus[pipe_index] = pipe_status;
    }
    int getStatus(int pipe_index) {
        return mStatus[pipe_index];
    }
private:
    int mVGPipes;
    int mVGUsed;
    int mVGIndex;
    int mRGBPipes;
    int mRGBUsed;
    int mRGBIndex;
    int mTotalAvail;
    int mStatus[MAX_PIPES];
};


class MDPComp {
    enum State {
        MDPCOMP_ON = 0,
        MDPCOMP_OFF,
        MDPCOMP_OFF_PENDING,
    };

    enum {
        MDPCOMP_LAYER_BLEND = 1,
        MDPCOMP_LAYER_DOWNSCALE = 2,
        MDPCOMP_LAYER_SKIP = 4,
        MDPCOMP_LAYER_UNSUPPORTED_MEM = 8,
    };

    struct mdp_pipe_info {
        int index;
        int z_order;
        bool isVG;
        bool isFG;
        bool vsync_wait;
    };

    struct pipe_layer_pair {
        int layer_index;
        mdp_pipe_info pipe_index;
        native_handle_t* handle;
    };

    struct frame_info {
        int count;
        struct pipe_layer_pair* pipe_layer;

    };

    struct layer_mdp_info {
        bool can_use_mdp;
        int pipe_pref;
    };

    static State sMDPCompState;
    static IdleInvalidator *idleInvalidator;
    static struct frame_info sCurrentFrame;
    static PipeMgr sPipeMgr;
    static int sSkipCount;
    static int sMaxLayers;
    static bool sDebugLogs;
    static bool sIdleFallBack;

public:
    /* Handler to invoke frame redraw on Idle Timer expiry */
    static void timeout_handler(void *udata);

    /* configure/tear-down MDPComp params*/
    static bool init(hwc_context_t *ctx);
    static bool deinit();

    /*sets up mdp comp for the current frame */
    static bool configure(hwc_context_t *ctx,  hwc_display_contents_1_t* list);

    /* draw */
    static int draw(hwc_context_t *ctx, hwc_display_contents_1_t *list);

    /* store frame stats */
    static void setStats(int skipCt) { sSkipCount  = skipCt;};

private:

    /* get/set pipe index associated with overlay layers */
    static void setLayerIndex(hwc_layer_1_t* layer, const int pipe_index);
    static int  getLayerIndex(hwc_layer_1_t* layer);

    /* set/reset flags for MDPComp */
    static void setMDPCompLayerFlags(hwc_display_contents_1_t* list);
    static void unsetMDPCompLayerFlags(hwc_context_t* ctx,
                                       hwc_display_contents_1_t* list);

    static void print_info(hwc_layer_1_t* layer);

    /* configure's overlay pipes for the frame */
    static int  prepare(hwc_context_t *ctx, hwc_layer_1_t *layer,
                        mdp_pipe_info& mdp_info);

    /* checks for conditions where mdpcomp is not possible */
    static bool is_doable(hwc_context_t *ctx, hwc_display_contents_1_t* list);

    static bool setup(hwc_context_t* ctx, hwc_display_contents_1_t* list);

    /* parses layer for properties affecting mdp comp */
    static void get_layer_info(hwc_layer_1_t* layer, int& flags);

    /* iterates through layer list to choose candidate to use overlay */
    static int  mark_layers(hwc_context_t *ctx, hwc_display_contents_1_t* list,
            layer_mdp_info* layer_info, frame_info& current_frame);

    static bool parse_and_allocate(hwc_context_t* ctx, hwc_display_contents_1_t* list,
                                                  frame_info& current_frame );

    /* clears layer info struct */
    static void reset_layer_mdp_info(layer_mdp_info* layer_mdp_info,int count);

    /* allocates pipes to selected candidates */
    static bool alloc_layer_pipes(hwc_context_t *ctx,
            hwc_display_contents_1_t* list,
            layer_mdp_info* layer_info,
            frame_info& current_frame);
    /* updates variable pipe mode for the current frame */
    static int  configure_var_pipe(hwc_context_t* ctx);

    /* get/set states */
    static State get_state() { return sMDPCompState; };
    static void set_state(State state) { sMDPCompState = state; };

    /* reset state */
    static void reset( hwc_context_t *ctx, hwc_display_contents_1_t* list );

    /* Is feature enabled */
    static bool isEnabled() { return sMaxLayers ? true : false; };
    /* Is debug enabled */
    static bool isDebug() { return sDebugLogs ? true : false; };
};
}; //namespace
#endif
