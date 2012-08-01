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

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <EGL/egl.h>

#include <overlay.h>
#include <fb_priv.h>
#include <mdp_version.h>
#include "hwc_utils.h"
#include "hwc_qbuf.h"
#include "hwc_video.h"
#include "hwc_uimirror.h"
#include "hwc_copybit.h"
#include "hwc_external.h"
#include "hwc_mdpcomp.h"
#include "hwc_extonly.h"

using namespace qhwc;

static int hwc_device_open(const struct hw_module_t* module,
                           const char* name,
                           struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        version_major: 2,
        version_minor: 0,
        id: HWC_HARDWARE_MODULE_ID,
        name: "Qualcomm Hardware Composer Module",
        author: "CodeAurora Forum",
        methods: &hwc_module_methods,
        dso: 0,
        reserved: {0},
    }
};

/*
 * Save callback functions registered to HWC
 */
static void hwc_registerProcs(struct hwc_composer_device* dev,
                              hwc_procs_t const* procs)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
        ALOGE("%s: Invalid context", __FUNCTION__);
        return;
    }
    ctx->device.reserved_proc[0] = (void*)procs;
}

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    ctx->overlayInUse = false;

    //Prepare is called after a vsync, so unlock previous buffers here.
    ctx->qbuf->unlockAllPrevious();
    return 0;

    if (LIKELY(list)) {
        //reset for this draw round
        VideoOverlay::reset();
        ExtOnly::reset();

        getLayerStats(ctx, list);
        // Mark all layers to COPYBIT initially
        CopyBit::prepare(ctx, list);
        if(VideoOverlay::prepare(ctx, list)) {
            ctx->overlayInUse = true;
            //Nothing here
        } else if(ExtOnly::prepare(ctx, list)) {
            ctx->overlayInUse = true;
        } else if(UIMirrorOverlay::prepare(ctx, list)) {
            ctx->overlayInUse = true;
        } else if(MDPComp::configure(dev, list)) {
            ctx->overlayInUse = true;
        } else if (0) {
            //Other features
            ctx->overlayInUse = true;
        } else { // Else set this flag to false, otherwise video cases
                 // fail in non-overlay targets.
            ctx->overlayInUse = false;
        }
    }

    return 0;
}

static int hwc_eventControl(struct hwc_composer_device* dev,
                             int event, int enabled)
{
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_module_t* m = reinterpret_cast<private_module_t*>(
                ctx->mFbDev->common.module);
    switch(event) {
        case HWC_EVENT_VSYNC:
            if(ioctl(m->framebuffer->fd, MSMFB_OVERLAY_VSYNC_CTRL, &enabled) < 0)
                ret = -errno;

            if(ctx->mExtDisplay->getExternalDisplay()) {
                ret = ctx->mExtDisplay->enableHDMIVsync(enabled);
            }
           break;
        default:
            ret = -EINVAL;
    }
    return ret;
}

static int hwc_query(struct hwc_composer_device* dev,
                     int param, int* value)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_module_t* m = reinterpret_cast<private_module_t*>(
        ctx->mFbDev->common.module);

    switch (param) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
        // Not supported for now
        value[0] = 0;
        break;
    case HWC_VSYNC_PERIOD:
        value[0] = 1000000000.0 / m->fps;
        ALOGI("fps: %d", value[0]);
        break;
    default:
        return -EINVAL;
    }
    return 0;

}

static int hwc_set(hwc_composer_device_t *dev,
                   hwc_display_t dpy,
                   hwc_surface_t sur,
                   hwc_layer_list_t* list)
{
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if (LIKELY(list)) {
        VideoOverlay::draw(ctx, list);
        ExtOnly::draw(ctx, list);
        CopyBit::draw(ctx, list, (EGLDisplay)dpy, (EGLSurface)sur);
        MDPComp::draw(ctx, list);
        EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
        UIMirrorOverlay::draw(ctx);
        if(ctx->mExtDisplay->getExternalDisplay())
           ctx->mExtDisplay->commit();
    } else {
        ctx->mOverlay->setState(ovutils::OV_CLOSED);
        ctx->qbuf->unlockAllPrevious();
    }

    if(!ctx->overlayInUse)
        ctx->mOverlay->setState(ovutils::OV_CLOSED);

    return ret;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    if(!dev) {
        ALOGE("%s: NULL device pointer", __FUNCTION__);
        return -1;
    }
    closeContext((hwc_context_t*)dev);
    free(dev);

    return 0;
}

static int hwc_device_open(const struct hw_module_t* module, const char* name,
                           struct hw_device_t** device)
{
    int status = -EINVAL;

    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
        struct hwc_context_t *dev;
        dev = (hwc_context_t*)malloc(sizeof(*dev));
        memset(dev, 0, sizeof(*dev));

        //Initialize hwc context
        initContext(dev);

        //Setup HWC methods
        hwc_methods_t *methods;
        methods = (hwc_methods_t *)malloc(sizeof(*methods));
        memset(methods, 0, sizeof(*methods));
        methods->eventControl = hwc_eventControl;

        dev->device.common.tag     = HARDWARE_DEVICE_TAG;
        dev->device.common.version = HWC_DEVICE_API_VERSION_0_3;
        dev->device.common.module  = const_cast<hw_module_t*>(module);
        dev->device.common.close   = hwc_device_close;
        dev->device.prepare        = hwc_prepare;
        dev->device.set            = hwc_set;
        dev->device.registerProcs  = hwc_registerProcs;
        dev->device.query          = hwc_query;
        dev->device.methods        = methods;
        *device                    = &dev->device.common;
        status = 0;
    }
    return status;
}
