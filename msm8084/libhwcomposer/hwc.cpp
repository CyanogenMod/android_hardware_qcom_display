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

#include "hwc_utils.h"
#include "hwc_video.h"
#include "hwc_uimirror.h"
#include "hwc_copybit.h"

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
        getLayerStats(ctx, list);
        if(VideoOverlay::prepare(ctx, list)) {
            ctx->overlayInUse = true;
            //Nothing here
        } else if(UIMirrorOverlay::prepare(ctx, list)) {
            ctx->overlayInUse = true;
        } else if (0) {
            //Other features
            ctx->overlayInUse = true;
        } else { // Else set this flag to false, otherwise video cases
                 // fail in non-overlay targets.
            ctx->overlayInUse = false;
        }
        CopyBit::prepare(ctx, list);
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
        CopyBit::draw(ctx, list, (EGLDisplay)dpy, (EGLSurface)sur);
        EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
        UIMirrorOverlay::draw(ctx);
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
        ALOGE("hwc_device_close null device pointer");
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
        initContext(dev);
        dev->device.common.tag     = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module  = const_cast<hw_module_t*>(module);
        dev->device.common.close   = hwc_device_close;
        dev->device.prepare        = hwc_prepare;
        dev->device.set            = hwc_set;
        dev->device.registerProcs  = hwc_registerProcs;
        *device                    = &dev->device.common;
        status = 0;
    }
    return status;
}
