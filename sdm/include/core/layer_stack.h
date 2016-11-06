/*
* Copyright (c) 2014 - 2016, The Linux Foundation. All rights reserved.
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
#include "sdm_types.h"

namespace sdm {

/*! @brief This enum represents display layer blending types.

  @sa Layer
*/
enum LayerBlending {
  kBlendingPremultiplied,   //!< Pixel color is expressed using premultiplied alpha in RGBA tuples.
                            //!< If plane alpha is less than 0xFF, apply modulation as well.
                            //!<   pixel.rgb = src.rgb + dest.rgb x (1 - src.a)

  kBlendingOpaque,          //!< Pixel color is expressed using straight alpha in color tuples. It
                            //!< is constant blend operation. The layer would appear opaque if plane
                            //!< alpha is 0xFF.

  kBlendingCoverage,        //!< Pixel color is expressed using straight alpha in color tuples. If
                            //!< plane alpha is less than 0xff, apply modulation as well.
                            //!<   pixel.rgb = src.rgb x src.a + dest.rgb x (1 - src.a)
};

/*! @brief This enum represents display layer composition types.

  @sa Layer
*/
enum LayerComposition {
  kCompositionGPU,          //!< This layer will be drawn onto the target buffer by GPU. Display
                            //!< device will mark the layer for GPU composition if it can not handle
                            //!< it completely.

  kCompositionGPUS3D,       //!< This layer will be drawn onto the target buffer in s3d mode by GPU.
                            //!< Display device will mark the layer for GPU composition if it can not
                            //!< handle it completely.

  kCompositionSDE,          //!< This layer will be handled by SDE. It must not be composed by GPU.

  kCompositionHWCursor,     //!< This layer will be handled by SDE using HWCursor. It must not be
                            //!< composed by GPU

  kCompositionHybrid,       //!< This layer will be drawn by a blit engine and SDE together. Display
                            //!< device will split the layer, update the blit rectangle that
                            //!< need to be composed by a blit engine and update original source
                            //!< rectangle that will be composed by SDE.

  kCompositionBlit,         //!< This layer will be composed using Blit Engine

  kCompositionGPUTarget,    //!< This layer will hold result of composition for layers marked for
                            //!< GPU composition.
                            //!< If display device does not set any layer for SDE composition then
                            //!< this would be ignored during Commit().
                            //!< Only one layer shall be marked as target buffer by the caller.
                            //!< GPU target layer shall be after application layers in layer stack.

  kCompositionBlitTarget,   //!< This layer will hold result of composition for blit rectangles
                            //!< from the layers marked for hybrid composition. Nth blit rectangle
                            //!< in a layer shall be composed onto Nth blit target.
                            //!< If display device does not set any layer for hybrid composition
                            //!< then this would be ignored during Commit().
                            //!< Blit target layers shall be after GPU target layer in layer stack.
};

/*! @brief This enum represents display layer color space conversion (CSC) matrix types.

  @sa Layer
*/
enum LayerCSC {
  kCSCLimitedRange601,    //!< 601 limited range color space.
  kCSCFullRange601,       //!< 601 full range color space.
  kCSCLimitedRange709,    //!< 709 limited range color space.
};

/*! @brief This enum represents display layer inverse gamma correction (IGC) types.

  @sa Layer
*/
enum LayerIGC {
  kIGCNotSpecified,       //!< IGC is not specified.
  kIGCsRGB,               //!< sRGB IGC type.
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
  union {
    struct {
      uint32_t skip : 1;      //!< This flag shall be set by client to indicate that this layer
                              //!< will be handled by GPU. Display Device will not consider it
                              //!< for composition.

      uint32_t updating : 1;  //!< This flag shall be set by client to indicate that this is
                              //!< updating non-updating. so strategy manager will mark them for
                              //!< SDE/GPU composition respectively when the layer stack qualifies
                              //!< for cache based composition.

      uint32_t solid_fill : 1;
                              //!< This flag shall be set by client to indicate that this layer
                              //!< is for solid fill without input buffer. Display Device will
                              //!< use SDE HW feature to achieve it.

      uint32_t cursor : 1;    //!< This flag shall be set by client to indicate that this layer
                              //!< is a cursor
                              //!< Display Device may handle this layer using HWCursor

      uint32_t single_buffer : 1;  //!< This flag shall be set by client to indicate that the layer
                                   //!< uses only a single buffer that will not be swapped out
    };

    uint32_t flags = 0;       //!< For initialization purpose only.
                              //!< Client shall not refer it directly.
  };
};

/*! @brief This structure defines flags associated with a layer stack. The 1-bit flag can be set to
  ON(1) or OFF(0).

  @sa LayerBuffer
*/
struct LayerStackFlags {
  union {
    struct {
      uint32_t geometry_changed : 1;  //!< This flag shall be set by client to indicate that the
                                      //!< layer set passed to Prepare() has changed by more than
                                      //!< just the buffer handles and acquire fences.

      uint32_t skip_present : 1;      //!< This flag will be set to true, if the current layer
                                      //!< stack contains skip layers.

      uint32_t video_present : 1;     //!< This flag will be set to true, if current layer stack
                                      //!< contains video.

      uint32_t secure_present : 1;    //!< This flag will be set to true, if the current layer
                                      //!< stack contains secure layers.

      uint32_t animating : 1;         //!< This flag shall be set by client to indicate that the
                                      //!<  current frame is animating.i

      uint32_t attributes_changed : 1;
                                      //!< This flag shall be set by client to indicate that the
                                      //!< current frame has some properties changed and
                                      //!< needs re-config.

      uint32_t cursor_present : 1;    //!< This flag will be set to true if the current layer
                                      //!< stack contains cursor layer.

      uint32_t single_buffered_layer_present : 1;    //!< Set if stack has single buffered layer

      uint32_t s3d_mode_present : 1;  //!< This flag will be set to true, if the current layer
                                      //!< stack contains s3d layer, and the layer stack can enter
                                      //!< s3d mode.
    };

    uint32_t flags = 0;               //!< For initialization purpose only.
                                      //!< Client shall not refer it directly.
  };
};

/*! @brief This structure defines a rectanglular area inside a display layer.

  @sa LayerRectArray
*/
struct LayerRect {
  float left   = 0.0f;   //!< Left-most pixel coordinate.
  float top    = 0.0f;   //!< Top-most pixel coordinate.
  float right  = 0.0f;   //!< Right-most pixel coordinate.
  float bottom = 0.0f;   //!< Bottom-most pixel coordinate.

  LayerRect() = default;
  LayerRect(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) { }
};

/*! @brief This structure defines an array of display layer rectangles.

  @sa LayerRect
*/
struct LayerRectArray {
  LayerRect *rect = NULL;  //!< Pointer to first element of array.
  uint32_t count = 0;      //!< Number of elements in the array.
};

/*! @brief This structure defines FRC info of display layer.

  @sa LayerFrcInfo
*/

struct LayerFrcInfo {
  /* Deterministic Frame Rate Control (FRC) */
  int32_t enable;
  /* video frame count */
  uint32_t frame_cnt;
  /* video timestamp */
  int64_t timestamp;
};

/*! @brief This structure defines display layer object which contains layer properties and a drawing
  buffer.

  @sa LayerArray
*/
struct Layer {
  LayerBuffer *input_buffer = NULL;                //!< Pointer to the buffer to be composed.
                                                   //!< If this remains unchanged between two
                                                   //!< consecutive Prepare() calls and
                                                   //!< geometry_changed flag is not set for the
                                                   //!< second call, then the display device will
                                                   //!< assume that buffer content has not
                                                   //!< changed.

  LayerComposition composition = kCompositionGPU;  //!< Composition type which can be set by either
                                                   //!< the client or the display device. This value
                                                   //!< should be preserved between Prepare() and
                                                   //!< Commit() calls.

  LayerRect src_rect;                              //!< Rectangular area of the layer buffer to
                                                   //!< consider for composition.

  LayerRect dst_rect;                              //!< The target position where the frame will be
                                                   //!< displayed. Cropping rectangle is scaled to
                                                   //!< fit into this rectangle. The origin is the
                                                   //!< top-left corner of the screen.

  LayerRectArray visible_regions;                  //!< Visible rectangular areas in screen space.
                                                   //!< The visible region includes areas overlapped
                                                   //!< by a translucent layer.

  LayerRectArray dirty_regions;                    //!< Rectangular areas in the current frames
                                                   //!< that have changed in comparison to
                                                   //!< previous frame.

  LayerRectArray blit_regions;                     //!< Rectangular areas of this layer which need
                                                   //!< to be composed to blit target. Display
                                                   //!< device will update blit rectangles if a
                                                   //!< layer composition is set as hybrid. Nth blit
                                                   //!< rectangle shall be composed onto Nth blit
                                                   //!< target.

  LayerBlending blending = kBlendingPremultiplied;  //!< Blending operation which need to be
                                                    //!< applied on the layer buffer during
                                                    //!< composition.

  LayerTransform transform;                        //!< Rotation/Flip operations which need to be
                                                   //!< applied to the layer buffer during
                                                   //!< composition.

  uint8_t plane_alpha = 0;                         //!< Alpha value applied to the whole layer.
                                                   //!< Value of each pixel is computed as:
                                                   //!<    if(kBlendingPremultiplied) {
                                                   //!<      pixel.RGB = pixel.RGB * planeAlpha/255
                                                   //!<    }
                                                   //!<    pixel.a = pixel.a * planeAlpha

  uint32_t frame_rate = 0;                         //!< Rate at which frames are being updated for
                                                   //!< this layer.

  LayerCSC csc = kCSCLimitedRange601;              //!< Color Space of the layer.

  LayerIGC igc = kIGCNotSpecified;                 //!< IGC that will be applied on this layer.

  LayerFrcInfo frc_info;                           //!< FRC info which contains video frame count
                                                   //!< and timestamp of this layer.

  uint32_t solid_fill_color = 0;                   //!< Solid color used to fill the layer when
                                                   //!< no content is associated with the layer.

  LayerFlags flags;                                //!< Flags associated with this layer.
};

/*! @brief This structure defines a layer stack that contains layers which need to be composed and
  rendered onto the target.

  @sa DisplayInterface::Prepare
  @sa DisplayInterface::Commit
*/
struct LayerStack {
  Layer *layers = NULL;                //!< Array of layers.
  uint32_t layer_count = 0;            //!< Total number of layers.

  int retire_fence_fd = -1;            //!< File descriptor referring to a sync fence object which
                                       //!< will be signaled when this composited frame has been
                                       //!< replaced on screen by a subsequent frame on a physical
                                       //!< display. The fence object is created and returned during
                                       //!< Commit(). Client shall close the returned file
                                       //!< descriptor.
                                       //!< NOTE: This field applies to a physical display only.

  LayerBuffer *output_buffer = NULL;   //!< Pointer to the buffer where composed buffer would be
                                       //!< rendered for virtual displays.
                                       //!< NOTE: This field applies to a virtual display only.

  LayerStackFlags flags;               //!< Flags associated with this layer set.
};

}  // namespace sdm

#endif  // __LAYER_STACK_H__

