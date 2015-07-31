/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
* Redistribution and use in source and binary forms, with or without
* * modification, are permitted provided that the following conditions are
* met:
*   * Redistributions of source code must retain the above copyrigh
*     notice, this list of conditions and the following disclaimer
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of The Linux Foundation nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "cb_utils.h"
#include "cb_swap_rect.h"
/* get union of two rects into 3rd rect */
void getUnion(hwc_rect_t& rect1,hwc_rect_t& rect2, hwc_rect_t& irect) {

    irect.left   = min(rect1.left, rect2.left);
    irect.top    = min(rect1.top, rect2.top);
    irect.right  = max(rect1.right, rect2.right);
    irect.bottom = max(rect1.bottom, rect2.bottom);
}

int clear (copybit_device_t *copybit, private_handle_t* hnd, hwc_rect_t& rect)
{
    int ret = 0;
    copybit_rect_t clear_rect = {rect.left, rect.top,rect.right,rect.bottom};

    copybit_image_t buf;
    buf.w = ALIGN(getWidth(hnd),32);
    buf.h = getHeight(hnd);
    buf.format = hnd->format;
    buf.base = (void *)hnd->base;
    buf.handle = (native_handle_t *)hnd;

    ret = copybit->clear(copybit, &buf, &clear_rect);
    return ret;
}
using namespace android;
using namespace qhwc;
namespace qdutils {

int CBUtils::uiClearRegion(hwc_display_contents_1_t* list,
        int version, LayerProp *layerProp,  hwc_rect_t dirtyRect,
            copybit_device_t *copybit, private_handle_t *renderBuffer) {

    size_t last = list->numHwLayers - 1;
    hwc_rect_t fbFrame = list->hwLayers[last].displayFrame;
    Rect fbFrameRect(fbFrame.left,fbFrame.top,fbFrame.right,fbFrame.bottom);
    Region wormholeRegion(fbFrameRect);

   if ((dirtyRect.right - dirtyRect.left > 0) &&
                               (dirtyRect.bottom - dirtyRect.top > 0)) {
#ifdef QTI_BSP
      Rect tmpRect(dirtyRect.left,dirtyRect.top,dirtyRect.right,
            dirtyRect.bottom);
      Region tmpRegion(tmpRect);
      wormholeRegion = wormholeRegion.intersect(tmpRegion);
#endif
   }
    if(cb_swap_rect::getInstance().checkSwapRectFeature_on() == true){
      wormholeRegion.set(0,0);
      for(size_t i = 0 ; i < last; i++) {
         if(((list->hwLayers[i].blending == HWC_BLENDING_NONE) &&
           (list->hwLayers[i].planeAlpha == 0xFF)) ||
           !(layerProp[i].mFlags & HWC_COPYBIT) ||
           (list->hwLayers[i].flags  & HWC_SKIP_HWC_COMPOSITION))
              continue ;
         hwc_rect_t displayFrame = list->hwLayers[i].displayFrame;
         Rect tmpRect(displayFrame.left,displayFrame.top,
                      displayFrame.right,displayFrame.bottom);
         wormholeRegion.set(tmpRect);
      }
   }else{
     for (size_t i = 0 ; i < last; i++) {
        // need to take care only in per pixel blending.
        // Restrict calculation only for copybit layers.
        if((list->hwLayers[i].blending != HWC_BLENDING_NONE) ||
           (list->hwLayers[i].planeAlpha != 0xFF) ||
           !(layerProp[i].mFlags & HWC_COPYBIT))
            continue ;
        hwc_rect_t displayFrame = list->hwLayers[i].displayFrame;
        Rect tmpRect(displayFrame.left,displayFrame.top,displayFrame.right,
        displayFrame.bottom);
        Region tmpRegion(tmpRect);
        wormholeRegion.subtractSelf(wormholeRegion.intersect(tmpRegion));
     }
   }
   if(wormholeRegion.isEmpty()){
        return 1;
   }
   //TO DO :- 1. remove union and call clear for each rect.
   Region::const_iterator it = wormholeRegion.begin();
   Region::const_iterator const end = wormholeRegion.end();
   while (it != end) {
       const Rect& r = *it++;
       hwc_rect_t tmpWormRect = {r.left,r.top,r.right,r.bottom};
       if (version == qdutils::MDP_V3_0_4 ||
               version == qdutils::MDP_V3_0_5) {
           int clear_w =  tmpWormRect.right - tmpWormRect.left;
           int clear_h =  tmpWormRect.bottom - tmpWormRect.top;
           //mdp can't handle solid fill for one line
           //making solid fill as full in this case
           //disable swap rect if presents
           if ((clear_w == 1) || (clear_h ==1)) {
               clear(copybit, renderBuffer, fbFrame);
               return 0;
           } else {
               clear(copybit, renderBuffer, tmpWormRect);
           }
       } else {
           clear(copybit, renderBuffer, tmpWormRect);
       }
   }
   return 1;
}

}//namespace qdutils
