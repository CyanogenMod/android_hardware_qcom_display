/*
 * Copyright (C) 2010 The Android Open Source Project
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
#include <EGL/eglext.h>
#include <ui/android_native_buffer.h>
#include <gralloc_priv.h>

/*****************************************************************************/
#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))
#define LIKELY( exp )       (__builtin_expect( (exp) != 0, true  ))
#define UNLIKELY( exp )     (__builtin_expect( (exp) != 0, false ))

// Enum containing the supported composition types
enum {
    COMPOSITION_TYPE_GPU = 0,
    COMPOSITION_TYPE_MDP = 0x1,
    COMPOSITION_TYPE_C2D = 0x2,
    COMPOSITION_TYPE_CPU = 0x4
};

enum HWCCompositionType {
    HWC_USE_GPU,     // This layer is to be handled by Surfaceflinger
    HWC_USE_OVERLAY, // This layer is to be handled by the overlay
    HWC_USE_COPYBIT  // This layer is to be handled by copybit
};

enum HWCPrivateFlags {
    HWC_USE_ORIGINAL_RESOLUTION = HWC_FLAGS_PRIVATE_0, // This layer is to be drawn using overlays
    HWC_DO_NOT_USE_OVERLAY      = HWC_FLAGS_PRIVATE_1, // Do not use overlays to draw this layer
};

enum HWCLayerType{
    HWC_SINGLE_VIDEO           = 0x1,
    HWC_ORIG_RESOLUTION        = 0x2,
    HWC_S3D_LAYER              = 0x4,
    HWC_STOP_UI_MIRRORING_MASK = 0xF
};

struct hwc_context_t {
    hwc_composer_device_t device;
    /* our private state goes below here */
    overlay::Overlay* mOverlayLibObject;
    int previousLayerCount;
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
    framebuffer_device_t *fbDevice;
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
   fbDevice: NULL,
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
#if defined HDMI_DUAL_DISPLAY
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    framebuffer_device_t *fbDev = hwcModule->fbDevice;
    if (fbDev) {
        fbDev->enableHDMIOutput(fbDev, enable);
    }

    if(ctx && ctx->mOverlayLibObject) {
        overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
        ovLibObject->setHDMIStatus(enable);

        if (!enable) {
            // Close the overlay channels if HDMI is disconnected
            ovLibObject->closeChannel();
            // Inform the gralloc that video mirroring is stopped
            framebuffer_device_t *fbDev = hwcModule->fbDevice;
            if (fbDev)
                fbDev->videoOverlayStarted(fbDev, false);
        }
    }
#endif
}

static int hwc_updateOverlayStatus(hwc_context_t* ctx, int layerType) {
#if defined HDMI_DUAL_DISPLAY
    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           ctx->device.common.module);
    overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
    if(!hwcModule || !ovLibObject) {
        LOGE("hwc_set_hdmi_status invalid params");
        return -1;
    }

    framebuffer_device_t *fbDev = hwcModule->fbDevice;
    if (!fbDev) {
        LOGE("hwc_set_hdmi_status fbDev is NULL");
        return -1;
    }

    if ((layerType & HWC_STOP_UI_MIRRORING_MASK) &&
        (OVERLAY_CHANNEL_DOWN == ovLibObject->getChannelStatus())) {
        // Inform the gralloc to stop UI mirroring
        fbDev->videoOverlayStarted(fbDev, true);
    }

    if ((OVERLAY_CHANNEL_UP == ovLibObject->getChannelStatus()) &&
        !(layerType & HWC_STOP_UI_MIRRORING_MASK)) {
        // Video mirroring is going on, and we do not have any layers to
        // mirror directly. Close the current video channel and inform the
        // gralloc to start UI mirroring
        ovLibObject->closeChannel();
        fbDev->videoOverlayStarted(fbDev, false);
    }
#endif
    return 0;
}

/*
 * Configures mdp pipes
 */
static int prepareOverlay(hwc_context_t *ctx, hwc_layer_t *layer) {
     int ret = 0;
     if (LIKELY(ctx && ctx->mOverlayLibObject)) {
        private_hwc_module_t* hwcModule =
            reinterpret_cast<private_hwc_module_t*>(ctx->device.common.module);
        if (UNLIKELY(!hwcModule)) {
            LOGE("prepareOverlay null module ");
            return -1;
        }

        private_handle_t *hnd = (private_handle_t *)layer->handle;
        overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
        int orientation = 0;
        overlay_buffer_info info;
        info.width = hnd->width;
        info.height = hnd->height;
        info.format = hnd->format;
        info.size = hnd->size;

        if (OVERLAY_CHANNEL_UP == ovLibObject->getChannelStatus())
            ovLibObject->getOrientation(orientation);

        if ((OVERLAY_CHANNEL_DOWN == ovLibObject->getChannelStatus())
            || (layer->transform != orientation) ||
            (hnd->flags & private_handle_t::PRIV_FLAGS_FORMAT_CHANGED)) {
            // Overlay channel is not started, or we have an orientation change
            // or there is a format change, call setSource to open the overlay
            // if necessary
            ret = ovLibObject->setSource(info, layer->transform,
                            (ovLibObject->getHDMIStatus()?true:false), false);
            if (!ret) {
                LOGE("prepareOverlay setSource failed");
                return -1;
            }
            // Reset this flag so that we don't keep opening and closing channels
            // unnecessarily
            hnd->flags &= ~private_handle_t::PRIV_FLAGS_FORMAT_CHANGED;
        } else {
            // The overlay goemetry may have changed, we only need to update the
            // overlay
            ret = ovLibObject->updateOverlaySource(info, layer->transform);
            if (!ret) {
                LOGE("prepareOverlay updateOverlaySource failed");
                return -1;
            }
        }

        hwc_rect_t sourceCrop = layer->sourceCrop;
        ret = ovLibObject->setCrop(sourceCrop.left, sourceCrop.top,
                                  (sourceCrop.right - sourceCrop.left),
                                  (sourceCrop.bottom - sourceCrop.top));
        if (!ret) {
            LOGE("prepareOverlay setCrop failed");
            return -1;
        }

        if (layer->flags == HWC_USE_ORIGINAL_RESOLUTION) {
            framebuffer_device_t* fbDev = hwcModule->fbDevice;
            ret = ovLibObject->setPosition(0, 0,
                                           fbDev->width, fbDev->height);
        } else {
            hwc_rect_t displayFrame = layer->displayFrame;
            ret = ovLibObject->setPosition(displayFrame.left, displayFrame.top,
                                    (displayFrame.right - displayFrame.left),
                                    (displayFrame.bottom - displayFrame.top));
        }
        if (!ret) {
            LOGE("prepareOverlay setPosition failed");
            return -1;
        }

        ovLibObject->getOrientation(orientation);
        if (orientation != layer->transform)
            ret = ovLibObject->setParameter(OVERLAY_TRANSFORM, layer->transform);
        if (!ret) {
            LOGE("prepareOverlay setParameter failed transform %x",
                    layer->transform);
            return -1;
        }
     }
     return 0;
}

bool canSkipComposition(hwc_context_t* ctx, int yuvBufferCount, int currentLayerCount,
                        int numLayersNotUpdating)
{
    if (!ctx) {
        LOGE("canSkipComposition invalid context");
        return false;
    }

    bool compCountChanged = false;
    if (yuvBufferCount == 1) {
        if (currentLayerCount != ctx->previousLayerCount) {
            compCountChanged = true;
            ctx->previousLayerCount = currentLayerCount;
        }

        if (!compCountChanged) {
            if ((currentLayerCount == 1) ||
                ((currentLayerCount-1) == numLayersNotUpdating)) {
                // We either have only one overlay layer or we have
                // all the non-UI layers not updating. In this case
                // we can skip the composition of the UI layers.
                return true;
            }
        }
    } else {
        ctx->previousLayerCount = -1;
    }
    return false;
}

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list) {

    hwc_context_t* ctx = (hwc_context_t*)(dev);

    if(!ctx || !list) {
         LOGE("hwc_prepare invalid context or list");
         return -1;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);

    if(!hwcModule) {
        LOGE("hwc_prepare null module ");
        return -1;
    }

    int yuvBufferCount = 0;
    int layerType = 0;
    int numLayersNotUpdating = 0;
    if (list) {
        for (size_t i=0 ; i<list->numHwLayers; i++) {
            private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
            if(hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO) &&
               !(list->hwLayers[i].flags & HWC_DO_NOT_USE_OVERLAY)) {
                yuvBufferCount++;
                if (yuvBufferCount > 1) {
                    break;
                }
            } else if (list->hwLayers[i].flags & HWC_LAYER_NOT_UPDATING) {
                numLayersNotUpdating++;
            }
        }

        for (size_t i=0 ; i<list->numHwLayers ; i++) {
            private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
            // If there is a single Fullscreen layer, we can bypass it - TBD
            // If there is only one video/camera buffer, we can bypass itn
            if (hnd && (hnd->bufferType == BUFFER_TYPE_VIDEO) && (yuvBufferCount == 1)) {
                if(prepareOverlay(ctx, &(list->hwLayers[i])) == 0) {
                    list->hwLayers[i].compositionType = HWC_USE_OVERLAY;
                    list->hwLayers[i].hints |= HWC_HINT_CLEAR_FB;
                } else if (hwcModule->compositionType & (COMPOSITION_TYPE_C2D)) {
                    //Fail safe path: If drawing with overlay fails,
                    //Use C2D if available.
                    list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
                    yuvBufferCount = 0;
                } else {
                    //If C2D is not enabled fall back to GPU.
                    list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
                    yuvBufferCount = 0;
                }
            } else if (list->hwLayers[i].flags == HWC_USE_ORIGINAL_RESOLUTION) {
                list->hwLayers[i].compositionType = HWC_USE_OVERLAY;
                list->hwLayers[i].hints |= HWC_HINT_CLEAR_FB;
                layerType |= HWC_ORIG_RESOLUTION;
            } else if (hnd && (hwcModule->compositionType & (COMPOSITION_TYPE_C2D|COMPOSITION_TYPE_MDP))) {
                list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
            } else {
                list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
            }
        }

        if (canSkipComposition(ctx, yuvBufferCount, list->numHwLayers, numLayersNotUpdating)) {
            list->flags |= HWC_SKIP_COMPOSITION;
        } else {
            list->flags &= ~HWC_SKIP_COMPOSITION;
        }

        if (list->flags & HWC_GEOMETRY_CHANGED) {
            layerType |= (yuvBufferCount == 1) ? HWC_SINGLE_VIDEO: 0;
            // Inform the gralloc of the current HDMI status
            hwc_updateOverlayStatus(ctx, layerType);
        }
    }

    return 0;
}
// ---------------------------------------------------------------------------
struct range {
    int current;
    int end;
};
struct region_iterator : public copybit_region_t {
    
    region_iterator(hwc_region_t region) {
        mRegion = region;
        r.end = region.numRects;
        r.current = 0;
        this->next = iterate;
    }

private:
    static int iterate(copybit_region_t const * self, copybit_rect_t* rect) {
        if (!self || !rect) {
            LOGE("iterate invalid parameters");
            return 0;
        }

        region_iterator const* me = static_cast<region_iterator const*>(self);
        if (me->r.current != me->r.end) {
            rect->l = me->mRegion.rects[me->r.current].left;
            rect->t = me->mRegion.rects[me->r.current].top;
            rect->r = me->mRegion.rects[me->r.current].right;
            rect->b = me->mRegion.rects[me->r.current].bottom;
            me->r.current++;
            return 1;
        }
        return 0;
    }
    
    hwc_region_t mRegion;
    mutable range r; 
};


static int drawLayerUsingCopybit(hwc_composer_device_t *dev, hwc_layer_t *layer, EGLDisplay dpy,
                                 EGLSurface surface)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
         LOGE("drawLayerUsingCopybit null context ");
         return -1;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(dev->common.module);
    if(!hwcModule) {
        LOGE("drawLayerUsingCopybit null module ");
        return -1;
    }

    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(!hnd) {
        LOGE("drawLayerUsingCopybit invalid handle");
        return -1;
    }

    // Set the copybit source:
    copybit_image_t src;
    src.w = ALIGN(hnd->width, 32);
    src.h = hnd->height;
    src.format = hnd->format;
    src.base = (void *)hnd->base;
    src.handle = (native_handle_t *)layer->handle;

    // Copybit source rect
    hwc_rect_t sourceCrop = layer->sourceCrop;
    copybit_rect_t srcRect = {sourceCrop.left, sourceCrop.top,
                              sourceCrop.right,
                              sourceCrop.bottom};

    // Copybit destination rect
    hwc_rect_t displayFrame = layer->displayFrame;
    copybit_rect_t dstRect = {displayFrame.left, displayFrame.top,
                              displayFrame.right,
                              displayFrame.bottom};

    // Copybit dst
    copybit_image_t dst;
    android_native_buffer_t *renderBuffer = (android_native_buffer_t *)eglGetRenderBufferANDROID(dpy, surface);
    if (!renderBuffer) {
        LOGE("eglGetRenderBufferANDROID returned NULL buffer");
        return -1;
    }
    private_handle_t *fbHandle = (private_handle_t *)renderBuffer->handle;
    if(!fbHandle) {
        LOGE("Framebuffer handle is NULL");
        return -1;
    }
    dst.w = ALIGN(fbHandle->width,32);
    dst.h = fbHandle->height;
    dst.format = fbHandle->format;
    dst.base = (void *)fbHandle->base;
    dst.handle = (native_handle_t *)renderBuffer->handle;

    // Copybit region
    hwc_region_t region = layer->visibleRegionScreen;
    region_iterator copybitRegion(region);

    copybit_device_t *copybit = hwcModule->copybitEngine;
    copybit->set_parameter(copybit, COPYBIT_TRANSFORM, layer->transform);
    copybit->set_parameter(copybit, COPYBIT_PLANE_ALPHA,
                           (layer->blending == HWC_BLENDING_NONE) ? 0xFF : layer->alpha);
    copybit->set_parameter(copybit, COPYBIT_PREMULTIPLIED_ALPHA,
                           (layer->blending == HWC_BLENDING_PREMULT)? COPYBIT_ENABLE : COPYBIT_DISABLE);
    int err = copybit->stretch(copybit, &dst, &src, &dstRect, &srcRect, &copybitRegion);

    if(err < 0)
        LOGE("copybit stretch failed");

    return err;
}

static int drawLayerUsingOverlay(hwc_context_t *ctx, hwc_layer_t *layer)
{
    if (ctx && ctx->mOverlayLibObject) {
        private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(ctx->device.common.module);
        if (!hwcModule) {
            LOGE("drawLayerUsingLayer null module ");
            return -1;
        }
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        overlay::Overlay *ovLibObject = ctx->mOverlayLibObject;
        int ret = 0;

        ret = ovLibObject->queueBuffer(hnd);
        if (!ret) {
            LOGE("drawLayerUsingOverlay queueBuffer failed");
            return -1;
        }
    }
    return 0;
}

static int hwc_set(hwc_composer_device_t *dev,
        hwc_display_t dpy,
        hwc_surface_t sur,
        hwc_layer_list_t* list)
{
    
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx || !list) {
         LOGE("hwc_set invalid context or list");
         return -1;
    }

    private_hwc_module_t* hwcModule = reinterpret_cast<private_hwc_module_t*>(
                                                           dev->common.module);
    if(!hwcModule) {
        LOGE("hwc_set null module ");
        return -1;
    }
    for (size_t i=0; i<list->numHwLayers; i++) {
        if (list->hwLayers[i].flags == HWC_SKIP_LAYER) {
            continue;
        }
        if (list->hwLayers[i].compositionType == HWC_USE_OVERLAY) {
            drawLayerUsingOverlay(ctx, &(list->hwLayers[i]));
        } else if (list->flags & HWC_SKIP_COMPOSITION) {
            break;
        } else if (list->hwLayers[i].compositionType == HWC_USE_COPYBIT) {
            drawLayerUsingCopybit(dev, &(list->hwLayers[i]), (EGLDisplay)dpy, (EGLSurface)sur);
        }
    }

    // Do not call eglSwapBuffers if we the skip composition flag is set on the list.
    if (!(list->flags & HWC_SKIP_COMPOSITION)) {
        EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
        if (!sucess) {
            return HWC_EGL_ERROR;
        }
    }
    return 0;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    if(!dev) {
        LOGE("hwc_device_close null device pointer");
        return -1;
    }

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
    if(hwcModule->fbDevice) {
        framebuffer_close(hwcModule->fbDevice);
        hwcModule->fbDevice = NULL;
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
    if (hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0) {
        framebuffer_open(module, &(hwcModule->fbDevice));
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
                hwcModule->compositionType = COMPOSITION_TYPE_GPU;
            } else if ((strncmp(property, "mdp", 3)) == 0) {
                hwcModule->compositionType = COMPOSITION_TYPE_MDP;
            } else if ((strncmp(property, "c2d", 3)) == 0) {
                hwcModule->compositionType = COMPOSITION_TYPE_C2D;
            } else {
                hwcModule->compositionType = COMPOSITION_TYPE_GPU;
            }

            if(!hwcModule->copybitEngine)
                hwcModule->compositionType = COMPOSITION_TYPE_GPU;
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
