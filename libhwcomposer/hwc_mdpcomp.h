/*
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
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
public:
    virtual ~MDPComp() {}
    /*sets up mdp comp for the current frame */
    virtual bool prepare(hwc_context_t *ctx,
            hwc_display_contents_1_t* list) = 0;
    /* draw */
    virtual bool draw(hwc_context_t *ctx, hwc_display_contents_1_t *list) = 0;
    void dump(android::String8& buf);
    bool isUsed() { return (mState == MDPCOMP_ON); };

    static MDPComp* getObject(const int& width);
    /* Handler to invoke frame redraw on Idle Timer expiry */
    static void timeout_handler(void *udata);
    static bool init(hwc_context_t *ctx);

protected:
    enum eState {
        MDPCOMP_ON = 0,
        MDPCOMP_OFF,
    };

    enum ePipeType {
        MDPCOMP_OV_RGB = ovutils::OV_MDP_PIPE_RGB,
        MDPCOMP_OV_VG = ovutils::OV_MDP_PIPE_VG,
        MDPCOMP_OV_ANY,
    };

    /* set/reset flags for MDPComp */
    void setMDPCompLayerFlags(hwc_context_t *ctx,
                                       hwc_display_contents_1_t* list);
    void unsetMDPCompLayerFlags(hwc_context_t* ctx,
                                       hwc_display_contents_1_t* list);
    void printInfo(hwc_layer_1_t* layer);
    /* get/set states */
    eState getState() { return mState; };

    /* set up Border fill as Base pipe */
    static bool setupBasePipe(hwc_context_t*);
    /* Is debug enabled */
    static bool isDebug() { return sDebugLogs ? true : false; };
    /* Is feature enabled */
    static bool isEnabled() { return sEnabled; };

    eState mState;

    static bool sEnabled;
    static bool sDebugLogs;
    static bool sIdleFallBack;
    static IdleInvalidator *idleInvalidator;

};

class MDPCompLowRes : public MDPComp {
public:
    virtual ~MDPCompLowRes() {}
    virtual bool prepare(hwc_context_t *ctx,
            hwc_display_contents_1_t* list);
    virtual bool draw(hwc_context_t *ctx, hwc_display_contents_1_t *list);

private:
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
    /* configure's overlay pipes for the frame */
    int configure(hwc_context_t *ctx, hwc_layer_1_t *layer,
                        MdpPipeInfo& mdp_info);
    /* checks for conditions where mdpcomp is not possible */
    bool isDoable(hwc_context_t *ctx, hwc_display_contents_1_t* list);
    bool setup(hwc_context_t* ctx, hwc_display_contents_1_t* list);
    /* allocates pipes to selected candidates */
    bool allocLayerPipes(hwc_context_t *ctx,
            hwc_display_contents_1_t* list,
            FrameInfo& current_frame);
    /* reset state */
    void reset( hwc_context_t *ctx, hwc_display_contents_1_t* list );
    /* configure MDP flags for video buffers */
    void setVidInfo(hwc_layer_1_t *layer, ovutils::eMdpFlags &mdpFlags);
    /* allocate MDP pipes from overlay */
    int getMdpPipe(hwc_context_t *ctx, ePipeType type);

    struct FrameInfo mCurrentFrame;
};

class MDPCompHighRes : public MDPComp {
public:
    virtual ~MDPCompHighRes() {}
    virtual bool prepare(hwc_context_t *ctx,
            hwc_display_contents_1_t* list) { return false; }
    virtual bool draw(hwc_context_t *ctx, hwc_display_contents_1_t *list) {
        return true;
    }
};

}; //namespace
#endif
