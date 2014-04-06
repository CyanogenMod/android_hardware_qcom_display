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
 * ARE DISCLAIMED.  INNO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER INCONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING INANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <dlfcn.h>
#include "hwc_vpuclient.h"
#include <binder/Parcel.h>
#include "hwc_fbupdate.h"
#include <vpu/vpu.h>

using namespace vpu;
using namespace android;
using namespace overlay::utils;
namespace ovutils = overlay::utils;

namespace qhwc {

VPUClient::VPUClient(hwc_context_t *ctx)
{
    mVPULib = dlopen("libvpu.so", RTLD_NOW);
    VPU* (*getObject)();

    mVPU = NULL;
    if (mVPULib == NULL) {
        ALOGE("%s: Cannot open libvpu.so object", __FUNCTION__);
        return;
    }

    *(void **) &getObject =  dlsym(mVPULib, "getObject");
    if (getObject) {
        mVPU = getObject();
        ALOGI("Initializing VPU client..");

       // calling vpu init
        if (mVPU->init() == NO_ERROR) {
            // passing display attributes to libvpu
            ALOGD_IF(isDebug(), "%s: VFM init successful!", __FUNCTION__);

            DispAttr_t attr;
            attr.width = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].xres;
            attr.height = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].yres;
            attr.fp100s = (ctx->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period) ?
              1000000000/(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period/100):0;
            mVPU->setDisplayAttr((DISPLAY_ID)HWC_DISPLAY_PRIMARY, attr);

            ALOGD_IF(isDebug(),"%s: Display attr: width:%d height:%d fp100s:%d",
                    __FUNCTION__, attr.width, attr.height, attr.fp100s);

            // memsetting the pipe structure to 0
            memset(mProp, 0, sizeof(mProp));

            mDebugLogs = 0;
            // enable logs
            char property[PROPERTY_VALUE_MAX];
            if ( property_get("debug.vpuclient.logs", property, NULL) > 0 )
                mDebugLogs = atoi(property);

            // allocating memory for LayerList
            for (int i = 0; i < HWC_NUM_DISPLAY_TYPES; ++i)
                vList[i] = (LayerList*) malloc(sizeof(LayerList));
        }
        else {
            ALOGE("Error: VPU init failed!");
            mVPU = NULL;
        }
    }
}

VPUClient::~VPUClient()
{
    // freeing LayerList
    for (int i = 0; i < HWC_NUM_DISPLAY_TYPES; ++i) {
        if (vList[i])
            free(vList[i]);
    }

    void (*destroy) (VPU*);
    *(void **) &destroy = dlsym(mVPULib, "deleteObject");
    dlclose(mVPULib);
}

void setLayer(hwc_layer_1_t *layer, Layer *vLayer)
{
    // setting handle info in vLayer
    vLayer->handle = (private_handle_t *)(layer->handle);

    if (vLayer->handle) {
        vLayer->srcStride.width = getWidth(vLayer->handle);
        vLayer->srcStride.height = getHeight(vLayer->handle);
    }

    // setting source crop
    hwc_rect_t sourceRect = integerizeSourceCrop(layer->sourceCropf);
    vLayer->srcRect.left = sourceRect.left;
    vLayer->srcRect.top  = sourceRect.top;
    vLayer->srcRect.right = sourceRect.right;
    vLayer->srcRect.bottom = sourceRect.bottom;

    // setting destination crop
    vLayer->tgtRect.left = layer->displayFrame.left;
    vLayer->tgtRect.top = layer->displayFrame.top;
    vLayer->tgtRect.right = layer->displayFrame.right;
    vLayer->tgtRect.bottom = layer->displayFrame.bottom;

    if (layer->flags & HWC_GEOMETRY_CHANGED)
        vLayer->inFlags |= GEOMETRY_CHANGED;

    vLayer->acquireFenceFd = layer->acquireFenceFd;

    if (layer->compositionType == HWC_FRAMEBUFFER_TARGET || isSkipLayer(layer))
        vLayer->inFlags |= SKIP_LAYER;
}

int VPUClient::setupVpuSession(hwc_context_t *ctx, int display,
                                            hwc_display_contents_1_t* list)
{
    memset(vList[display], 0, sizeof(LayerList));
    memset(mProp, 0, sizeof(mProp));
    mNumVpuLayers = 0;

    // setting up the layer
    LayerList *vpuList = vList[display];
    vpuList->numLayers = list->numHwLayers;
    for (unsigned int i=0; i<(list->numHwLayers); ++i) {
        hwc_layer_1_t *layer = &list->hwLayers[i];
        Layer *vLayer = &vpuList->layers[i];
        VpuLayerProp* prop = &mProp[display][i];

        // Storing the sourceCropf, as it's going to be changed for overlay Set
        // will be restored after overlay set in prepare.
        prop->sourceCropf = layer->sourceCropf;

        // filling up the vpu list
        setLayer(layer, vLayer);
        ALOGD_IF(isDebug2(), "%s:Done setting lyr:%d for VFM", __FUNCTION__, i);
    }

    if (mVPU->setupVpuSession((DISPLAY_ID)display, vpuList) != NO_ERROR) {
        //error in vpu prepare
        ALOGE("%s: ERROR in VPU::setupVpuSession", __FUNCTION__);
        return -1;
    }
    ALOGD_IF(isDebug2(), "%s: Done VFM: setupVpuSession", __FUNCTION__);

    mGpuFallback = true;
    LayerProp *layerProp = ctx->layerProp[display];
    // check if the pipeID is already set for this layer, then will need to
    // ensure that it is reserved in overlay
    for (unsigned int i=0; i<(vpuList->numLayers); ++i) {
        hwc_layer_1_t *layer = &list->hwLayers[i];
        Layer *vLayer = &vpuList->layers[i];
        VpuLayerProp* prop = &mProp[display][i];

        if (vLayer->outFlags & VPU_LAYER) {
            ALOGD_IF(isDebug(), "%s: VPU supported layer:%d", __FUNCTION__, i);

            mNumVpuLayers++;
            mGpuFallback = false;
            // Reserving the pipe used in last iteration for the same layer
            if ((vLayer->outFlags & RESERVE_PREV_PIPES) &&
                                            vLayer->sDestPipes.numPipes > 0) {
                prop->pipeCount = vLayer->sDestPipes.numPipes;
                if (prop->pipeCount == 1) {
                    setPipeId(prop, vLayer->sDestPipes.pipe[0]);
                    ALOGD_IF(isDebug(), "%s: VPU: Reserved pipe:%d",
                            __FUNCTION__, prop->pipeID[0]);
                }
                else if (prop->pipeCount == 2) {
                    setPipeId(prop, vLayer->sDestPipes.pipe[0],
                                                    vLayer->sDestPipes.pipe[1]);
                    ALOGD_IF(isDebug(), "%s: VPU: Reserved lpipe:%d, rpipe:%d",
                            __FUNCTION__, prop->pipeID[0], prop->pipeID[1]);
                }
                else {
                    ALOGE("%s: Invalid pipeCount for resevation", __FUNCTION__);
                }
            }
            else {
                ALOGD_IF(isDebug(), "%s: 1st vid frame for VPU", __FUNCTION__);
                prop->firstBuffer = true;
            }

            // marking the layer pipes for vpu.
            prop->vpuLayer = true;
            prop->layer = layer;
            layer->flags |= HWC_VPU_PIPE;

            // getting image width and height
            prop->width = layer->displayFrame.right - layer->displayFrame.left;
            prop->height = layer->displayFrame.bottom - layer->displayFrame.top;

            //setting source crop = dest crop (only for layers drawn by vpu,
            // since we know it will be scaled up/down by vpu)
            layer->sourceCropf.left = 0.0;
            layer->sourceCropf.top = 0.0;
            layer->sourceCropf.right = (float) prop->width;
            layer->sourceCropf.bottom = (float) prop->height;

            // setting the flag so that mdpComp wont recognize it as the MDPCOMP
            layerProp[i].mFlags |= HWC_VPUCOMP;

            // TODO: need to get the proper solution for color fill

            // storing locally the vpu supported format from VFM
            prop->format = vLayer->vpuOutPixFmt;
            ALOGD_IF(isDebug(), "%s: MDP: sourceCropf: w:%d h:%d format:%d",
                    __FUNCTION__, prop->width, prop->height, prop->format);
        }
    }
    return 0;
}

bool VPUClient::allocResLayerPipes(hwc_context_t* ctx, int dpy,
                                         hwc_display_contents_1_t* list)
{
    overlay::Overlay& ov = *ctx->mOverlay;
    for (unsigned int i=0; i<(list->numHwLayers); ++i) {
        int pipeid = -1;
        VpuLayerProp* prop = &mProp[dpy][i];

        // checking if there is already a reserved pipe for this layer
        // then use the same allocated pipe for this layer
        getPipeId(prop, pipeid);

        if (pipeid != -1) {
            // there is a reserved pipe for this layer.
            ovutils::eDest dest = ov.reservePipe(pipeid);
            if (dest == ovutils::OV_INVALID) {
                ALOGE("%s: Unable to get reserved pipe: layer#%d",
                        __FUNCTION__, i);
                return false;
            }

            // setting dest locally
            setDest(prop, dest);
            ALOGD_IF(isDebug(), "%s: Reserving pipe:%d, dest:%d ", __FUNCTION__,
                    pipeid, dest);
        }
        else {
            ALOGD_IF(isDebug2(), "%s: No reserved pipe for layer:%d",
                    __FUNCTION__, i);
        }
    }
    return true;
}

bool VPUClient::allocLayerPipes(hwc_context_t* ctx, int dpy,
                                         hwc_display_contents_1_t* list)
{
    // checking if the pipes are reserved for any layer,
    // if yes, then updating the index of the pipes
    if (!allocResLayerPipes(ctx, dpy, list)) {
        ALOGE("%s: Reserved pipe alloc failed", __FUNCTION__);
        return false;
    }

    for (unsigned int i=0; i<(list->numHwLayers); ++i) {
        hwc_layer_1_t* layer = &list->hwLayers[i];
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        VpuLayerProp* prop = &mProp[dpy][i];
        int pipe = -1;
        overlay::Overlay& ov = *ctx->mOverlay;

        // only care about the layers supported by VPU
        if (!prop->vpuLayer)
            continue;

        // continue if this layer has reserved pipe
        getPipeId(prop, pipe);
        if (pipe != -1)
            continue;

        ovutils::eDest dest = ov.nextPipe(ovutils::OV_MDP_PIPE_VG, dpy,
                overlay::Overlay::MIXER_DEFAULT);
        if (dest == ovutils::OV_INVALID) {
            ALOGE("%s: Unable to allocate pipe for layer#%d", __FUNCTION__, i);
            return false;
        }

        // setting dest locally
        setDest(prop, dest);
        ALOGD_IF(isDebug(), "%s: Newly allocated pipe_dest:%d", __FUNCTION__,
                dest);
    }
    return true;
}

bool VPUClient::allocResLayerPipesSplit(hwc_context_t* ctx, int dpy,
                                         hwc_display_contents_1_t* list)
{
    overlay::Overlay& ov = *ctx->mOverlay;
    for (unsigned int i=0; i<(list->numHwLayers); ++i) {
        int lpipeid = -1;
        int rpipeid = -1;
        VpuLayerProp* prop = &mProp[dpy][i];

        // checking if there is already a reserved pipe for this layer
        // then use the same allocated pipe for this layer
        getPipeId(prop, lpipeid, rpipeid);

        if (lpipeid != -1 && rpipeid != -1) {
            ovutils::eDest ldest = ov.reservePipe(lpipeid);
            if (ldest == ovutils::OV_INVALID) {
                ALOGD_IF(isDebug(), "%s: Unable to get reserved pipe-lsplit: "
                         "layer#%d", __FUNCTION__, i);
                return false;
            }

            ovutils::eDest rdest = ov.reservePipe(rpipeid);
            if (rdest == ovutils::OV_INVALID) {
                ALOGD_IF(isDebug(), "%s: Unable to get reserved pipe-rsplit: "
                         "layer#%d", __FUNCTION__, i);
                return false;
            }

            setDest(prop, ldest, rdest);
            ALOGD_IF(isDebug(), "%s: Reserve lpipe:%d, ldest:%d, rpipe:%d, "
                    "rdest:%d", __FUNCTION__, lpipeid, ldest, rpipeid, rdest);
        }
        else if (lpipeid != -1 || rpipeid != -1) {
            ALOGE("%s: Bug: only one pipe reserved!", __FUNCTION__);
            return false;
        }
    }
    return true;
}

bool VPUClient::allocLayerPipesSplit(hwc_context_t* ctx, int dpy,
                                         hwc_display_contents_1_t* list)
{
    // checking if the pipes are reserved for any layer,
    // if yes, then updating the index of the pipes
    if (!allocResLayerPipesSplit(ctx, dpy, list)) {
        ALOGE("%s: Reserved pipe alloc failed", __FUNCTION__);
        return false;
    }

    for (unsigned int i=0; i<(list->numHwLayers); ++i) {
        hwc_layer_1_t* layer = &list->hwLayers[i];
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        VpuLayerProp* prop = &mProp[dpy][i];
        int lpipe, rpipe;
        overlay::Overlay& ov = *ctx->mOverlay;

        // only care about the layers supported by VPU
        if (!prop->vpuLayer)
            continue;

        // only care about the layers supported by VPU
        getPipeId(prop, lpipe, rpipe);
        if (lpipe != -1 && rpipe != -1)
            continue;

        ovutils::eDest ldest = ov.nextPipe(ovutils::OV_MDP_PIPE_VG, dpy,
                overlay::Overlay::MIXER_LEFT);
        if (ldest == ovutils::OV_INVALID) {
            ALOGE("%s: Unable to allocate pipe for layer#%d", __FUNCTION__, i);
            return false;
        }

        ovutils::eDest rdest = ov.nextPipe(ovutils::OV_MDP_PIPE_VG, dpy,
                overlay::Overlay::MIXER_RIGHT);
        if (rdest == ovutils::OV_INVALID) {
            ALOGE("%s: Unable to allocate pipe for layer#%d", __FUNCTION__, i);
            return false;
        }

        // setting dests locally
        setDest(prop, ldest, rdest);
        ALOGD_IF(isDebug(), "%s: Newly allocated ldest:%d rdest:%d",
                __FUNCTION__, ldest, rdest);
    }
    return true;
}

bool VPUClient::configureLayers(hwc_context_t* ctx, int dpy,
                                         hwc_display_contents_1_t* list)
{
    for (unsigned int i=0; i<(list->numHwLayers); ++i) {
        VpuLayerProp* prop = &mProp[dpy][i];
        hwc_layer_1_t* layer = &list->hwLayers[i];

        if (!prop->vpuLayer)
            continue;

        eMdpFlags mdpFlags = OV_MDP_BACKEND_COMPOSITION;
        eZorder zOrder = static_cast<eZorder>(i);
        eIsFg isFg = IS_FG_OFF;
        setPipeCount(prop, 1);
        eDest dest = (eDest) getDest(prop, 0);

        ALOGD_IF(isDebug(),"%s: configuring: layer:%p z_order:%d dest_pipe:%d",
                __FUNCTION__, layer, zOrder, dest);

        if (configureNonSplit(ctx, layer, dpy, mdpFlags, zOrder, isFg,
                            dest, NULL)) {
            ALOGE("%s: Failed to configure overlay for layer %d",
                    __FUNCTION__, i);
            return false;
        }
        ALOGD_IF(isDebug2(), "%s: layer:%d configured!", __FUNCTION__, i);

        // Pipe is successfully allocated for this layer; retrieving it from
        // overlay
        int pipeId = ctx->mOverlay->getPipeId((eDest) getDest(prop, 0));
        setPipeId(prop, pipeId);

        ALOGD_IF(isDebug(), "%s: allocated pipe:%d layer:%d", __FUNCTION__,
                    pipeId, i);
    }
    return true;
}

bool VPUClient::configureLayersSplit(hwc_context_t* ctx, int dpy,
                                         hwc_display_contents_1_t* list)
{
    for (unsigned int i=0; i<(list->numHwLayers); ++i) {
        VpuLayerProp* prop = &mProp[dpy][i];
        hwc_layer_1_t* layer = &list->hwLayers[i];

        if (!prop->vpuLayer)
            continue;

        eMdpFlags mdpFlags = OV_MDP_BACKEND_COMPOSITION;
        eZorder zOrder = static_cast<eZorder>(i);
        eIsFg isFg = IS_FG_OFF;
        setPipeCount(prop, 2);
        eDest ldest = (eDest) getDest(prop, 0);
        eDest rdest = (eDest) getDest(prop, 1);

        ALOGD_IF(isDebug(),"%s: configuring: layer:%p z_order:%d dest_pipeL:%d"
                "dest_pipeR:%d",__FUNCTION__, layer, zOrder, ldest, rdest);

        if (configureSplit(ctx, layer, dpy, mdpFlags, zOrder, isFg, ldest,
                            rdest, NULL)) {
            ALOGE("%s: Failed to configure overlay for layer %d",
                    __FUNCTION__, i);
            return false;
        }
        ALOGD_IF(isDebug2(), "%s: layer:%d configured!", __FUNCTION__, i);

        // Pipe is successfully allocated for this layer; retrieving it from
        // overlay
        int lpipeId = ctx->mOverlay->getPipeId((eDest) getDest(prop, 0));
        int rpipeId = ctx->mOverlay->getPipeId((eDest) getDest(prop, 1));
        setPipeId(prop, lpipeId, rpipeId);

        ALOGD_IF(isDebug(), "%s: allocated l-pipe:%d - r-pipe:%d for layer:%d",
                __FUNCTION__, lpipeId, rpipeId, i);
    }
    return true;
}

void VPUClient::setMDPCompLayerFlags(hwc_context_t *ctx, int dpy,
                                   hwc_display_contents_1_t* list)
{
    LayerProp *layerProp = ctx->layerProp[dpy];

    // disableGpu only disables gpu for video layer. The expected behavior is to
    // show a blank screen in case VPU doesnt support a video layer, and gpu
    // fallback is disabled by the user.
    bool disableGpu = false;
    char property[PROPERTY_VALUE_MAX];
    if ((property_get("persist.hwc.noGpuFallback", property, NULL) > 0) &&
        (!strncmp(property, "1", PROPERTY_VALUE_MAX ) ||
            (!strncasecmp(property,"true", PROPERTY_VALUE_MAX )))) {
        ALOGD_IF(isDebug(), "%s: GPU fallback is disabled through prop",
                __FUNCTION__);
        disableGpu = true;
    }

    // no layers are supported by vpu
    if (mGpuFallback && !disableGpu) {
        ALOGD_IF(isDebug(), "%s: No VPU supported layers - Falling back to GPU",
                __FUNCTION__);
        return;
    }

    for (unsigned int i=0; i<(list->numHwLayers); ++i) {
        hwc_layer_1_t* layer = &(list->hwLayers[i]);
        VpuLayerProp* prop = &mProp[dpy][i];
        private_handle_t *hnd = (private_handle_t *)layer->handle;

        // mark vpu layers as HWC_OVERLAY, and those video layers that
        // are not supported by vpu and gpu fallback is disabled by the
        // user.
        if (prop->vpuLayer || (isYuvBuffer(hnd) && disableGpu)) {
            layer->compositionType = HWC_OVERLAY;
            layer->hints |= HWC_HINT_CLEAR_FB;
            ALOGD_IF(isDebug(), "%s: Marking layer:%d as overlay",
                    __FUNCTION__, i);
        }
    }
}

int VPUClient::prepare(hwc_context_t *ctx, int display,
            hwc_display_contents_1_t* list)
{
    if (!mVPU) {
        return -1;
    }

    const int numLayers = ctx->listStats[display].numAppLayers;
    //number of app layers exceeds MAX_NUM_APP_LAYERS fall back to GPU
    //do not cache the information for next draw cycle.
    if (numLayers > MAX_NUM_APP_LAYERS) {
        ALOGE("%s: Number of App layers exceeded the limit ",__FUNCTION__);
        return -1;
    }

    if (setupVpuSession(ctx, display, list)) {
        ALOGD_IF(isDebug(), "%s: Vpu session setup failed! ",__FUNCTION__);
        return -1;
    }

    LayerProp *layerProp = ctx->layerProp[display];
    bool isSplit = isDisplaySplit(ctx, display);
    ALOGD_IF(isDebug2(), "%s: Split Pipe:%d ", __FUNCTION__,
            isSplit ? 1 : 0);

    // setting up the layer
    LayerList *vpuList = vList[display];
    vpuList->numLayers = list->numHwLayers;

    // Prepare FB Update at z-0
    if (numLayers > mNumVpuLayers) {
        if (!ctx->mFBUpdate[display]->prepare(ctx, list, mNumVpuLayers)) {
            ALOGD_IF(isDebug(), "%s configure framebuffer failed",
                    __FUNCTION__);
            return -1;
        }
    }

    // Allocate pipe for layers
    if (!isSplit ? !allocLayerPipes(ctx, display, list) :
                 !allocLayerPipesSplit(ctx, display, list)) {
        ALOGD_IF(isDebug(), "%s: Unable to allocate MDP pipes", __FUNCTION__);
        return -1;
    }

    // Configure layers
    if (!isSplit ? !configureLayers(ctx, display, list) :
                 !configureLayersSplit(ctx, display, list)) {
        ALOGD_IF(isDebug(), "%s: Unable to configure MDP pipes", __FUNCTION__);
        return -1;
    }

    // Set layer flags for MDP/VPU composition
    setMDPCompLayerFlags(ctx, display, list);

    for (unsigned int i=0; i<(list->numHwLayers); ++i) {
        VpuLayerProp* prop = &mProp[display][i];

        if (!prop->vpuLayer)
            continue;

        hwc_layer_1_t *layer = &list->hwLayers[i];
        Layer *vLayer = &vpuList->layers[i];

        // re-storing the sourceCropf, as it was changed in setVpuSession for
        // overlay set
        layer->sourceCropf = prop->sourceCropf;

        // updating the pipe info inside vfm list
        if ( prop->pipeCount > 0  && prop->pipeCount <= MAX_PIPES_PER_LAYER ) {
            vLayer->sDestPipes.numPipes = prop->pipeCount;

            for (int j=0; j < prop->pipeCount; ++j) {
                // Setting pipe for VPU
                vLayer->sDestPipes.pipe[j] = prop->pipeID[j];
            }
        }
    }

    if (mVPU->prepare((DISPLAY_ID)display, vpuList) != NO_ERROR) {
        //error in vpu prepare
        ALOGE("%s: ERROR in VPU::prepare", __func__);
        return -1;
    }
    return 0;
}

bool VPUClient::queueHandle(hwc_context_t* ctx, VpuLayerProp* prop,
        private_handle_t* hnd)
{
    overlay::Overlay& ov = *ctx->mOverlay;
    ovutils::eDest dest = (eDest) getDest(prop, 0);

    int fd = hnd->fd;
    uint32_t offset = hnd->offset;

    if (dest != ovutils::OV_INVALID) {
        if (!ov.queueBuffer(fd, offset, dest)) {
            ALOGE("%s: queueBuffer failed", __FUNCTION__);
            return false;
        }
        else {
            ALOGD_IF(isDebug(), "%s: Queue handle successful: hnd:0x%x "
                    "dest:%d", __FUNCTION__, (unsigned int) hnd, dest);
        }
    }
    else {
        ALOGE("%s: Invalid Dest: dest:%d", __FUNCTION__, dest);
        return false;
    }
    return true;
}

bool VPUClient::queueHandleSplit(hwc_context_t* ctx, VpuLayerProp* prop,
        private_handle_t* hnd)
{
    overlay::Overlay& ov = *ctx->mOverlay;
    ovutils::eDest ldest = (eDest) getDest(prop, 0);
    ovutils::eDest rdest = (eDest) getDest(prop, 1);

    int fd = hnd->fd;
    uint32_t offset = hnd->offset;

    // play left mixer
    if (ldest != ovutils::OV_INVALID) {
        ALOGD_IF(isDebug(), "%s: Queuing left mixer", __FUNCTION__);
        if (!ov.queueBuffer(fd, offset, ldest)) {
            ALOGE("%s: queueBuffer failed for left mixer ", __FUNCTION__);
            return false;
        }
        else {
            ALOGD_IF(isDebug(), "%s: Queue left-handle successful: hnd:0x%x "
                    "ldest:%d", __FUNCTION__, (unsigned int) hnd, ldest);
        }
    }
    else {
        ALOGE("%s: Invalid l-Split Dest", __FUNCTION__);
        return false;
    }

    // play right mixer
    if (rdest != ovutils::OV_INVALID) {
        ALOGD_IF(isDebug(), "%s: Queuing right mixer", __FUNCTION__);
        if (!ov.queueBuffer(fd, offset, rdest)) {
            ALOGE("%s: queueBuffer failed for right mixer ", __FUNCTION__);
            return false;
        }
        else {
            ALOGD_IF(isDebug(), "%s: Queue right-handle successful: hnd:0x%x "
                    "rdest:%d", __FUNCTION__, (unsigned int) hnd, rdest);
        }
    }
    else {
        ALOGE("%s: Invalid r-Split Dest", __FUNCTION__);
        return false;
    }
    return true;
}

bool VPUClient::drawDummyLayers(hwc_context_t* ctx, int dpy,
                    hwc_display_contents_1_t* list)
{
    int err = 0;
    for (unsigned int i=0; i<(list->numHwLayers); ++i) {
        VpuLayerProp* prop = &mProp[dpy][i];

        if (!prop->vpuLayer)
            continue;

        // displaying blank screen for the first frame
        if (prop->firstBuffer) {
            ALOGD_IF(isDebug(), "%s: Displaying first (blank) frame",
                    __FUNCTION__);
            prop->firstBuffer = false;

            if (mHnd[dpy][i] != NULL)
                free_buffer(mHnd[dpy][i]);

            // TO-FIX: out dummy buffer is currently allocated based on
            // RGB888 format
            err = alloc_buffer(&mHnd[dpy][i], prop->width, prop->height,
                HAL_PIXEL_FORMAT_RGB_888, GRALLOC_USAGE_PRIVATE_IOMMU_HEAP);
            if (err == -1) {
                ALOGE("%s: Dummy buffer allocation failed!", __FUNCTION__);
                return false;
            }

            private_handle_t* hnd = mHnd[dpy][i];
            if (prop->format == HAL_PIXEL_FORMAT_RGB_888) {
                ALOGD_IF(isDebug(), "%s: Format: RGB888", __FUNCTION__);
                memset((void*)hnd->base, 0x0, hnd->size);
            }
            else if (prop->format ==
                    HAL_PIXEL_FORMAT_YCbCr_422_I_10BIT_COMPRESSED) {
                ALOGD_IF(isDebug(), "%s: Format: 10BIT_BWC", __FUNCTION__);
                memset((void*)hnd->base, 0xaa, hnd->size);
            }
            else {
                ALOGE("%s: Error! Wrong VPU out format - layer:%d",
                        __FUNCTION__, i);
                return false;
            }

            bool isSplit = isDisplaySplit(ctx, dpy);
            if (!isSplit ? !queueHandle(ctx, prop, hnd) :
                        !queueHandleSplit(ctx, prop, hnd)) {
                ALOGD_IF(isDebug(), "%s: Error in queue handle: layer:%d",
                        __FUNCTION__, i);
                return false;
            }
            else {
                ALOGD_IF(isDebug(), "%s: queue handle successful: hnd:0x%x "
                        "layer:%d", __FUNCTION__, (unsigned int) hnd, i);
            }
        }
    }
    return true;
}

int VPUClient::predraw(hwc_context_t *ctx, int display,
                                        hwc_display_contents_1_t* list)
{
    if (!mVPU) {
        return -1;
    }

    if (!ctx || !list) {
        ALOGE("%s: invalid contxt or list",__FUNCTION__);
        return -1;
    }

    if (ctx->listStats[display].numAppLayers > MAX_NUM_APP_LAYERS) {
        ALOGE("%s: Exceeding max layer count", __FUNCTION__);
        return -1;
    }

    // Although all the video layers are composed through VPU, but still need to
    // queue the first buffer (blank screen) to mdp in order to initialize the
    // settings
    if (!drawDummyLayers(ctx, display, list)) {
        ALOGE("%s: Failed to draw the first layer through overlay",
                __FUNCTION__);
        return -1;
    }
    return 0;
}

int VPUClient::draw(hwc_context_t *ctx, int display,
                                        hwc_display_contents_1_t* list)
{
    if (!mVPU) {
        return -1;
    }

    LayerList *vpuList = vList[display];
    vpuList->numLayers = list->numHwLayers;

    for (unsigned int i=0; i<(list->numHwLayers); ++i) {
        hwc_layer_1_t *layer = &list->hwLayers[i];
        Layer *vLayer = &vpuList->layers[i];

        // setting layer info again for the update content.
        setLayer(layer, vLayer);
    }

    // queuing the buffer to VPU
    if (mVPU->draw((DISPLAY_ID)display, vpuList) != NO_ERROR) {
        //error in vpu draw
        ALOGE("%s: ERROR in VPU::draw", __func__);
        return -1;
    }

    ALOGD_IF(isDebug2(), "%s: Done VFM draw", __FUNCTION__);

    LayerProp *layerProp = ctx->layerProp[display];
    // setting releaseFenceFd for the vpu layer
    for (unsigned int i=0; i<(vpuList->numLayers); ++i) {

        VpuLayerProp* prop = &mProp[display][i];
        if (!prop->vpuLayer)
            continue;

        hwc_layer_1_t *layer = &list->hwLayers[i];
        Layer *vLayer = &vpuList->layers[i];

        // TODO: Fix properly once the releaseFenceFd is implemented
        layer->releaseFenceFd = vLayer->releaseFenceFd;
        ALOGD_IF(isDebug(), "%s: releaseFd:%d for layer:%d", __FUNCTION__,
                layer->releaseFenceFd, i);
    }
    return 0;
}

int VPUClient::getLayerIdx(int dpy, hwc_layer_1_t *layer)
{
    for (int i=0; i < MAX_NUM_APP_LAYERS; ++i) {
        VpuLayerProp* prop = &mProp[dpy][i];

        if (!prop->vpuLayer)
            continue;

        if (prop->layer == layer) {
            ALOGD_IF(isDebug2(), "%s: OUT - dpy:%d", __FUNCTION__, dpy);
            return i;
        }
    }
    return -1;
}

int VPUClient::getLayerFormat(int dpy, hwc_layer_1_t *layer)
{
    if (!mVPU) {
        return -1;
    }

    int idx = -1;
    if ((idx = getLayerIdx(dpy, layer)) == -1) {
        ALOGE("%s: Layer not found!", __FUNCTION__);
        return -1;
    }

    VpuLayerProp* prop = &mProp[dpy][idx];
    ALOGD_IF(isDebug(), "%s: layer:%d format:0x%x", __FUNCTION__, idx,
            (unsigned int) prop->format);

    return prop->format;
}

int VPUClient::getWidth(int dpy, hwc_layer_1_t *layer)
{
    if (!mVPU) {
        return -1;
    }

    int idx = -1;
    if ((idx = getLayerIdx(dpy, layer)) == -1) {
        ALOGE("%s: Layer not found!", __FUNCTION__);
        return -1;
    }

    VpuLayerProp* prop = &mProp[dpy][idx];
    ALOGD_IF(isDebug(), "%s: layer:%d width:%d", __FUNCTION__, idx,
            prop->width);

    return prop->width;
}

int VPUClient::getHeight(int dpy, hwc_layer_1_t *layer)
{
    if (!mVPU) {
        return -1;
    }

    int idx = -1;
    if ((idx = getLayerIdx(dpy, layer)) == -1) {
        ALOGE("%s: Layer not found!", __FUNCTION__);
        return -1;
    }

    VpuLayerProp* prop = &mProp[dpy][idx];
    ALOGD_IF(isDebug(), "%s: layer:%d height:%d", __FUNCTION__, idx,
            prop->height);

    return prop->height;
}

// TODO: getter function has side-effect. Need to cleanup
void VPUClient::getPipeId(VpuLayerProp* prop, int &pipe)
{
    pipe = (prop->pipeCount == 1) ? (prop->pipeID[0]) : -1;
}

void VPUClient::getPipeId(VpuLayerProp* prop, int &lPipe, int &rPipe)
{
    lPipe = (prop->pipeCount == 2) ? (prop->pipeID[0]) : -1;
    rPipe = (prop->pipeCount == 2) ? (prop->pipeID[1]) : -1;
}

int VPUClient::getDest(VpuLayerProp* prop, int pipenum)
{
    return (prop->pipeCount > 0) ? (prop->dest[pipenum]) : -1;
}

void VPUClient::setPipeCount(VpuLayerProp* prop, int count)
{
    prop->pipeCount = count;
}

void VPUClient::setPipeId(VpuLayerProp* prop, int lPipeId, int rPipeId)
{
    prop->pipeCount = 2;
    prop->pipeID[0] = lPipeId;
    prop->pipeID[1] = rPipeId;
}

void VPUClient::setPipeId(VpuLayerProp* prop, int pipeId)
{
    prop->pipeCount = 1;
    prop->pipeID[0] = pipeId;
}

void VPUClient::setDest(VpuLayerProp* prop, int lDest, int rDest)
{
    prop->dest[0] = lDest;
    prop->dest[1] = rDest;
}

void VPUClient::setDest(VpuLayerProp* prop, int dest)
{
    prop->dest[0] = dest;
}

bool VPUClient::supportedVPULayer(VpuLayerProp* prop)
{
    if (!prop->vpuLayer)
        return false;

    return true;
}

bool VPUClient::supportedVPULayer(int dpy, hwc_layer_1_t *layer)
{
    if (!mVPU) {
        return false;
    }

    int idx = -1;
    if ((idx = getLayerIdx(dpy, layer)) == -1) {
        ALOGD_IF(isDebug(), "%s: Layer not found!", __FUNCTION__);
        return false;
    }
    return true;
}

int VPUClient::processCommand(uint32_t command,
                              const Parcel* inParcel, Parcel* outParcel)
{
    if (!mVPU)
        return 0;

    return mVPU->processCommand(command, inParcel, outParcel);
}

}; // namespace qhwc
