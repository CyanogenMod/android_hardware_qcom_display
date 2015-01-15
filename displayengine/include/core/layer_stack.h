/*
* Copyright (c) 2014 - 2015, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*! @file layer_stack.h
  @brief File for display layer stack structure which represents a drawing buffer.

  @details Display layer is a drawing buffer object which will be blended with other drawing buffers
  under blending rules.
*/
#ifndef __LAYER_STACK_H__
#define __LAYER_STACK_H__

#include <stdint.h>
#include <utils/constants.h>

#include "layer_buffer.h"
#include "sde_types.h"

namespace sde {

/*! @brief This enum represents display layer blending types.

  @sa Layer
*/
enum LayerBlending {
  kBlendingNone,            //!< Blend operation is not specified.

  kBlendingOpaque,          //!< Pixel color is expressed using straight alpha in color tuples. It
                            //!< is constant blend operation. The layer would appear opaque if plane
                            //!< alpha is 0xFF.

  kBlendingPremultiplied,   //!< Pixel color is expressed using premultiplied alpha in RGBA tuples.
                            //!< If plane alpha is less than 0xFF, apply modulation as well.
                            //!<   pixel.rgb = src.rgb + dest.rgb x (1 - src.a)

  kBlendingCoverage,        //!< Pixel color is expressed using straight alpha in color tuples. If
                            //!< plane alpha is less than 0xff, apply modulation as well.
                            //!<   pixel.rgb = src.rgb x src.a + dest.rgb x (1 - src.a)
};

/*! @brief This enum represents display layer composition types.

  @sa Layer
*/
enum LayerComposition {
  kCompositionGPU,        //!< This layer will be drawn into the target buffer by GPU. Display
                          //!< device will mark the layer for SDE composition if it can handle it
                          //!< or it will mark the layer for GPU composition.

  kCompositionSDE,        //!< This layer will be handled by SDE. It must not be composed by GPU.

  kCompositionGPUTarget,  //!< This layer will hold result of composition for layers marked for GPU
                          //!< composition. If display device sets all other layers for SDE
                          //!< composition then this layer would be ignored during Commit().
                          //!< Only one layer shall be marked as target buffer by the caller.
};

/*! @brief This structure defines rotation and flip values for a display layer.

  @sa Layer
*/
struct LayerTransform {
  float rotation;         //!< Left most pixel coordinate.
  bool flip_horizontal;   //!< Mirror reversal of the layer across a horizontal axis.
  bool flip_vertical;     //!< Mirror reversal of the layer across a vertical axis.

  LayerTransform() : rotation(0.0f), flip_horizontal(false), flip_vertical(false) { }
};

/*! @brief This structure defines flags associated with a layer. The 1-bit flag can be set to ON(1)
  or OFF(0).

  @sa LayerBuffer
*/
struct LayerFlags {
  uint64_t skip : 1;      //!< This flag shall be set by client to indicate that this layer will be
                          //!< handled by GPU. Display Device will not consider it for composition.

  uint64_t updating : 1;  //!< This flag shall be set by client to indicate that this is updating/
                          //!< non-updating. so strategy manager will mark them for SDE/GPU
                          //!< composition respectively when the layer stack qualifies for cache
                          //!< based composition.

  LayerFlags() : skip(0), updating(0) { }
};

/*! @brief This structure defines flags associated with a layer stack. The 1-bit flag can be set to
  ON(1) or OFF(0).

  @sa LayerBuffer
*/
struct LayerStackFlags {
  uint64_t geometry_changed : 1;  //!< This flag shall be set by client to indicate that the layer
                                  //!< set passed to Prepare() has changed by more than just the
                                  //!< buffer handles and acquire fences.

  uint64_t skip_present : 1;      //!< This flag will be set to true, if the current layer stack
                                  //!< contains skip layers.

  uint64_t video_present : 1;     //!< This flag will be set to true, if current layer stack
                                  //!< contains video.

  uint64_t secure_present : 1;    //!< This flag will be set to true, if the current layer stack
                                  //!< contains secure layers.

  LayerStackFlags() : geometry_changed(0), skip_present(0), video_present(0), secure_present(0) { }
};

/*! @brief This structure defines a rectanglular area inside a display layer.

  @sa LayerRectArray
*/
struct LayerRect {
  float left;     //!< Left-most pixel coordinate.
  float top;      //!< Top-most pixel coordinate.
  float right;    //!< Right-most pixel coordinate.
  float bottom;   //!< Bottom-most pixel coordinate.

  LayerRect() : left(0.0f), top(0.0f), right(0.0f), bottom(0.0f) { }

  LayerRect(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) { }
};

/*! @brief This structure defines an array of display layer rectangles.

  @sa LayerRect
*/
struct LayerRectArray {
  LayerRect *rect;  //!< Pointer to first element of array.
  uint32_t count;   //!< Number of elements in the array.

  LayerRectArray() : rect(NULL), count(0) { }
};

/*! @brief This structure defines display layer object which contains layer properties and a drawing
  buffer.

  @sa LayerArray
*/
struct Layer {
  LayerBuffer *input_buffer;        //!< Pointer to the buffer to be composed. If this remains
                                    //!< unchanged between two consecutive Prepare() calls and
                                    //!< geometry_changed flag is not set for the second call, then
                                    //!< the display device will assume that buffer content has not
                                    //!< changed.

  LayerComposition composition;     //!< Composition type which can be set by either the client or
                                    //!< the display device. This value should be preserved between
                                    //!< Prepare() and Commit() calls.

  LayerRect src_rect;               //!< Rectangular area of the layer buffer to consider for
                                    //!< composition.

  LayerRect dst_rect;               //!< The target position where the frame will be displayed.
                                    //!< Cropping rectangle is scaled to fit into this rectangle.
                                    //!< The origin is top-left corner of the screen.

  LayerRectArray visible_regions;   //!< Visible rectangular areas in screen space. The visible
                                    //!< region includes areas overlapped by a translucent layer.

  LayerRectArray dirty_regions;     //!< Rectangular areas in the current frames that have changed
                                    //!< in comparison to previous frame.

  LayerBlending blending;           //!< Blending operation which need to be applied on the layer
                                    //!< buffer during composition.

  LayerTransform transform;         //!< Rotation/Flip operations which need to be applied to the
                                    //!< layer buffer during composition.

  uint8_t plane_alpha;              //!< Alpha value applied to the whole layer. Value of each pixel
                                    //!< computed as:
                                    //!<    if(kBlendingPremultiplied) {
                                    //!<      pixel.RGB = pixel.RGB * planeAlpha / 255
                                    //!<    }
                                    //!<    pixel.a = pixel.a * planeAlpha

  LayerFlags flags;                 //!< Flags associated with this layer.

  uint32_t frame_rate;              //!< Rate at which frames are being updated for this layer.

  Layer() : input_buffer(NULL), composition(kCompositionGPU), blending(kBlendingNone),
            plane_alpha(0), frame_rate(0) { }
};

/*! @brief This structure defines a layer stack that contains layers which need to be composed and
  rendered onto the target.

  @sa DisplayInterface::Prepare
  @sa DisplayInterface::Commit
*/
struct LayerStack {
  Layer *layers;                //!< Array of layers.
  uint32_t layer_count;         //!< Total number of layers.

  int retire_fence_fd;          //!< File descriptor referring to a sync fence object which will
                                //!< be signaled when this composited frame has been replaced on
                                //!< screen by a subsequent frame on a physical display. The fence
                                //!< object is created and returned during Commit(). Client shall
                                //!< Client shall close the returned file descriptor.
                                //!< NOTE: This field applies to a physical display only.

  LayerBuffer *output_buffer;   //!< Pointer to the buffer where composed buffer would be rendered
                                //!< for virtual displays.
                                //!< NOTE: This field applies to a virtual display only.


  LayerStackFlags flags;        //!< Flags associated with this layer set.

  LayerStack() : layers(NULL), layer_count(0), retire_fence_fd(-1), output_buffer(NULL) { }
};

}  // namespace sde

#endif  // __LAYER_STACK_H__

