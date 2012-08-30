/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
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

#define DEBUG_COPYBIT 0
#include <copybit.h>
#include <genlock.h>
#include "hwc_copybit.h"
#include "hwc_copybit.h"
#include "comptype.h"

namespace qhwc {


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
    static int iterate(copybit_region_t const * self, copybit_rect_t* rect){
        if (!self || !rect) {
            ALOGE("iterate invalid parameters");
            return 0;
        }

        region_iterator const* me =
                                  static_cast<region_iterator const*>(self);
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

// Initialize CopyBit Class Static Mmembers.
functype_eglGetRenderBufferANDROID CopyBit::LINK_eglGetRenderBufferANDROID
                                                                   = NULL;
functype_eglGetCurrentSurface CopyBit::LINK_eglGetCurrentSurface = NULL;
int CopyBit::sYuvCount = 0;
int CopyBit::sYuvLayerIndex = -1;
bool CopyBit::sIsModeOn = false;
bool CopyBit::sIsLayerSkip = false;
void* CopyBit::egl_lib = NULL;

void CopyBit::openEglLibAndGethandle()
{
    egl_lib = ::dlopen("libEGL_adreno200.so", RTLD_GLOBAL | RTLD_LAZY);
    if (!egl_lib) {
        return;
    }
    updateEglHandles(egl_lib);
}
void CopyBit::closeEglLib()
{
    if(egl_lib)
        ::dlclose(egl_lib);

    egl_lib = NULL;
    updateEglHandles(NULL);
}

void CopyBit::updateEglHandles(void* egl_lib)
{
    if(egl_lib != NULL) {
        *(void **)&CopyBit::LINK_eglGetRenderBufferANDROID =
                             ::dlsym(egl_lib, "eglGetRenderBufferANDROID");
        *(void **)&CopyBit::LINK_eglGetCurrentSurface =
                                  ::dlsym(egl_lib, "eglGetCurrentSurface");
   }else {
        LINK_eglGetCurrentSurface = NULL;
        LINK_eglGetCurrentSurface = NULL;
   }
}

bool CopyBit::canUseCopybitForYUV(hwc_context_t *ctx) {
    // return true for non-overlay targets
    if(ctx->mMDP.hasOverlay) {
       return false;
    }
    return true;
}

bool CopyBit::canUseCopybitForRGB(hwc_context_t *ctx, hwc_display_contents_1_t *list) {
    int compositionType =
        qdutils::QCCompositionType::getInstance().getCompositionType();

    if ((compositionType & qdutils::COMPOSITION_TYPE_C2D) ||
        (compositionType & qdutils::COMPOSITION_TYPE_DYN)) {
         if (sYuvCount) {
             //Overlay up & running. Dont use COPYBIT for RGB layers.
             // TODO need to implement blending with C2D
             return false;
         }
    }

    if (compositionType & qdutils::COMPOSITION_TYPE_DYN) {
        // DYN Composition:
        // use copybit, if (TotalRGBRenderArea < 2 * FB Area)
        // this is done based on perf inputs in ICS
        // TODO: Above condition needs to be re-evaluated in JB

        framebuffer_device_t *fbDev = ctx->mFbDev;
        if (!fbDev) {
            ALOGE("%s:Invalid FB device", __FUNCTION__);
            return false;
        }
        unsigned int fbArea = (fbDev->width * fbDev->height);
        unsigned int renderArea = getRGBRenderingArea(list);
            ALOGD_IF (DEBUG_COPYBIT, "%s:renderArea %u, fbArea %u",
                                  __FUNCTION__, renderArea, fbArea);
        if (renderArea < (2 * fbArea)) {
            return true;
        }
    } else if ((compositionType & qdutils::COMPOSITION_TYPE_MDP)) {
      // MDP composition, use COPYBIT always
      return true;
    } else if ((compositionType & qdutils::COMPOSITION_TYPE_C2D)) {
      // C2D composition, use COPYBIT
      return true;
    }
    return false;
}

unsigned int CopyBit::getRGBRenderingArea(const hwc_display_contents_1_t *list) {
    //Calculates total rendering area for RGB layers
    unsigned int renderArea = 0;
    unsigned int w=0, h=0;
    for (unsigned int i=0; i<list->numHwLayers; i++) {
         private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
         if (hnd) {
             if (BUFFER_TYPE_UI == hnd->bufferType) {
                 getLayerResolution(&list->hwLayers[i], w, h);
                 renderArea += (w*h);
             }
         }
    }
    return renderArea;
}

bool CopyBit::prepare(hwc_context_t *ctx, hwc_display_contents_1_t *list) {

    int compositionType =
        qdutils::QCCompositionType::getInstance().getCompositionType();

    if ((compositionType & qdutils::COMPOSITION_TYPE_GPU) ||
        (compositionType & qdutils::COMPOSITION_TYPE_CPU))   {
        //GPU/CPU composition, don't change layer composition type
        return true;
    }

    bool useCopybitForYUV = canUseCopybitForYUV(ctx);
    bool useCopybitForRGB = canUseCopybitForRGB(ctx, list);

    if(!(validateParams(ctx, list))) {
       ALOGE("%s:Invalid Params", __FUNCTION__);
       return false;
    }

    for (int i=list->numHwLayers-1; i >= 0 ; i--) {
        private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;

        if (isSkipLayer(&list->hwLayers[i])) {
            return true;
        } else if (hnd->bufferType == BUFFER_TYPE_VIDEO) {
          //YUV layer, check, if copybit can be used
          if (useCopybitForYUV) {
              list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
          }
       } else if (hnd->bufferType == BUFFER_TYPE_UI) {
          //RGB layer, check, if copybit can be used
          if (useCopybitForRGB) {
              list->hwLayers[i].compositionType = HWC_USE_COPYBIT;
          }
       }
    }
    return true;
}

bool CopyBit::draw(hwc_context_t *ctx, hwc_display_contents_1_t *list, EGLDisplay dpy,
                                                               EGLSurface sur){
    // draw layers marked for COPYBIT
    int retVal = true;
    for (size_t i=0; i<list->numHwLayers; i++) {
        if (list->hwLayers[i].compositionType == HWC_USE_COPYBIT) {
            retVal = drawLayerUsingCopybit(ctx, &(list->hwLayers[i]),
                                                     (EGLDisplay)dpy,
                                                     (EGLSurface)sur,
                                      LINK_eglGetRenderBufferANDROID,
                                          LINK_eglGetCurrentSurface);
           if(retVal<0) {
              ALOGE("%s : drawLayerUsingCopybit failed", __FUNCTION__);
           }
        }
    }
    return true;
}

int  CopyBit::drawLayerUsingCopybit(hwc_context_t *dev, hwc_layer_1_t *layer,
                                                            EGLDisplay dpy,
                                                        EGLSurface surface,
        functype_eglGetRenderBufferANDROID& LINK_eglGetRenderBufferANDROID,
                    functype_eglGetCurrentSurface LINK_eglGetCurrentSurface)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    if(!ctx) {
         ALOGE("%s: null context ", __FUNCTION__);
         return -1;
    }

    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(!hnd) {
        ALOGE("%s: invalid handle", __FUNCTION__);
        return -1;
    }

    // Lock this buffer for read.
    genlock_lock_type lockType = GENLOCK_READ_LOCK;
    int err = genlock_lock_buffer(hnd, lockType, GENLOCK_MAX_TIMEOUT);
    if (GENLOCK_FAILURE == err) {
        ALOGE("%s: genlock_lock_buffer(READ) failed", __FUNCTION__);
        return -1;
    }
    //render buffer
    EGLSurface eglSurface = LINK_eglGetCurrentSurface(EGL_DRAW);
    android_native_buffer_t *renderBuffer =
     (android_native_buffer_t *)LINK_eglGetRenderBufferANDROID(dpy, eglSurface);
    if (!renderBuffer) {
        ALOGE("%s: eglGetRenderBuffer returned NULL buffer", __FUNCTION__);
        genlock_unlock_buffer(hnd);
        return -1;
    }
    private_handle_t *fbHandle = (private_handle_t *)renderBuffer->handle;
    if(!fbHandle) {
        ALOGE("%s: Framebuffer handle is NULL", __FUNCTION__);
        genlock_unlock_buffer(hnd);
        return -1;
    }

    // Set the copybit source:
    copybit_image_t src;
    src.w = hnd->width;
    src.h = hnd->height;
    src.format = hnd->format;
    src.base = (void *)hnd->base;
    src.handle = (native_handle_t *)layer->handle;
    src.horiz_padding = src.w - hnd->width;
    // Initialize vertical padding to zero for now,
    // this needs to change to accomodate vertical stride
    // if needed in the future
    src.vert_padding = 0;
    // Remove the srcBufferTransform if any
    layer->transform = (layer->transform & FINAL_TRANSFORM_MASK);

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
    dst.w = ALIGN(fbHandle->width,32);
    dst.h = fbHandle->height;
    dst.format = fbHandle->format;
    dst.base = (void *)fbHandle->base;
    dst.handle = (native_handle_t *)renderBuffer->handle;

    copybit_device_t *copybit = ctx->mCopybitEngine->getEngine();

    int32_t screen_w        = displayFrame.right - displayFrame.left;
    int32_t screen_h        = displayFrame.bottom - displayFrame.top;
    int32_t src_crop_width  = sourceCrop.right - sourceCrop.left;
    int32_t src_crop_height = sourceCrop.bottom -sourceCrop.top;

    // Copybit dst
    float copybitsMaxScale =
                      (float)copybit->get(copybit,COPYBIT_MAGNIFICATION_LIMIT);
    float copybitsMinScale =
                       (float)copybit->get(copybit,COPYBIT_MINIFICATION_LIMIT);

    if((layer->transform == HWC_TRANSFORM_ROT_90) ||
                           (layer->transform == HWC_TRANSFORM_ROT_270)) {
        //swap screen width and height
        int tmp = screen_w;
        screen_w  = screen_h;
        screen_h = tmp;
    }
    private_handle_t *tmpHnd = NULL;

    if(screen_w <=0 || screen_h<=0 ||src_crop_width<=0 || src_crop_height<=0 ) {
        ALOGE("%s: wrong params for display screen_w=%d src_crop_width=%d \
        screen_w=%d src_crop_width=%d", __FUNCTION__, screen_w,
                                src_crop_width,screen_w,src_crop_width);
        genlock_unlock_buffer(hnd);
        return -1;
    }

    float dsdx = (float)screen_w/src_crop_width;
    float dtdy = (float)screen_h/src_crop_height;

    float scaleLimitMax = copybitsMaxScale * copybitsMaxScale;
    float scaleLimitMin = copybitsMinScale * copybitsMinScale;
    if(dsdx > scaleLimitMax ||
        dtdy > scaleLimitMax ||
        dsdx < 1/scaleLimitMin ||
        dtdy < 1/scaleLimitMin) {
        ALOGE("%s: greater than max supported size dsdx=%f dtdy=%f \
              scaleLimitMax=%f scaleLimitMin=%f", __FUNCTION__,dsdx,dtdy,
                                          scaleLimitMax,1/scaleLimitMin);
        genlock_unlock_buffer(hnd);
        return -1;
    }
    if(dsdx > copybitsMaxScale ||
        dtdy > copybitsMaxScale ||
        dsdx < 1/copybitsMinScale ||
        dtdy < 1/copybitsMinScale){
        // The requested scale is out of the range the hardware
        // can support.
       ALOGE("%s:%d::Need to scale twice dsdx=%f, dtdy=%f,copybitsMaxScale=%f,\
                                 copybitsMinScale=%f,screen_w=%d,screen_h=%d \
                  src_crop_width=%d src_crop_height=%d",__FUNCTION__,__LINE__,
              dsdx,dtdy,copybitsMaxScale,1/copybitsMinScale,screen_w,screen_h,
                                              src_crop_width,src_crop_height);

       //Driver makes width and height as even
       //that may cause wrong calculation of the ratio
       //in display and crop.Hence we make
       //crop width and height as even.
       src_crop_width  = (src_crop_width/2)*2;
       src_crop_height = (src_crop_height/2)*2;

       int tmp_w =  src_crop_width;
       int tmp_h =  src_crop_height;

       if (dsdx > copybitsMaxScale || dtdy > copybitsMaxScale ){
         tmp_w = src_crop_width*copybitsMaxScale;
         tmp_h = src_crop_height*copybitsMaxScale;
       }else if (dsdx < 1/copybitsMinScale ||dtdy < 1/copybitsMinScale ){
         tmp_w = src_crop_width/copybitsMinScale;
         tmp_h = src_crop_height/copybitsMinScale;
         tmp_w  = (tmp_w/2)*2;
         tmp_h = (tmp_h/2)*2;
       }
       ALOGE("%s:%d::tmp_w = %d,tmp_h = %d",__FUNCTION__,__LINE__,tmp_w,tmp_h);

       int usage = GRALLOC_USAGE_PRIVATE_MM_HEAP;

       if (0 == alloc_buffer(&tmpHnd, tmp_w, tmp_h, fbHandle->format, usage)){
            copybit_image_t tmp_dst;
            copybit_rect_t tmp_rect;
            tmp_dst.w = tmp_w;
            tmp_dst.h = tmp_h;
            tmp_dst.format = tmpHnd->format;
            tmp_dst.handle = tmpHnd;
            tmp_dst.horiz_padding = src.horiz_padding;
            tmp_dst.vert_padding = src.vert_padding;
            tmp_rect.l = 0;
            tmp_rect.t = 0;
            tmp_rect.r = tmp_dst.w;
            tmp_rect.b = tmp_dst.h;
            //create one clip region
            hwc_rect tmp_hwc_rect = {0,0,tmp_rect.r,tmp_rect.b};
            hwc_region_t tmp_hwc_reg = {1,(hwc_rect_t const*)&tmp_hwc_rect};
            region_iterator tmp_it(tmp_hwc_reg);
            copybit->set_parameter(copybit,COPYBIT_TRANSFORM,0);
            //TODO: once, we are able to read layer alpha, update this
            copybit->set_parameter(copybit, COPYBIT_PLANE_ALPHA, 255);
            err = copybit->stretch(copybit,&tmp_dst, &src, &tmp_rect,
                                                           &srcRect, &tmp_it);
            if(err < 0){
                ALOGE("%s:%d::tmp copybit stretch failed",__FUNCTION__,
                                                             __LINE__);
                if(tmpHnd)
                    free_buffer(tmpHnd);
                genlock_unlock_buffer(hnd);
                return err;
            }
            // copy new src and src rect crop
            src = tmp_dst;
            srcRect = tmp_rect;
      }
    }
    // Copybit region
    hwc_region_t region = layer->visibleRegionScreen;
    region_iterator copybitRegion(region);

    copybit->set_parameter(copybit, COPYBIT_FRAMEBUFFER_WIDTH,
                                          renderBuffer->width);
    copybit->set_parameter(copybit, COPYBIT_FRAMEBUFFER_HEIGHT,
                                          renderBuffer->height);
    copybit->set_parameter(copybit, COPYBIT_TRANSFORM,
                                              layer->transform);
    //TODO: once, we are able to read layer alpha, update this
    copybit->set_parameter(copybit, COPYBIT_PLANE_ALPHA, 255);
    copybit->set_parameter(copybit, COPYBIT_PREMULTIPLIED_ALPHA,
                      (layer->blending == HWC_BLENDING_PREMULT)?
                                             COPYBIT_ENABLE : COPYBIT_DISABLE);
    copybit->set_parameter(copybit, COPYBIT_DITHER,
                             (dst.format == HAL_PIXEL_FORMAT_RGB_565)?
                                             COPYBIT_ENABLE : COPYBIT_DISABLE);
    copybit->set_parameter(copybit, COPYBIT_BLIT_TO_FRAMEBUFFER,
                                                COPYBIT_ENABLE);
    err = copybit->stretch(copybit, &dst, &src, &dstRect, &srcRect,
                                                   &copybitRegion);
    copybit->set_parameter(copybit, COPYBIT_BLIT_TO_FRAMEBUFFER,
                                               COPYBIT_DISABLE);

    if(tmpHnd)
        free_buffer(tmpHnd);

    if(err < 0)
        ALOGE("%s: copybit stretch failed",__FUNCTION__);

    // Unlock this buffer since copybit is done with it.
    err = genlock_unlock_buffer(hnd);
    if (GENLOCK_FAILURE == err) {
        ALOGE("%s: genlock_unlock_buffer failed", __FUNCTION__);
    }

    return err;
}

void CopyBit::getLayerResolution(const hwc_layer_1_t* layer,
                                 unsigned int& width, unsigned int& height)
{
    hwc_rect_t displayFrame  = layer->displayFrame;

    width = displayFrame.right - displayFrame.left;
    height = displayFrame.bottom - displayFrame.top;
}

bool CopyBit::validateParams(hwc_context_t *ctx, const hwc_display_contents_1_t *list) {
   //Validate parameters
   if (!ctx) {
       ALOGE("%s:Invalid HWC context", __FUNCTION__);
       return false;
   } else if (!list) {
       ALOGE("%s:Invalid HWC layer list", __FUNCTION__);
       return false;
   }

   framebuffer_device_t *fbDev = ctx->mFbDev;

   if (!fbDev) {
       ALOGE("%s:Invalid FB device", __FUNCTION__);
       return false;
   }

   if (LINK_eglGetRenderBufferANDROID == NULL ||
            LINK_eglGetCurrentSurface == NULL) {
       ALOGE("%s:Not able to link to ADRENO", __FUNCTION__);
       return false;
   }

   return true;
}

//CopybitEngine Class functions
CopybitEngine* CopybitEngine::sInstance = 0;

struct copybit_device_t* CopybitEngine::getEngine() {
   return sEngine;
}
CopybitEngine* CopybitEngine::getInstance() {
   if(sInstance == NULL)
       sInstance = new CopybitEngine();
   return sInstance;
}

CopybitEngine::CopybitEngine(){
    hw_module_t const *module;
    if (hw_get_module(COPYBIT_HARDWARE_MODULE_ID, &module) == 0) {
        copybit_open(module, &sEngine);
        CopyBit::openEglLibAndGethandle();
    } else {
       ALOGE("FATAL ERROR: copybit open failed.");
    }
}

CopybitEngine::~CopybitEngine()
{
    if(sEngine)
    {
        CopyBit::closeEglLib();
        copybit_close(sEngine);
        sEngine = NULL;
    }
}

}; //namespace qhwc
