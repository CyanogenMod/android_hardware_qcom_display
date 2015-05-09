/*
* Copyright (c) 2014, The Linux Foundation. All rights reserved.
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

/*! @file layer_buffer.h
  @brief File for layer buffer structure.

*/
#ifndef __LAYER_BUFFER_H__
#define __LAYER_BUFFER_H__

#include <stdint.h>

#include "sdm_types.h"

namespace sdm {

/*! @brief This enum represents different buffer formats supported by display manager.

  @sa LayerBuffer
*/
enum LayerBufferFormat {
  /* All RGB formats, Any new format will be added towards end of this group to maintain backward
     compatibility.
  */
  kFormatARGB8888,      //!< 8-bits Alpha, Red, Green, Blue interleaved in ARGB order.
  kFormatRGBA8888,      //!< 8-bits Red, Green, Blue, Alpha interleaved in RGBA order.
  kFormatBGRA8888,      //!< 8-bits Blue, Green, Red, Alpha interleaved in BGRA order.
  kFormatXRGB8888,      //!< 8-bits Padding, Red, Green, Blue interleaved in XRGB order. No Alpha.
  kFormatRGBX8888,      //!< 8-bits Red, Green, Blue, Padding interleaved in RGBX order. No Alpha.
  kFormatBGRX8888,      //!< 8-bits Blue, Green, Red, Padding interleaved in BGRX order. No Alpha.
  kFormatRGBA5551,      //!< 5-bits Red, Green, Blue, and 1 bit Alpha interleaved in RGBA order.
  kFormatRGBA4444,      //!< 4-bits Red, Green, Blue, Alpha interleaved in RGBA order.
  kFormatRGB888,        //!< 8-bits Red, Green, Blue interleaved in RGB order. No Alpha.
  kFormatBGR888,        //!< 8-bits Blue, Green, Red interleaved in BGR order. No Alpha.
  kFormatRGB565,        //!< 5-bit Red, 6-bit Green, 5-bit Blue interleaved in RGB order. No Alpha.
  kFormatRGBA8888Ubwc,  //!< UBWC aligned RGBA8888 format
  kFormatRGBX8888Ubwc,  //!< UBWC aligned RGBX8888 format
  kFormatRGB565Ubwc,    //!< UBWC aligned RGB565 format

  /* All YUV-Planar formats, Any new format will be added towards end of this group to maintain
     backward compatibility.
  */
  kFormatYCbCr420Planar = 0x100,  //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                  //!< 2x2 subsampled U-plane: u(0), u(2) ... u(n-1)
                                  //!< 2x2 subsampled V-plane: v(0), v(2) ... v(n-1)

  kFormatYCrCb420Planar,          //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                  //!< 2x2 subsampled V-plane: v(0), v(2) ... v(n-1)
                                  //!< 2x2 subsampled U-plane: u(0), u(2) ... u(n-1)

  /* All YUV-Semiplanar formats, Any new format will be added towards end of this group to
     maintain backward compatibility.
  */
  kFormatYCbCr420SemiPlanar = 0x200,  //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< 2x2 subsampled interleaved UV-plane:
                                      //!<    u(0), v(0), u(2), v(2) ... u(n-1), v(n-1)
                                      //!< aka NV12.

  kFormatYCrCb420SemiPlanar,          //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< 2x2 subsampled interleaved VU-plane:
                                      //!<    v(0), u(0), v(2), u(2) ... v(n-1), u(n-1)
                                      //!< aka NV21.

  kFormatYCbCr420SemiPlanarVenus,     //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< 2x2 subsampled interleaved UV-plane:
                                      //!<    u(0), v(0), u(2), v(2) ... u(n-1), v(n-1)

  kFormatYCbCr422H1V2SemiPlanar,      //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< vertically subsampled interleaved UV-plane:
                                      //!<    u(0), v(1), u(2), v(3) ... u(n-1), v(n)

  kFormatYCrCb422H1V2SemiPlanar,      //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< vertically subsampled interleaved VU-plane:
                                      //!<    v(0), u(1), v(2), u(3) ... v(n-1), u(n)

  kFormatYCbCr422H2V1SemiPlanar,      //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< horizontally subsampled interleaved UV-plane:
                                      //!<    u(0), v(1), u(2), v(3) ... u(n-1), v(n)

  kFormatYCrCb422H2V1SemiPlanar,      //!< Y-plane: y(0), y(1), y(2) ... y(n)
                                      //!< horizontally subsampled interleaved VU-plane:
                                      //!<    v(0), u(1), v(2), u(3) ... v(n-1), u(n)

  kFormatYCbCr420SPVenusUbwc,         //!< UBWC aligned YCbCr420SemiPlanarVenus format

  /* All YUV-Packed formats, Any new format will be added towards end of this group to maintain
     backward compatibility.
  */
  kFormatYCbCr422H2V1Packed = 0x300,  //!< Y-plane interleaved with horizontally subsampled U/V by
                                      //!< factor of 2
                                      //!<    y(0), u(0), y(1), v(0), y(2), u(2), y(3), v(2)
                                      //!<    y(n-1), u(n-1), y(n), v(n-1)

  kFormatInvalid = 0xFFFFFFFF,
};

/*! @brief This structure defines a color sample plane belonging to a buffer format. RGB buffer
  formats have 1 plane whereas YUV buffer formats may have upto 4 planes.

  @sa LayerBuffer
*/
struct LayerBufferPlane {
  int fd;           //!< File descriptor referring to the buffer associated with this plane.
  uint32_t offset;  //!< Offset of the plane in bytes from beginning of the buffer.
  uint32_t stride;  //!< Stride in bytes i.e. length of a scanline including padding.

  LayerBufferPlane() : fd(-1), offset(0), stride(0) { }
};

/*! @brief This structure defines flags associated with a layer buffer. The 1-bit flag can be set
  to ON(1) or OFF(0).

  @sa LayerBuffer
*/
struct LayerBufferFlags {
  union {
    struct {
      uint32_t secure : 1;          //!< This flag shall be set by client to indicate that the
                                    //!< buffer need to be handled securely.

      uint32_t video  : 1;          //!< This flag shall be set by client to indicate that the
                                    //!< buffer is video/ui buffer.

      uint32_t macro_tile : 1;      //!< This flag shall be set by client to indicate that the
                                    //!< buffer format is macro tiled.

      uint32_t interlace : 1;       //!< This flag shall be set by the client to indicate that
                                    //!< the buffer has interlaced content.

      uint32_t secure_display : 1;
                                    //!< This flag shall be set by the client to indicate that the
                                    //!< secure display session is in progress. Secure display
                                    //!< session can not coexist with non-secure session.
      };

      uint32_t flags;   //!< For initialization purpose only. Client shall not refer it directly.
  };

  LayerBufferFlags() : flags(0) { }
};

/*! @brief This structure defines a layer buffer handle which contains raw buffer and its associated
  properties.

  @sa LayerBuffer
  @sa LayerStack
*/
struct LayerBuffer {
  uint32_t width;               //!< Actual width of the Layer that this buffer is for.
  uint32_t height;              //!< Actual height of the Layer that this buffer is for.
  LayerBufferFormat format;     //!< Format of the buffer content.
  LayerBufferPlane planes[4];   //!< Array of planes that this buffer contains. RGB buffer formats
                                //!< have 1 plane whereas YUV buffer formats may have upto 4 planes.
                                //!< Total number of planes for the buffer will be interpreted based
                                //!< on the buffer format specified.

  int acquire_fence_fd;         //!< File descriptor referring to a sync fence object which will be
                                //!< signaled when buffer can be read/write by display manager.
                                //!< This fence object is set by the client during Commit(). For
                                //!< input buffers client shall signal this fence when buffer
                                //!< content is available and can be read by display manager. For
                                //!< output buffers, client shall signal fence when buffer is ready
                                //!< to be written by display manager.

                                //!< This field is used only during Commit() and shall be set to -1
                                //!< by the client when buffer is already available for read/write.

  int release_fence_fd;         //!< File descriptor referring to a sync fence object which will be
                                //!< signaled when buffer has been read/written by display manager.
                                //!< This fence object is set by display manager during Commit().
                                //!< For input buffers display manager will signal this fence when
                                //!< buffer has been consumed. For output buffers, display manager
                                //!< will signal this fence when buffer is produced.

                                //!< This field is used only during Commit() and will be set to -1
                                //!< by display manager when buffer is already available for
                                //!< read/write.

  LayerBufferFlags flags;       //!< Flags associated with this buffer.

  LayerBuffer() : width(0), height(0), format(kFormatRGBA8888), acquire_fence_fd(-1),
                  release_fence_fd(-1) { }
};

}  // namespace sdm

#endif  // __LAYER_BUFFER_H__

