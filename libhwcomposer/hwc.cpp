/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
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
#include "hwc_video.h"
#include "hwc_uimirror.h"
#include "external.h"
#include "hwc_mdpcomp.h"

#define VSYNC_DEBUG 0
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
static void hwc_registerProcs(struct hwc_composer_device_1* dev,
                              hwc_procs_t const* procs)
{
    ALOGI("%s", __FUNCTION__);
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
        ALOGE("%s: Invalid context", __FUNCTION__);
        return;
    }
    ctx->proc = procs;

    // Now that we have the functions needed, kick off
    // the uevent & vsync threads
    init_uevent_thread(ctx);
    init_vsync_thread(ctx);
}

//Helper
static void reset(hwc_context_t *ctx, int numDisplays) {
    memset(ctx->listStats, 0, sizeof(ctx->listStats));
    for(int i = 0; i < HWC_NUM_DISPLAY_TYPES; i++){
        ctx->overlayInUse[i] = false;
        ctx->listStats[i].yuvIndex = -1;
    }
}

static int hwc_prepare_primary(hwc_composer_device_1 *dev,
        hwc_display_contents_1_t *list) {
    hwc_context_t* ctx = (hwc_context_t*)(dev);

    if (LIKELY(list && list->numHwLayers > 1) &&
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].isActive) {

        uint32_t last = list->numHwLayers - 1;
        hwc_layer_1_t *fbLayer = &list->hwLayers[last];
        if(fbLayer->handle) {
            setListStats(ctx, list, HWC_DISPLAY_PRIMARY);
            if(VideoOverlay::prepare(ctx, list, HWC_DISPLAY_PRIMARY)) {
                ctx->overlayInUse[HWC_DISPLAY_PRIMARY] = true;
            } else if(MDPComp::configure(ctx, list)) {
                ctx->overlayInUse[HWC_DISPLAY_PRIMARY] = true;
            } else {
                ctx->overlayInUse[HWC_DISPLAY_PRIMARY] = false;
            }
        }
    }
    return 0;
}

static int hwc_prepare_external(hwc_composer_device_1 *dev,
        hwc_display_contents_1_t *list) {
    hwc_context_t* ctx = (hwc_context_t*)(dev);

    if (LIKELY(list && list->numHwLayers > 1) &&
        ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].isActive &&
        ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].connected) {

        setListStats(ctx, list, HWC_DISPLAY_EXTERNAL);

        uint32_t last = list->numHwLayers - 1;
        hwc_layer_1_t *fbLayer = &list->hwLayers[last];
        if(fbLayer->handle) {
            if(UIMirrorOverlay::prepare(ctx, fbLayer)) {
                ctx->overlayInUse[HWC_DISPLAY_EXTERNAL] = true;
            }

            if(VideoOverlay::prepare(ctx, list, HWC_DISPLAY_EXTERNAL)) {
                ctx->overlayInUse[HWC_DISPLAY_EXTERNAL] = true;
            } else {
                ctx->mOverlay[HWC_DISPLAY_EXTERNAL]->setState(
                        ovutils::OV_UI_MIRROR);
            }
        }
    }
    return 0;
}

static int hwc_prepare(hwc_composer_device_1 *dev, size_t numDisplays,
                       hwc_display_contents_1_t** displays)
{
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    reset(ctx, numDisplays);

    //If securing of h/w in progress skip comp using overlay.
    if(ctx->mSecuring == true) return 0;

    for (uint32_t i = 0; i < numDisplays; i++) {
        hwc_display_contents_1_t *list = displays[i];
        switch(i) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_prepare_primary(dev, list);
                break;
            case HWC_DISPLAY_EXTERNAL:
                ret = hwc_prepare_external(dev, list);
                break;
            default:
                ret = -EINVAL;
        }
    }
    return ret;
}

static int hwc_eventControl(struct hwc_composer_device_1* dev, int dpy,
                             int event, int enabled)
{
    int ret = 0;

    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_module_t* m = reinterpret_cast<private_module_t*>(
                ctx->mFbDev->common.module);
    pthread_mutex_lock(&ctx->vstate.lock);
    switch(event) {
        case HWC_EVENT_VSYNC:
            if (ctx->vstate.enable == enabled)
                break;
            ctx->vstate.enable = !!enabled;
            pthread_cond_signal(&ctx->vstate.cond);
            ALOGD_IF (VSYNC_DEBUG, "VSYNC state changed to %s",
                      (enabled)?"ENABLED":"DISABLED");
            break;
        default:
            ret = -EINVAL;
    }
    pthread_mutex_unlock(&ctx->vstate.lock);
    return ret;
}

static int hwc_blank(struct hwc_composer_device_1* dev, int dpy, int blank)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_module_t* m = reinterpret_cast<private_module_t*>(
        ctx->mFbDev->common.module);
    int ret = 0;
    ALOGD("%s: Doing Dpy=%d, blank=%d", __FUNCTION__, dpy, blank);
    switch(dpy) {
        case HWC_DISPLAY_PRIMARY:
            if(blank) {
                Locker::Autolock _l(ctx->mBlankLock);
                ctx->mOverlay[dpy]->setState(ovutils::OV_CLOSED);
                ret = ioctl(m->framebuffer->fd, FBIOBLANK, FB_BLANK_POWERDOWN);
            } else {
                ret = ioctl(m->framebuffer->fd, FBIOBLANK, FB_BLANK_UNBLANK);
            }
            break;
        case HWC_DISPLAY_EXTERNAL:
            if(blank) {
                //TODO actual
            } else {
            }
            break;
        default:
            return -EINVAL;
    }

    if(ret < 0) {
        ALOGE("%s: failed. Dpy=%d, blank=%d : %s",
                __FUNCTION__, dpy, blank, strerror(errno));
        return ret;
    }
    ALOGD("%s: Done Dpy=%d, blank=%d", __FUNCTION__, dpy, blank);
    ctx->dpyAttr[dpy].isActive = !blank;
    return 0;
}

static int hwc_query(struct hwc_composer_device_1* dev,
                     int param, int* value)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_module_t* m = reinterpret_cast<private_module_t*>(
        ctx->mFbDev->common.module);
    int supported = HWC_DISPLAY_PRIMARY_BIT;

    switch (param) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
        // Not supported for now
        value[0] = 0;
        break;
    case HWC_VSYNC_PERIOD: //Not used for hwc > 1.1
        value[0] = m->fps;
        ALOGI("fps: %d", value[0]);
        break;
    case HWC_DISPLAY_TYPES_SUPPORTED:
        if(ctx->mMDP.hasOverlay)
            supported |= HWC_DISPLAY_EXTERNAL_BIT;
        value[0] = supported;
        break;
    default:
        return -EINVAL;
    }
    return 0;

}

static int hwc_set_primary(hwc_context_t *ctx, hwc_display_contents_1_t* list) {
    int ret = 0;

    if (LIKELY(list && list->numHwLayers > 1) &&
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].isActive) {
        uint32_t last = list->numHwLayers - 1;
        hwc_layer_1_t *fbLayer = &list->hwLayers[last];

        hwc_sync(ctx, list, HWC_DISPLAY_PRIMARY);

        if (!VideoOverlay::draw(ctx, list, HWC_DISPLAY_PRIMARY)) {
            ALOGE("%s: VideoOverlay::draw fail!", __FUNCTION__);
            ret = -1;
        }
        if (MDPComp::draw(ctx, list)) {
            ALOGE("%s: MDPComp::draw fail!", __FUNCTION__);
            ret = -1;
        }
        //TODO We dont check for SKIP flag on this layer because we need PAN
        //always. Last layer is always FB
        if(list->hwLayers[last].compositionType == HWC_FRAMEBUFFER_TARGET) {
            if (ctx->mFbDev->post(ctx->mFbDev, fbLayer->handle)) {
                ALOGE("%s: ctx->mFbDev->post fail!", __FUNCTION__);
                return -1;
            }
        }
    }
    return ret;
}

static int hwc_set_external(hwc_context_t *ctx,
        hwc_display_contents_1_t* list) {
    int ret = 0;
    Locker::Autolock _l(ctx->mExtSetLock);

    if (LIKELY(list && list->numHwLayers > 1) &&
        ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].isActive &&
        ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].connected) {
        uint32_t last = list->numHwLayers - 1;
        hwc_layer_1_t *fbLayer = &list->hwLayers[last];

        hwc_sync(ctx, list, HWC_DISPLAY_EXTERNAL);

        if (!VideoOverlay::draw(ctx, list, HWC_DISPLAY_EXTERNAL)) {
            ALOGE("%s: VideoOverlay::draw fail!", __FUNCTION__);
            ret = -1;
        }

        private_handle_t *hnd = (private_handle_t *)fbLayer->handle;
        if(fbLayer->compositionType == HWC_FRAMEBUFFER_TARGET &&
                !(fbLayer->flags & HWC_SKIP_LAYER) && hnd) {
            if (!UIMirrorOverlay::draw(ctx, fbLayer)) {
                ALOGE("%s: UIMirrorOverlay::draw fail!", __FUNCTION__);
                ret = -1;
            }
        }
        if (!ctx->mExtDisplay->post()) {
            ALOGE("%s: ctx->mExtDisplay->post fail!", __FUNCTION__);
            return -1;
        }
    }
    return ret;
}

static int hwc_set(hwc_composer_device_1 *dev,
                   size_t numDisplays,
                   hwc_display_contents_1_t** displays)
{
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    Locker::Autolock _l(ctx->mBlankLock);

    for(uint32_t i = 0; i < HWC_NUM_DISPLAY_TYPES; i++) {
        if(!ctx->overlayInUse[i])
            ctx->mOverlay[i]->setState(ovutils::OV_CLOSED);
    }

    for (uint32_t i = 0; i < numDisplays; i++) {
        hwc_display_contents_1_t* list = displays[i];
        switch(i) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_set_primary(ctx, list);
                break;
            case HWC_DISPLAY_EXTERNAL:
                ret = hwc_set_external(ctx, list);
                break;
            default:
                ret = -EINVAL;
        }
    }
    return ret;
}

int hwc_getDisplayConfigs(struct hwc_composer_device_1* dev, int disp,
        uint32_t* configs, size_t* numConfigs) {
    int ret = 0;
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    //in 1.1 there is no way to choose a config, report as config id # 0
    //This config is passed to getDisplayAttributes. Ignore for now.
    switch(disp) {
        case HWC_DISPLAY_PRIMARY:
            if(*numConfigs > 0) {
                configs[0] = 0;
                *numConfigs = 1;
            }
            ret = 0; //NO_ERROR
            break;
        case HWC_DISPLAY_EXTERNAL:
            ret = -1; //Not connected
            if(ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].connected) {
                ret = 0; //NO_ERROR
                if(*numConfigs > 0) {
                    configs[0] = 0;
                    *numConfigs = 1;
                }
            }
            break;
    }
    return ret;
}

int hwc_getDisplayAttributes(struct hwc_composer_device_1* dev, int disp,
        uint32_t config, const uint32_t* attributes, int32_t* values) {

    hwc_context_t* ctx = (hwc_context_t*)(dev);
    //If hotpluggable displays are inactive return error
    if(disp == HWC_DISPLAY_EXTERNAL && !ctx->dpyAttr[disp].connected) {
        return -1;
    }

    //From HWComposer
    static const uint32_t DISPLAY_ATTRIBUTES[] = {
        HWC_DISPLAY_VSYNC_PERIOD,
        HWC_DISPLAY_WIDTH,
        HWC_DISPLAY_HEIGHT,
        HWC_DISPLAY_DPI_X,
        HWC_DISPLAY_DPI_Y,
        HWC_DISPLAY_NO_ATTRIBUTE,
    };

    const int NUM_DISPLAY_ATTRIBUTES = (sizeof(DISPLAY_ATTRIBUTES) /
            sizeof(DISPLAY_ATTRIBUTES)[0]);

    for (size_t i = 0; i < NUM_DISPLAY_ATTRIBUTES - 1; i++) {
        switch (attributes[i]) {
        case HWC_DISPLAY_VSYNC_PERIOD:
            values[i] = ctx->dpyAttr[disp].vsync_period;
            break;
        case HWC_DISPLAY_WIDTH:
            values[i] = ctx->dpyAttr[disp].xres;
            ALOGD("%s disp = %d, width = %d",__FUNCTION__, disp,
                    ctx->dpyAttr[disp].xres);
            break;
        case HWC_DISPLAY_HEIGHT:
            values[i] = ctx->dpyAttr[disp].yres;
            ALOGD("%s disp = %d, height = %d",__FUNCTION__, disp,
                    ctx->dpyAttr[disp].yres);
            break;
        case HWC_DISPLAY_DPI_X:
            values[i] = (int32_t) (ctx->dpyAttr[disp].xdpi*1000.0);
            break;
        case HWC_DISPLAY_DPI_Y:
            values[i] = (int32_t) (ctx->dpyAttr[disp].ydpi*1000.0);
            break;
        default:
            ALOGE("Unknown display attribute %d",
                    attributes[i]);
            return -EINVAL;
        }
    }
    return 0;
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
        dev->device.common.tag          = HARDWARE_DEVICE_TAG;
        dev->device.common.version      = HWC_DEVICE_API_VERSION_1_1;
        dev->device.common.module       = const_cast<hw_module_t*>(module);
        dev->device.common.close        = hwc_device_close;
        dev->device.prepare             = hwc_prepare;
        dev->device.set                 = hwc_set;
        dev->device.eventControl        = hwc_eventControl;
        dev->device.blank               = hwc_blank;
        dev->device.query               = hwc_query;
        dev->device.registerProcs       = hwc_registerProcs;
        dev->device.dump                = NULL;
        dev->device.getDisplayConfigs   = hwc_getDisplayConfigs;
        dev->device.getDisplayAttributes = hwc_getDisplayAttributes;
        *device = &dev->device.common;
        status = 0;
    }
    return status;
}
