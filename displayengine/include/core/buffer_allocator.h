/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*  * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*! @file buffer_allocator.h
  @brief Interface file for platform specific buffer allocator.

  @details Buffer manager in display engine uses this interface to allocate buffer for internal
  usage.
*/

#ifndef __BUFFER_ALLOCATOR_H__
#define __BUFFER_ALLOCATOR_H__

#include <core/layer_buffer.h>

namespace sde {
  /*! @brief Input configuration set by the client for buffer allocation.

    @sa BufferInfo::BufferConfig
  */

struct BufferConfig {
  uint32_t width;            //!< Specifies buffer width for buffer allocation.
  uint32_t height;           //!< Specifies buffer height for buffer allocation.
  LayerBufferFormat format;  //!< Specifies buffer format for buffer allocation.
  uint32_t buffer_count;     //!< Specifies number of buffers to be allocated.
  bool secure;               //!< Specifies buffer to be allocated from secure region.
  bool cache;                //!< Specifies whether the buffer needs to be cache.

  BufferConfig() : width(0), height(0), format(kFormatInvalid), buffer_count(0), secure(false),
                  cache(false) { }
};

/*! @brief Holds the information about the allocated buffer.

  @sa BufferAllocator::AllocateBuffer
  @sa BufferAllocator::FreeBuffer
*/
struct AllocatedBufferInfo {
  int fd;                    //!< Specifies the fd of the allocated buffer.
  uint32_t stride;           //!< Specifies aligned buffer width of the allocated buffer.
  uint32_t size;             //!< Specifies the size of the allocated buffer.

  AllocatedBufferInfo() : fd(-1), stride(0), size(0) { }
};

/*! @brief Holds the information about the input/output configuration of an output buffer.

  @sa BufferAllocator::AllocateBuffer
  @sa BufferAllocator::FreeBuffer
*/
struct BufferInfo {
  BufferConfig buffer_config;             //!< Specifies configuration of a buffer to be allocated.
  AllocatedBufferInfo alloc_buffer_info;  //!< Specifies buffer information of allocated buffer.

  void *private_data;                      //!< Pointer to private data.

  BufferInfo() : private_data(NULL) { }
};

/*! @brief Buffer allocator implemented by the client

  @details This class declares prototype for BufferAllocator methods which must be
  implemented by the client. Buffer manager in display engine will use these methods to
  allocate/deallocate buffers for display engine.

  @sa CompManager::Init
  @sa ResManager::Init
*/
class BufferAllocator {
 public:
  /*! @brief Method to allocate ouput buffer for the given input configuration.

    @details This method allocates memory based on input configuration.

    @param[in] buffer_info \link BufferInfo \endlink

    @return \link DisplayError \endlink

    @sa BufferManager
  */
  virtual DisplayError AllocateBuffer(BufferInfo *buffer_info) = 0;


  /*! @brief Method to deallocate the ouput buffer.

    @details This method deallocates the memory allocated using AllocateBuffer method.

    @param[in] buffer_info \link BufferInfo \endlink

    @return \link DisplayError \endlink

    @sa BufferManager
  */
  virtual DisplayError FreeBuffer(BufferInfo *buffer_info) = 0;

 protected:
  virtual ~BufferAllocator() { }
};

}  // namespace sde

#endif  // __BUFFER_ALLOCATOR_H__

