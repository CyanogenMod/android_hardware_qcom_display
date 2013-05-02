/*
 * Copyright (C) 2012-2013, The Linux Foundation. All rights reserved.
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

#ifndef HWC_MDP_COMP
#define HWC_MDP_COMP

#include <hwc_utils.h>
#include <idle_invalidator.h>
#include <cutils/properties.h>
#include <overlay.h>

#define DEFAULT_IDLE_TIME 2000
#define MAX_PIPES_PER_MIXER 4

namespace overlay {
    class Rotator;
};

namespace qhwc {
namespace ovutils = overlay::utils;

class MDPComp {
public:
    virtual ~MDPComp(){};
    /*sets up mdp comp for the current frame */
    bool prepare(hwc_context_t *ctx, hwc_display_contents_1_t* list);
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
        MDPCOMP_OV_DMA = ovutils::OV_MDP_PIPE_DMA,
        MDPCOMP_OV_ANY,
    };
    struct MdpPipeInfo {
        int zOrder;
        virtual ~MdpPipeInfo(){};
    };
    struct PipeLayerPair {
        MdpPipeInfo *pipeInfo;
        native_handle_t* handle;
        overlay::Rotator* rot;
    };

    /* introduced for mixed mode implementation */
    struct FrameInfo {
        int count;
        struct PipeLayerPair* pipeLayer;
    };

    /* calculates pipes needed for the panel */
    virtual int pipesNeeded(hwc_context_t *ctx,
                            hwc_display_contents_1_t* list) = 0;
    /* allocates pipe from pipe book */
    virtual bool allocLayerPipes(hwc_context_t *ctx,
                hwc_display_contents_1_t* list,FrameInfo& current_frame) = 0;
    /* configures MPD pipes */
    virtual int configure(hwc_context_t *ctx, hwc_layer_1_t *layer,
                PipeLayerPair& pipeLayerPair) = 0;


    /* set/reset flags for MDPComp */
    void setMDPCompLayerFlags(hwc_context_t *ctx,
                                       hwc_display_contents_1_t* list);
    void unsetMDPCompLayerFlags(hwc_context_t* ctx,
                                       hwc_display_contents_1_t* list);
    /* get/set states */
    eState getState() { return mState; };
    /* reset state */
    void reset( hwc_context_t *ctx, hwc_display_contents_1_t* list );
    /* allocate MDP pipes from overlay */
    ovutils::eDest getMdpPipe(hwc_context_t *ctx, ePipeType type);
    /* checks for conditions where mdpcomp is not possible */
    bool isDoable(hwc_context_t *ctx, hwc_display_contents_1_t* list);
    /* sets up MDP comp for current frame */
    bool setup(hwc_context_t* ctx, hwc_display_contents_1_t* list);
    /* Is debug enabled */
    static bool isDebug() { return sDebugLogs ? true : false; };
    /* Is feature enabled */
    static bool isEnabled() { return sEnabled; };
    /* checks for mdp comp width limitation */
    bool isValidDimension(hwc_context_t *ctx, hwc_layer_1_t *layer);

    eState mState;

    static bool sEnabled;
    static bool sDebugLogs;
    static bool sIdleFallBack;
    static IdleInvalidator *idleInvalidator;
    struct FrameInfo mCurrentFrame;
};

class MDPCompLowRes : public MDPComp {
public:
     virtual ~MDPCompLowRes(){};
     virtual bool draw(hwc_context_t *ctx, hwc_display_contents_1_t *list);

private:
    struct MdpPipeInfoLowRes : public MdpPipeInfo {
        ovutils::eDest index;
        virtual ~MdpPipeInfoLowRes() {};
    };

    /* configure's overlay pipes for the frame */
    virtual int configure(hwc_context_t *ctx, hwc_layer_1_t *layer,
            PipeLayerPair& pipeLayerPair);

    /* allocates pipes to selected candidates */
    virtual bool allocLayerPipes(hwc_context_t *ctx,
            hwc_display_contents_1_t* list,
            FrameInfo& current_frame);

    virtual int pipesNeeded(hwc_context_t *ctx,
            hwc_display_contents_1_t* list);
};

class MDPCompHighRes : public MDPComp {
public:
    virtual ~MDPCompHighRes(){};
    virtual bool draw(hwc_context_t *ctx, hwc_display_contents_1_t *list);
private:
    struct MdpPipeInfoHighRes : public MdpPipeInfo {
        ovutils::eDest lIndex;
        ovutils::eDest rIndex;
        virtual ~MdpPipeInfoHighRes() {};
    };

    bool acquireMDPPipes(hwc_context_t *ctx, hwc_layer_1_t* layer,
                        MdpPipeInfoHighRes& pipe_info, ePipeType type);

    /* configure's overlay pipes for the frame */
    virtual int configure(hwc_context_t *ctx, hwc_layer_1_t *layer,
            PipeLayerPair& pipeLayerPair);

    /* allocates pipes to selected candidates */
    virtual bool allocLayerPipes(hwc_context_t *ctx,
            hwc_display_contents_1_t* list,
            FrameInfo& current_frame);

    virtual int pipesNeeded(hwc_context_t *ctx, hwc_display_contents_1_t* list);
};
}; //namespace
#endif
