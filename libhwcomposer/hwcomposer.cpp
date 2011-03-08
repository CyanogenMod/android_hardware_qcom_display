/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <hardware/hardware.h>
#include <hardware/overlay.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>

#include <hardware/hwcomposer.h>
#include <hardware/overlay.h>
#include <hardware/copybit.h>
#include <overlayLib.h>

#include <EGL/egl.h>
#include <gralloc_priv.h>

/*****************************************************************************/

// Enum containing the supported composition types
enum {
    COMPOSITION_TYPE_GPU,
    COMPOSITION_TYPE_MDP,
    COMPOSITION_TYPE_C2D,
    COMPOSITION_TYPE_CPU
};

enum LayerType{
    GPU,     // This layer is to be handled by Surfaceflinger
    OVERLAY, // This layer is to be handled by the overlay
    COPYBIT  // This layer is to be handled by copybit
};

struct hwc_context_t {
    hwc_composer_device_t device;
    /* our private state goes below here */
    overlay::Overlay* mOverlayLibObject;
    bool hdmiConnected;
};

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};


struct private_hwc_module_t {
    hwc_module_t base;
    overlay_control_device_t *overlayEngine;
    copybit_device_t *copybitEngine;
    int compositionType;
};

struct private_hwc_module_t HAL_MODULE_INFO_SYM = {
    base: {
        common: {
            tag: HARDWARE_MODULE_TAG,
            version_major: 1,
            version_minor: 0,
            id: HWC_HARDWARE_MODULE_ID,
            name: "Hardware Composer Module",
            author: "The Android Open Source Project",
            methods: &hwc_module_methods,
        }
   },
   overlayEngine: NULL,
   copybitEngine: NULL,
   compositionType: 0,
};

/*****************************************************************************/

static void dump_layer(hwc_layer_t const* l) {
    LOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}, {%d,%d,%d,%d}",
            l->compositionType, l->flags, l->handle, l->transform, l->blending,
            l->sourceCrop.left,
            l->sourceCrop.top,
            l->sourceCrop.right,
            l->sourceCrop.bottom,
            l->displayFrame.left,
            l->displayFrame.top,
            l->displayFrame.right,
            l->displayFrame.bottom);
}

static void hwc_enableHDMIOutput(hwc_composer_device_t *dev, bool enable) {
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(ctx) {
        ctx->hdmiConnected = enable;
    }
}

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list) {

    int yuvBufferCount = 0;

    if (list && (list->flags & HWC_GEOMETRY_CHANGED)) {
        for (size_t i=0 ; i<list->numHwLayers; i++) {
            private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
            if(hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO)) {
                yuvBufferCount++;
                if (yuvBufferCount > 1) {
                    break;
                }
            }
        }

        for (size_t i=0 ; i<list->numHwLayers ; i++) {
            private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
            // If there is a single Fullscreen layer, we can bypass it - TBD
            // If there is only one video/camera buffer, we can bypass itn
            if(hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO) && (yuvBufferCount == 1)) {
                list->hwLayers[i].compositionType = OVERLAY;
                list->hwLayers[i].hints |= HWC_HINT_CLEAR_FB;
            } else {
                // For other layers, check composition used. - C2D/MDP composition - TBD
                list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
            }
        }
    }

    return 0;
}

static int drawLayerUsingOverlay(hwc_context_t *ctx, hwc_layer_t *layer)
{
     int ret = 0;
     if (ctx && ctx->mOverlayLibObject) {
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;

        ret = ovLibObject->setSource(hnd->width, hnd->height, hnd->format, layer->transform, false);
        if (!ret) {
            LOGE("drawLayerUsingOverlay setSource failed");
            return -1;
        }

        hwc_rect_t sourceCrop = layer->sourceCrop;
        ret = ovLibObject->setCrop(sourceCrop.left, sourceCrop.top,
                                  (sourceCrop.right - sourceCrop.left),
                                  (sourceCrop.bottom-sourceCrop.top));
        if (!ret) {
            LOGE("drawLayerUsingOverlay setCrop failed");
            return -1;
        }

        hwc_rect_t displayFrame = layer->displayFrame;
        ret = ovLibObject->setPosition(displayFrame.left, displayFrame.top,
                                       (displayFrame.right - displayFrame.left),
                                       (displayFrame.bottom-displayFrame.top));
        if (!ret) {
            LOGE("drawLayerUsingOverlay setPosition failed");
            return -1;
        }

        int orientation;
        ovLibObject->getOrientation(orientation);
        if (orientation != layer->transform)
            ret = ovLibObject->setParameter(OVERLAY_TRANSFORM, layer->transform);
        if (!ret) {
            LOGE("drawLayerUsingOverlay setParameter failed transform %x", layer->transform);
            return -1;
        }

        ret = ovLibObject->queueBuffer(hnd);
        if (!ret) {
            LOGE("drawLayerUsingOverlay queueBuffer failed");
            return -1;
        }
        return 0;
    }
    return -1;
}

static int hwc_set(hwc_composer_device_t *dev,
        hwc_display_t dpy,
        hwc_surface_t sur,
        hwc_layer_list_t* list)
{
    
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
         LOGE("hwc_set null context ");
         return -1;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    if(!hwcModule) {
        LOGE("hwc_set null module ");
        return -1;
    }

    for (size_t i=0 ; i<list->numHwLayers ; i++) {
        if (list->hwLayers[i].compositionType == HWC_OVERLAY) {
            drawLayerUsingOverlay(ctx, &(list->hwLayers[i]));
        } else if ((hwcModule->compositionType == COMPOSITION_TYPE_C2D) ||
                (hwcModule->compositionType == COMPOSITION_TYPE_MDP)) {
            // TBD
        }
    }
    EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
    if (!sucess) {
        return HWC_EGL_ERROR;
    }

    return 0;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
            ctx->device.common.module);

    // Close the overlay and copybit modules
    if(hwcModule->copybitEngine) {
        copybit_close(hwcModule->copybitEngine);
        hwcModule->copybitEngine = NULL;
    }
    if(hwcModule->overlayEngine) {
        overlay_control_close(hwcModule->overlayEngine);
        hwcModule->overlayEngine = NULL;
    }

    if (ctx) {
         delete ctx->mOverlayLibObject;
         ctx->mOverlayLibObject = NULL;
        free(ctx);
    }
    return 0;
}

/*****************************************************************************/
static int hwc_module_initialize(struct private_hwc_module_t* hwcModule)
{

    // Open the overlay and copybit modules
    hw_module_t const *module;

    if (hw_get_module(COPYBIT_HARDWARE_MODULE_ID, &module) == 0) {
        copybit_open(module, &(hwcModule->copybitEngine));
    }
    if (hw_get_module(OVERLAY_HARDWARE_MODULE_ID, &module) == 0) {
        overlay_control_open(module, &(hwcModule->overlayEngine));
    }

    // get the current composition type
    char property[PROPERTY_VALUE_MAX];
    if (property_get("debug.sf.hw", property, NULL) > 0) {
        if(atoi(property) == 0) {
            //debug.sf.hw = 0
            hwcModule->compositionType = COMPOSITION_TYPE_CPU;
        } else { //debug.sf.hw = 1
            // Get the composition type
            property_get("debug.composition.type", property, NULL);
            if (property == NULL) {
                hwcModule->compositionType == COMPOSITION_TYPE_GPU;
            } else if ((strncmp(property, "mdp", 3)) == 0) {
                hwcModule->compositionType == COMPOSITION_TYPE_MDP;
            } else if ((strncmp(property, "c2d", 3)) == 0) {
                hwcModule->compositionType == COMPOSITION_TYPE_C2D;
            } else {
                hwcModule->compositionType == COMPOSITION_TYPE_GPU;
            }

            if(!hwcModule->copybitEngine)
                hwcModule->compositionType == COMPOSITION_TYPE_GPU;
            }
    } else { //debug.sf.hw is not set. Use cpu composition
        hwcModule->compositionType = COMPOSITION_TYPE_CPU;
    }

    return 0;
}


static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;
    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
	 private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>
                                        (const_cast<hw_module_t*>(module));
        
	hwc_module_initialize(hwcModule);
        struct hwc_context_t *dev;
        dev = (hwc_context_t*)malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));
	if(hwcModule->overlayEngine) {
		dev->mOverlayLibObject = new overlay::Overlay();
        } else
            dev->mOverlayLibObject = NULL;

        dev->hdmiConnected = false;

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = hwc_device_close;

        dev->device.prepare = hwc_prepare;
        dev->device.set = hwc_set;
        dev->device.enableHDMIOutput = hwc_enableHDMIOutput;
        *device = &dev->device.common;

        status = 0;
    }
    return status;
}
