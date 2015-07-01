/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution.
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
#include <utils/Timers.h>
#include "hwc_copybit.h"
#include "comptype.h"
#include "mdp_version.h"
#include "gr.h"
#include "cb_utils.h"
#include "sync/sync.h"

using namespace qdutils;
#ifdef NO_IOMMU
#define HEAP_ID GRALLOC_USAGE_PRIVATE_UI_CONTIG_HEAP
#else
#define HEAP_ID GRALLOC_USAGE_PRIVATE_IOMMU_HEAP
#endif

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

void CopyBit::reset() {
    mIsModeOn = false;
    mCopyBitDraw = false;
}

bool CopyBit::canUseCopybitForYUV(hwc_context_t *ctx) {
    // return true for non-overlay targets
    if(ctx->mMDP.hasOverlay) {
       return false;
    }
    return true;
}

bool CopyBit::canUseCopybitForRGB(hwc_context_t *ctx,
                                        hwc_display_contents_1_t *list,
                                        int dpy) {
    int compositionType = qdutils::QCCompositionType::
                                    getInstance().getCompositionType();

    if (compositionType & qdutils::COMPOSITION_TYPE_DYN) {
        // DYN Composition:
        // use copybit, if (TotalRGBRenderArea < threashold * FB Area)
        // this is done based on perf inputs in ICS
        // TODO: Above condition needs to be re-evaluated in JB
        int fbWidth =  ctx->dpyAttr[dpy].xres;
        int fbHeight =  ctx->dpyAttr[dpy].yres;
        unsigned int fbArea = (fbWidth * fbHeight);
        unsigned int renderArea = getRGBRenderingArea(list);
            ALOGD_IF (DEBUG_COPYBIT, "%s:renderArea %u, fbArea %u",
                                  __FUNCTION__, renderArea, fbArea);
        if (renderArea < (mDynThreshold * fbArea)) {
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

unsigned int CopyBit::getRGBRenderingArea
                                    (const hwc_display_contents_1_t *list) {
    //Calculates total rendering area for RGB layers
    unsigned int renderArea = 0;
    unsigned int w=0, h=0;
    //Do not include Framebuffer area in calculating total area
    for (unsigned int i=0; i<(list->numHwLayers)-1; i++) {
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

bool CopyBit::prepare(hwc_context_t *ctx, hwc_display_contents_1_t *list,
                                                            int dpy) {

    if(mEngine == NULL) {
        // No copybit device found - cannot use copybit
        return false;
    }
    int compositionType = qdutils::QCCompositionType::
                                    getInstance().getCompositionType();

    if ((compositionType == qdutils::COMPOSITION_TYPE_GPU) ||
        (compositionType == qdutils::COMPOSITION_TYPE_CPU))   {
        //GPU/CPU composition, don't change layer composition type
        return true;
    }

    if(!(validateParams(ctx, list))) {
        ALOGE("%s:Invalid Params", __FUNCTION__);
        return false;
    }

    if(ctx->listStats[dpy].skipCount) {
        //GPU will be anyways used
        return false;
    }

    if (ctx->listStats[dpy].numAppLayers > MAX_NUM_APP_LAYERS) {
        // Reached max layers supported by HWC.
        return false;
    }

    bool useCopybitForYUV = canUseCopybitForYUV(ctx);
    bool useCopybitForRGB = canUseCopybitForRGB(ctx, list, dpy);
    LayerProp *layerProp = ctx->layerProp[dpy];
    size_t fbLayerIndex = ctx->listStats[dpy].fbLayerIndex;
    hwc_layer_1_t *fbLayer = &list->hwLayers[fbLayerIndex];


    //Allocate render buffers if they're not allocated
    if (ctx->mMDP.version > qdutils::MDP_V4_3 &&
            (useCopybitForYUV || useCopybitForRGB)) {
        int ret = allocRenderBuffers(mAlignedFBWidth,
                                     mAlignedFBHeight,
                                     HAL_PIXEL_FORMAT_RGBA_8888);
        if (ret < 0) {
            return false;
        } else {
            mCurRenderBufferIndex = (mCurRenderBufferIndex + 1) %
                NUM_RENDER_BUFFERS;
        }
    }


    // numAppLayers-1, as we iterate till 0th layer index
    for (int i = ctx->listStats[dpy].numAppLayers-1; i >= 0 ; i--) {
        private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;

        if((hnd->bufferType == BUFFER_TYPE_VIDEO) && (layerProp[i].mFlags & HWC_MDPCOMP))
            continue;

        hwc_layer_1_t* layer = &list->hwLayers[i];
        if (!(layer->planeAlpha < 0xFF) &&
            ((hnd->bufferType == BUFFER_TYPE_VIDEO && useCopybitForYUV) ||
            (hnd->bufferType == BUFFER_TYPE_UI && useCopybitForRGB))) {
            layerProp[i].mFlags |= HWC_COPYBIT;
            if (ctx->mMDP.version <= qdutils::MDP_V4_3)
                list->hwLayers[i].compositionType = HWC_BLIT;
            else
                list->hwLayers[i].compositionType = HWC_OVERLAY;
            mCopyBitDraw = true;
        } else {
            ALOGD_IF(DEBUG_COPYBIT,"%s:Can not do copybit, Resetting all the layers marked for it", __FUNCTION__);
            // We currently cannot mix copybit layers with layers marked to
            // be drawn on the framebuffer or that are on the layer cache.
            mCopyBitDraw = false;
            //Layer flag should be reset so that SF can compose it on FrameBuffer
            for(int j = ctx->listStats[dpy].numAppLayers-1; j > i; j--) {
                private_handle_t *hnd1 = (private_handle_t *)list->hwLayers[j].handle;
                if(hnd1->bufferType == BUFFER_TYPE_UI)
                    list->hwLayers[j].compositionType = HWC_FRAMEBUFFER;
            }
            break;
        }
    }
    return true;
}

int CopyBit::clear (private_handle_t* hnd, hwc_rect_t& rect)
{
    int ret = 0;
    copybit_rect_t clear_rect = {rect.left, rect.top,
        rect.right,
        rect.bottom};

    copybit_image_t buf;
    buf.w = ALIGN(getWidth(hnd),32);
    buf.h = getHeight(hnd);
    buf.format = hnd->format;
    buf.base = (void *)hnd->base;
    buf.handle = (native_handle_t *)hnd;

    copybit_device_t *copybit = mEngine;
    ret = copybit->clear(copybit, &buf, &clear_rect);
    return ret;
}

bool CopyBit::draw(hwc_context_t *ctx, hwc_display_contents_1_t *list,
                                                        int dpy, int32_t *fd) {
    // draw layers marked for COPYBIT
    int retVal = true;
    int copybitLayerCount = 0;
    uint32_t last = 0;
    LayerProp *layerProp = ctx->layerProp[dpy];
    private_handle_t *renderBuffer;

    if(mCopyBitDraw == false) // there is no layer marked for copybit
        return false ;

    //render buffer
    if (ctx->mMDP.version <= qdutils::MDP_V4_3) {
        last = list->numHwLayers - 1;
        renderBuffer = (private_handle_t *)list->hwLayers[last].handle;
    } else {
        renderBuffer = getCurrentRenderBuffer();
    }
    if (!renderBuffer) {
        ALOGE("%s: Render buffer layer handle is NULL", __FUNCTION__);
        return false;
    }

    if (ctx->mMDP.version > qdutils::MDP_V4_3) {
        //Wait for the previous frame to complete before rendering onto it
        if(mRelFd[mCurRenderBufferIndex] >=0) {
            sync_wait(mRelFd[mCurRenderBufferIndex], 1000);
            close(mRelFd[mCurRenderBufferIndex]);
            mRelFd[mCurRenderBufferIndex] = -1;
        }
    } else {
        if(list->hwLayers[last].acquireFenceFd >=0) {
            sync_wait(list->hwLayers[last].acquireFenceFd, 1000);
            close(list->hwLayers[last].acquireFenceFd);
            list->hwLayers[last].acquireFenceFd = -1;
        }
    }

    //Clear the transparent or left out region on the render buffer
    hwc_rect_t clearRegion = {0,0,0,0};
    if(CBUtils::getuiClearRegion(list, clearRegion, layerProp))
        clear(renderBuffer, clearRegion);
    // numAppLayers-1, as we iterate from 0th layer index with HWC_COPYBIT flag
    for (int i = 0; i <= (ctx->listStats[dpy].numAppLayers-1); i++) {
        hwc_layer_1_t *layer = &list->hwLayers[i];
        if(!(layerProp[i].mFlags & HWC_COPYBIT)) {
            ALOGD_IF(DEBUG_COPYBIT, "%s: Not Marked for copybit", __FUNCTION__);
            continue;
        }
        int ret = -1;
        if (list->hwLayers[i].acquireFenceFd != -1 ) {
            // Wait for acquire Fence on the App buffers.
            ret = sync_wait(list->hwLayers[i].acquireFenceFd, 1000);
            if(ret < 0) {
                ALOGE("%s: sync_wait error!! error no = %d err str = %s",
                                    __FUNCTION__, errno, strerror(errno));
            }
            close(list->hwLayers[i].acquireFenceFd);
            list->hwLayers[i].acquireFenceFd = -1;
        }
        retVal = drawLayerUsingCopybit(ctx, &(list->hwLayers[i]),
                                                    renderBuffer, dpy);
        copybitLayerCount++;
        if(retVal < 0) {
            ALOGE("%s : drawLayerUsingCopybit failed", __FUNCTION__);
        }
    }

    if (copybitLayerCount) {
        copybit_device_t *copybit = getCopyBitDevice();
        // Async mode
        copybit->flush_get_fence(copybit, fd);
    }
    return true;
}

int  CopyBit::drawLayerUsingCopybit(hwc_context_t *dev, hwc_layer_1_t *layer,
                                     private_handle_t *renderBuffer, int dpy)
{
    hwc_context_t* ctx = (hwc_context_t*)(dev);
    int err = 0;
    if(!ctx) {
         ALOGE("%s: null context ", __FUNCTION__);
         return -1;
    }

    private_handle_t *hnd = (private_handle_t *)layer->handle;
    if(!hnd) {
        ALOGE("%s: invalid handle", __FUNCTION__);
        return -1;
    }

    private_handle_t *fbHandle = (private_handle_t *)renderBuffer;
    if(!fbHandle) {
        ALOGE("%s: Framebuffer handle is NULL", __FUNCTION__);
        return -1;
    }

    // Set the copybit source:
    copybit_image_t src;
    src.w = getWidth(hnd);
    src.h = getHeight(hnd);
    src.format = hnd->format;
    src.base = (void *)hnd->base;
    src.handle = (native_handle_t *)layer->handle;
    src.horiz_padding = src.w - getWidth(hnd);
    // Initialize vertical padding to zero for now,
    // this needs to change to accomodate vertical stride
    // if needed in the future
    src.vert_padding = 0;

    // Copybit source rect
    hwc_rect_t sourceCrop = integerizeSourceCrop(layer->sourceCropf);
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
    dst.handle = (native_handle_t *)fbHandle;

    copybit_device_t *copybit = mEngine;

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

       int usage = HEAP_ID;

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
    copybit->set_parameter(copybit, COPYBIT_BLEND_MODE,
                                              layer->blending);
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
    return err;
}

void CopyBit::getLayerResolution(const hwc_layer_1_t* layer,
                                 unsigned int& width, unsigned int& height)
{
    hwc_rect_t displayFrame  = layer->displayFrame;

    width = displayFrame.right - displayFrame.left;
    height = displayFrame.bottom - displayFrame.top;
}

bool CopyBit::validateParams(hwc_context_t *ctx,
                                        const hwc_display_contents_1_t *list) {
    //Validate parameters
    if (!ctx) {
        ALOGE("%s:Invalid HWC context", __FUNCTION__);
        return false;
    } else if (!list) {
        ALOGE("%s:Invalid HWC layer list", __FUNCTION__);
        return false;
    }
    return true;
}


int CopyBit::allocRenderBuffers(int w, int h, int f)
{
    int ret = 0;
    for (int i = 0; i < NUM_RENDER_BUFFERS; i++) {
        if (mRenderBuffer[i] == NULL) {
            ret = alloc_buffer(&mRenderBuffer[i],
                               w, h, f, HEAP_ID);
        }
        if(ret < 0) {
            freeRenderBuffers();
            break;
        }
    }
    return ret;
}

void CopyBit::freeRenderBuffers()
{
    for (int i = 0; i < NUM_RENDER_BUFFERS; i++) {
        if(mRenderBuffer[i]) {
            //Since we are freeing buffer close the fence if it has a valid one.
            if(mRelFd[i] >= 0) {
                close(mRelFd[i]);
                mRelFd[i] = -1;
            }
            free_buffer(mRenderBuffer[i]);
            mRenderBuffer[i] = NULL;
        }
    }
}

private_handle_t * CopyBit::getCurrentRenderBuffer() {
    return mRenderBuffer[mCurRenderBufferIndex];
}

void CopyBit::setReleaseFd(int fd) {
    if(mRelFd[mCurRenderBufferIndex] >=0)
        close(mRelFd[mCurRenderBufferIndex]);
    mRelFd[mCurRenderBufferIndex] = dup(fd);
}

struct copybit_device_t* CopyBit::getCopyBitDevice() {
    return mEngine;
}

CopyBit::CopyBit(hwc_context_t *ctx, const int& dpy) : mIsModeOn(false),
        mCopyBitDraw(false), mCurRenderBufferIndex(0) {

    getBufferSizeAndDimensions(ctx->dpyAttr[dpy].xres,
            ctx->dpyAttr[dpy].yres,
            HAL_PIXEL_FORMAT_RGBA_8888,
            mAlignedFBWidth,
            mAlignedFBHeight);

    hw_module_t const *module;
    for (int i = 0; i < NUM_RENDER_BUFFERS; i++) {
        mRenderBuffer[i] = NULL;
        mRelFd[i] = -1;
    }

    char value[PROPERTY_VALUE_MAX];
    property_get("debug.hwc.dynThreshold", value, "2");
    mDynThreshold = atof(value);

    if (hw_get_module(COPYBIT_HARDWARE_MODULE_ID, &module) == 0) {
        if(copybit_open(module, &mEngine) < 0) {
            ALOGE("FATAL ERROR: copybit open failed.");
        }
    } else {
        ALOGE("FATAL ERROR: copybit hw module not found");
    }
}

CopyBit::~CopyBit()
{
    freeRenderBuffers();
    if(mEngine)
    {
        copybit_close(mEngine);
        mEngine = NULL;
    }
}
}; //namespace qhwc
