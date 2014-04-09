/*
* Copyright (c) 2013-2014 The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef HWC_VPU_H
#define HWC_VPU_H

#include <sys/types.h>
#include "hwc_utils.h"

#define MAX_PIPES_PER_LAYER 2

//Forward declarations
struct hwc_display_contents_1;
typedef struct hwc_display_contents_1 hwc_display_contents_1_t;
struct hwc_layer_1;
typedef struct hwc_layer_1 hwc_layer_1_t;
struct hwc_context_t;

namespace vpu {
class VPU;
struct LayerList;
};
namespace android {
class Parcel;
};

namespace qhwc {

class VPUClient {
public:
    VPUClient(hwc_context_t *ctx);

    ~VPUClient();

    int setupVpuSession(hwc_context_t *ctx, int display,
                            hwc_display_contents_1_t* list);
    int prepare(hwc_context_t *ctx, int display,
                            hwc_display_contents_1_t* list);
    int predraw(hwc_context_t *ctx, int display,
                            hwc_display_contents_1_t* list);
    int draw(hwc_context_t *ctx, int display,
                            hwc_display_contents_1_t* list);
    int processCommand(uint32_t command,
        const android::Parcel* inParcel, android::Parcel* outParcel);
    int getLayerFormat(int dpy, hwc_layer_1_t *layer);
    int getWidth(int dpy, hwc_layer_1_t *layer);
    int getHeight(int dpy, hwc_layer_1_t *layer);
    bool supportedVPULayer(int dpy, hwc_layer_1_t *layer);

private:
    vpu::VPU *mVPU;
    void* mVPULib;

    /* VpuLayerProp struct:
     *  This struct corresponds to only one layer
     *  pipeCount: number of pipes required for a layer
     *  pipeID[]: pipe ids corresponding to the layer
     */
    struct VpuLayerProp {
        int format;
        int width;
        int height;
        int pipeCount;
        bool vpuLayer;
        bool firstBuffer;
        hwc_frect_t sourceCropf;
        hwc_layer_1_t *layer;
        int pipeID[MAX_PIPES_PER_LAYER];
        int dest[MAX_PIPES_PER_LAYER];
    };
    int mNumVpuLayers;  /* total num of vpu supported layers */
    int mGpuFallback;   /* all layers are not supported by vpu */

    VpuLayerProp mProp[HWC_NUM_DISPLAY_TYPES][MAX_NUM_APP_LAYERS];
    int mDebugLogs;
    private_handle_t *mHnd[HWC_NUM_DISPLAY_TYPES][MAX_NUM_APP_LAYERS];
    vpu::LayerList *vList[HWC_NUM_DISPLAY_TYPES];

    /* Private debug functions */
    int32_t isDebug() { return (mDebugLogs >= 1); }
    int32_t isDebug2() { return (mDebugLogs >= 2); }

    /* Private Get/Set functions */
    int getLayerIdx(int dpy, hwc_layer_1_t *layer);
    void getPipeId(VpuLayerProp* prop, int &pipe);
    void getPipeId(VpuLayerProp* prop, int &lPipe, int &rPipe);
    int getDest(VpuLayerProp* prop, int pipenum);
    void setPipeCount(VpuLayerProp* prop, int count);
    void setPipeId(VpuLayerProp* prop, int lPipeId, int rPipeId);
    void setPipeId(VpuLayerProp* prop, int pipeId);
    void setDest(VpuLayerProp* prop, int lDest, int rDest);
    void setDest(VpuLayerProp* prop, int dest);

    /* Private implementations */
    bool supportedVPULayer(VpuLayerProp* prop);
    bool allocResLayerPipes(hwc_context_t* ctx, int dpy,
                            hwc_display_contents_1_t* list);
    bool allocLayerPipes(hwc_context_t* ctx, int dpy,
                            hwc_display_contents_1_t* list);
    bool allocResLayerPipesSplit(hwc_context_t* ctx, int dpy,
                            hwc_display_contents_1_t* list);
    bool allocLayerPipesSplit(hwc_context_t* ctx, int dpy,
                            hwc_display_contents_1_t* list);
    bool configureLayers(hwc_context_t* ctx, int dpy,
                            hwc_display_contents_1_t* list);
    bool configureLayersSplit(hwc_context_t* ctx, int dpy,
                            hwc_display_contents_1_t* list);
    void setMDPCompLayerFlags(hwc_context_t *ctx, int dpy,
                            hwc_display_contents_1_t* list);
    bool drawDummyLayers(hwc_context_t* ctx, int dpy,
                            hwc_display_contents_1_t* list);
    bool queueHandle(hwc_context_t* ctx, VpuLayerProp* prop,
                            private_handle_t* hnd);
    bool queueHandleSplit(hwc_context_t* ctx, VpuLayerProp* prop,
                            private_handle_t* hnd);
}; // class VPU
}; // namespace qhwc
#endif /* end of include guard: HWC_VPU_H */
