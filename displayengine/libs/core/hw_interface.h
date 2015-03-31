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

#ifndef __HW_INTERFACE_H__
#define __HW_INTERFACE_H__

#include <core/display_interface.h>
#include <private/strategy_interface.h>
#include <private/hw_info_types.h>
#include <utils/constants.h>
#include <core/buffer_allocator.h>
#include <core/buffer_sync_handler.h>

namespace sde {

enum HWBlockType {
  kHWPrimary,
  kHWHDMI,
  kHWWriteback0,
  kHWWriteback1,
  kHWWriteback2,
  kHWBlockMax
};

struct HWBufferInfo : public BufferInfo {
  LayerBuffer output_buffer;
  int session_id;
  uint32_t slot;

  HWBufferInfo() : session_id(-1), slot(0) { }
};

struct HWRotateInfo {
  LayerBuffer *input_buffer;
  uint32_t pipe_id;
  LayerRect src_roi;
  LayerRect dst_roi;
  LayerBufferFormat dst_format;
  HWBlockType writeback_id;
  float downscale_ratio;
  HWBufferInfo hw_buffer_info;
  bool valid;
  uint32_t frame_rate;

  HWRotateInfo() : input_buffer(NULL), pipe_id(0), dst_format(kFormatInvalid),
                   writeback_id(kHWWriteback0), downscale_ratio(1.0f), valid(false),
                   frame_rate(0) { }

  void Reset() { *this = HWRotateInfo(); }
};

struct HWPipeInfo {
  uint32_t pipe_id;
  LayerRect src_roi;
  LayerRect dst_roi;
  uint8_t horizontal_decimation;
  uint8_t vertical_decimation;
  bool valid;
  uint32_t z_order;

  HWPipeInfo() : pipe_id(0), horizontal_decimation(0), vertical_decimation(0), valid(false),
                 z_order(0) { }

  void Reset() { *this = HWPipeInfo(); }
};

struct HWLayerConfig {
  bool use_non_dma_pipe;  // set by client
  HWPipeInfo left_pipe;  // pipe for left side of output
  HWPipeInfo right_pipe;  // pipe for right side of output
  uint32_t num_rotate;  // number of rotate
  HWRotateInfo rotates[kMaxRotatePerLayer];  // rotation for the buffer

  HWLayerConfig() : use_non_dma_pipe(false), num_rotate(0) { }

  void Reset() { *this = HWLayerConfig(); }
};

struct HWLayers {
  HWLayersInfo info;
  HWLayerConfig config[kMaxSDELayers];
  int closed_session_ids[kMaxSDELayers * 2];  // split panel (left + right)

  HWLayers() { memset(closed_session_ids, -1, sizeof(closed_session_ids)); }

  void Reset() { *this = HWLayers(); }
};

struct HWDisplayAttributes : DisplayConfigVariableInfo {
  bool is_device_split;
  uint32_t split_left;
  bool always_src_split;

  HWDisplayAttributes() : is_device_split(false), split_left(0), always_src_split(false) { }

  void Reset() { *this = HWDisplayAttributes(); }
};

// HWEventHandler - Implemented in DisplayBase and HWInterface implementation
class HWEventHandler {
 public:
  virtual DisplayError VSync(int64_t timestamp) = 0;
  virtual DisplayError Blank(bool blank) = 0;
  virtual void IdleTimeout() = 0;
  virtual void ThermalEvent(int64_t thermal_level) = 0;
 protected:
  virtual ~HWEventHandler() { }
};

class HWInterface {
 public:
  virtual DisplayError Open(HWEventHandler *eventhandler) = 0;
  virtual DisplayError Close() = 0;
  virtual DisplayError GetNumDisplayAttributes(uint32_t *count) = 0;
  virtual DisplayError GetDisplayAttributes(HWDisplayAttributes *display_attributes,
                                            uint32_t index) = 0;
  virtual DisplayError GetHWPanelInfo(HWPanelInfo *panel_info) = 0;
  virtual DisplayError SetDisplayAttributes(uint32_t index) = 0;
  virtual DisplayError GetConfigIndex(uint32_t mode, uint32_t *index) = 0;
  virtual DisplayError PowerOn() = 0;
  virtual DisplayError PowerOff() = 0;
  virtual DisplayError Doze() = 0;
  virtual DisplayError Standby() = 0;
  virtual DisplayError Validate(HWLayers *hw_layers) = 0;
  virtual DisplayError Commit(HWLayers *hw_layers) = 0;
  virtual DisplayError Flush() = 0;

 protected:
  virtual ~HWInterface() { }
};

}  // namespace sde

#endif  // __HW_INTERFACE_H__

