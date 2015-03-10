/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2014, The Linux Foundation. All rights reserved.
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
#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <overlayWriteback.h>
#include "hwc_utils.h"
#include "hwc_fbupdate.h"
#include "hwc_mdpcomp.h"
#include "hwc_dump_layers.h"
#include "hwc_copybit.h"
#include "hwc_virtual.h"
#include "sync/sync.h"
#include <utils/Trace.h>

#define HWCVIRTUAL_LOG 0

using namespace qhwc;
using namespace overlay;

bool HWCVirtualVDS::sVDDumpEnabled = false;

void HWCVirtualVDS::init(hwc_context_t *ctx) {
    const int dpy = HWC_DISPLAY_VIRTUAL;
    mScalingWidth = 0, mScalingHeight = 0;
    initCompositionResources(ctx, dpy);

    if(ctx->mFBUpdate[dpy])
        ctx->mFBUpdate[dpy]->reset();
    if(ctx->mMDPComp[dpy])
        ctx->mMDPComp[dpy]->reset();
}

void HWCVirtualVDS::destroy(hwc_context_t *ctx, size_t /*numDisplays*/,
                       hwc_display_contents_1_t** displays) {
    int dpy = HWC_DISPLAY_VIRTUAL;

    //Cleanup virtual display objs, since there is no explicit disconnect
    if(ctx->dpyAttr[dpy].connected && (displays[dpy] == NULL)) {
        ctx->dpyAttr[dpy].connected = false;
        ctx->dpyAttr[dpy].isPause = false;

        destroyCompositionResources(ctx, dpy);

        // signal synclock to indicate successful wfd teardown
        ctx->mWfdSyncLock.lock();
        ctx->mWfdSyncLock.signal();
        ctx->mWfdSyncLock.unlock();
    }
}

int HWCVirtualVDS::prepare(hwc_composer_device_1 *dev,
        hwc_display_contents_1_t *list) {
    ATRACE_CALL();
    //XXX: Fix when framework support is added
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    const int dpy = HWC_DISPLAY_VIRTUAL;

    if (list && list->outbuf && list->numHwLayers > 0) {
        reset_layer_prop(ctx, dpy, (int)list->numHwLayers - 1);
        uint32_t last = (uint32_t)list->numHwLayers - 1;
        hwc_layer_1_t *fbLayer = &list->hwLayers[last];
        int fbWidth = 0, fbHeight = 0;
        getLayerResolution(fbLayer, fbWidth, fbHeight);
        ctx->dpyAttr[dpy].xres = fbWidth;
        ctx->dpyAttr[dpy].yres = fbHeight;

        if(ctx->dpyAttr[dpy].connected == false) {
            ctx->dpyAttr[dpy].connected = true;
            ctx->dpyAttr[dpy].isPause = false;
            // We set the vsync period to the primary refresh rate, leaving
            // it up to the consumer to decide how fast to consume frames.
            ctx->dpyAttr[dpy].vsync_period
                              = ctx->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period;
            ctx->dpyAttr[dpy].fbformat = HAL_PIXEL_FORMAT_RGBA_8888;
            init(ctx);
            // XXX: for architectures with limited resources we would normally
            // allow one padding round to free up resources but this breaks
            // certain use cases.
        }
        if(!ctx->dpyAttr[dpy].isPause) {
            ctx->dpyAttr[dpy].isConfiguring = false;
            ctx->dpyAttr[dpy].fd = Writeback::getInstance()->getFbFd();
            private_handle_t *ohnd = (private_handle_t *)list->outbuf;

            setMDPScalingMode(ctx, ohnd, dpy);

            mScalingWidth = getWidth(ohnd);
            mScalingHeight = getHeight(ohnd);

            Writeback::getInstance()->configureDpyInfo(mScalingWidth,
                                                        mScalingHeight);
            setListStats(ctx, list, dpy);

            if(ctx->mMDPComp[dpy]->prepare(ctx, list) < 0) {
                const int fbZ = 0;
                if(not ctx->mFBUpdate[dpy]->prepareAndValidate(ctx, list, fbZ))
                {
                    ctx->mOverlay->clear(dpy);
                    ctx->mLayerRotMap[dpy]->clear();
                }
            }
        } else {
            /* Virtual Display is in Pause state.
             * Mark all application layers as OVERLAY so that
             * GPU will not compose.
             */
            Writeback::getInstance(); //Ensure that WB is active during pause
            for(size_t i = 0 ;i < (size_t)(list->numHwLayers - 1); i++) {
                hwc_layer_1_t *layer = &list->hwLayers[i];
                layer->compositionType = HWC_OVERLAY;
            }
        }
    }
    return 0;
}

int HWCVirtualVDS::set(hwc_context_t *ctx, hwc_display_contents_1_t *list) {
    ATRACE_CALL();
    int ret = 0;
    const int dpy = HWC_DISPLAY_VIRTUAL;

    if (list && list->outbuf && list->numHwLayers > 0) {
        uint32_t last = (uint32_t)list->numHwLayers - 1;
        hwc_layer_1_t *fbLayer = &list->hwLayers[last];

        if(ctx->dpyAttr[dpy].connected
                && (!ctx->dpyAttr[dpy].isPause))
        {
            private_handle_t *ohnd = (private_handle_t *)list->outbuf;
            int format = ohnd->format;
            if (format == HAL_PIXEL_FORMAT_RGBA_8888)
                format = HAL_PIXEL_FORMAT_RGBX_8888;
            Writeback::getInstance()->setOutputFormat(
                                    utils::getMdpFormat(format));

            // Configure WB secure mode based on output buffer handle
            if(! Writeback::getInstance()->setSecure(isSecureBuffer(ohnd)))
            {
                ALOGE("Failed to set WB secure mode: %d for virtual display",
                    isSecureBuffer(ohnd));
                return false;
            }

            int fd = -1; //FenceFD from the Copybit
            hwc_sync(ctx, list, dpy, fd);

            // Dump the layers for virtual
            if(ctx->mHwcDebug[dpy])
                ctx->mHwcDebug[dpy]->dumpLayers(list);

            if (!ctx->mMDPComp[dpy]->draw(ctx, list)) {
                ALOGE("%s: MDPComp draw failed", __FUNCTION__);
                ret = -1;
            }
            // We need an FB layer handle check to cater for this usecase:
            // Video is playing in landscape on primary, then launch
            // ScreenRecord app.
            // In this scenario, the first VDS draw call will have HWC
            // composition and VDS does nit involve GPU to get eglSwapBuffer
            // to get valid fb handle.
            if (fbLayer->handle && !ctx->mFBUpdate[dpy]->draw(ctx,
                        (private_handle_t *)fbLayer->handle)) {
                ALOGE("%s: FBUpdate::draw fail!", __FUNCTION__);
                ret = -1;
            }

            Writeback::getInstance()->queueBuffer(ohnd->fd,
                                        (uint32_t)ohnd->offset);
            if(!Overlay::displayCommit(ctx->dpyAttr[dpy].fd)) {
                ALOGE("%s: display commit fail!", __FUNCTION__);
                ret = -1;
            }

            if(sVDDumpEnabled) {
                char bufferName[128];
                // Dumping frame buffer
                sync_wait(fbLayer->acquireFenceFd, 1000);
                snprintf(bufferName, sizeof(bufferName), "vds.fb");
                dumpBuffer((private_handle_t *)fbLayer->handle, bufferName);
                // Dumping WB output for non-secure session
                if(!isSecureBuffer(ohnd)) {
                    sync_wait(list->retireFenceFd, 1000);
                    snprintf(bufferName, sizeof(bufferName), "vds.wb");
                    dumpBuffer(ohnd, bufferName);
                }
            }
        } else if(list->outbufAcquireFenceFd >= 0) {
            //If we dont handle the frame, set retireFenceFd to outbufFenceFd,
            //which will make sure, the framework waits on it and closes it.
            //The other way is to wait on outbufFenceFd ourselves, close it and
            //set retireFenceFd to -1. Since we want hwc to be async, choosing
            //the former.
            //Also dup because, the closeAcquireFds() will close the outbufFence
            list->retireFenceFd = dup(list->outbufAcquireFenceFd);
        }
    }

    closeAcquireFds(list);
    return ret;
}

/* We set scaling mode on the VD if the output handle width and height
   differs from the virtual frame buffer width and height. */
void HWCVirtualVDS::setMDPScalingMode(hwc_context_t* ctx,
        private_handle_t* ohnd, int dpy) {
    bool scalingMode = false;
    int fbWidth = ctx->dpyAttr[dpy].xres;
    int fbHeight =  ctx->dpyAttr[dpy].yres;
    if((getWidth(ohnd) != fbWidth) || (getHeight(ohnd) != fbHeight)) {
        scalingMode = true;
    }
    ctx->dpyAttr[dpy].mMDPScalingMode = scalingMode;

    ALOGD_IF(HWCVIRTUAL_LOG, "%s fb(%dx%d) outputBuffer(%dx%d) scalingMode=%d",
            __FUNCTION__, fbWidth, fbHeight,
            getWidth(ohnd), getHeight(ohnd), scalingMode);
}
