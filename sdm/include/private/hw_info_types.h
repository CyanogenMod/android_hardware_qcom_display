/*
* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
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

#ifndef __HW_INFO_TYPES_H__
#define __HW_INFO_TYPES_H__

#include <stdint.h>
#include <core/display_interface.h>
#include <core/core_interface.h>
#include <vector>
#include <map>
#include <string>

namespace sdm {
const int kMaxSDELayers = 16;   // Maximum number of layers that can be handled by hardware in a
                                // given layer stack.

enum HWDeviceType {
  kDevicePrimary,
  kDeviceHDMI,
  kDeviceVirtual,
  kDeviceRotator,
  kDeviceMax,
};

enum HWBlockType {
  kHWPrimary,
  kHWHDMI,
  kHWWriteback0,
  kHWWriteback1,
  kHWWriteback2,
  kHWBlockMax
};

enum HWDisplayMode {
  kModeDefault,
  kModeVideo,
  kModeCommand,
};

enum HWDisplayPort {
  kPortDefault,
  kPortDSI,
  kPortDTv,
  kPortWriteBack,
  kPortLVDS,
  kPortEDP,
};

enum PipeType {
  kPipeTypeUnused,
  kPipeTypeVIG,
  kPipeTypeRGB,
  kPipeTypeDMA,
  kPipeTypeCursor,
};

enum HWSubBlockType {
  kHWVIGPipe,
  kHWRGBPipe,
  kHWDMAPipe,
  kHWCursorPipe,
  kHWRotatorInput,
  kHWRotatorOutput,
  kHWWBIntfOutput,
  kHWSubBlockMax,
};

typedef std::map<HWSubBlockType, std::vector<LayerBufferFormat>> FormatsMap;

struct HWDynBwLimitInfo {
  uint32_t cur_mode = kBwDefault;
  uint32_t total_bw_limit[kBwModeMax] = { 0 };
  uint32_t pipe_bw_limit[kBwModeMax] = { 0 };
};

struct HWPipeCaps {
  PipeType type = kPipeTypeUnused;
  uint32_t id = 0;
  uint32_t max_rects = 1;
};

struct HWRotatorInfo {
  enum { ROT_TYPE_MDSS, ROT_TYPE_V4L2 };
  uint32_t type = ROT_TYPE_MDSS;
  uint32_t num_rotator = 0;
  bool has_downscale = false;
  std::string device_path = "";

  void Reset() { *this = HWRotatorInfo(); }
};

struct HWResourceInfo {
  uint32_t hw_version = 0;
  uint32_t hw_revision = 0;
  uint32_t num_dma_pipe = 0;
  uint32_t num_vig_pipe = 0;
  uint32_t num_rgb_pipe = 0;
  uint32_t num_cursor_pipe = 0;
  uint32_t num_blending_stages = 0;
  uint32_t num_control = 0;
  uint32_t num_mixer_to_disp = 0;
  uint32_t smp_total = 0;
  uint32_t smp_size = 0;
  uint32_t num_smp_per_pipe = 0;
  uint32_t max_scale_up = 1;
  uint32_t max_scale_down = 1;
  uint64_t max_bandwidth_low = 0;
  uint64_t max_bandwidth_high = 0;
  uint32_t max_mixer_width = 2048;
  uint32_t max_pipe_width = 2048;
  uint32_t max_cursor_size = 0;
  uint32_t max_pipe_bw =  0;
  uint32_t max_sde_clk = 0;
  float clk_fudge_factor = 1.0f;
  uint32_t macrotile_nv12_factor = 0;
  uint32_t macrotile_factor = 0;
  uint32_t linear_factor = 0;
  uint32_t scale_factor = 0;
  uint32_t extra_fudge_factor = 0;
  bool has_bwc = false;
  bool has_ubwc = false;
  bool has_decimation = false;
  bool has_macrotile = false;
  bool has_non_scalar_rgb = false;
  bool is_src_split = false;
  bool perf_calc = false;
  bool has_dyn_bw_support = false;
  bool separate_rotator = false;
  HWDynBwLimitInfo dyn_bw_info;
  std::vector<HWPipeCaps> hw_pipes;
  FormatsMap supported_formats_map;
  HWRotatorInfo hw_rot_info;

  void Reset() { *this = HWResourceInfo(); }
};

struct HWSplitInfo {
  uint32_t left_split = 0;
  uint32_t right_split = 0;

  bool operator !=(const HWSplitInfo &split_info) {
    return ((left_split != split_info.left_split) || (right_split != split_info.right_split));
  }

  bool operator ==(const HWSplitInfo &split_info) {
    return !(operator !=(split_info));
  }
};

enum HWS3DMode {
  kS3DModeNone,
  kS3DModeLR,
  kS3DModeRL,
  kS3DModeTB,
  kS3DModeFP,
  kS3DModeMax,
};

struct HWPanelInfo {
  HWDisplayPort port = kPortDefault;  // Display port
  HWDisplayMode mode = kModeDefault;  // Display mode
  bool partial_update = false;        // Partial update feature
  int left_align = 0;                 // ROI left alignment restriction
  int width_align = 0;                // ROI width alignment restriction
  int top_align = 0;                  // ROI top alignment restriction
  int height_align = 0;               // ROI height alignment restriction
  int min_roi_width = 0;              // Min width needed for ROI
  int min_roi_height = 0;             // Min height needed for ROI
  bool needs_roi_merge = false;       // Merge ROI's of both the DSI's
  bool dynamic_fps = false;           // Panel Supports dynamic fps
  uint32_t min_fps = 0;               // Min fps supported by panel
  uint32_t max_fps = 0;               // Max fps supported by panel
  bool is_primary_panel = false;      // Panel is primary display
  bool is_pluggable = false;          // Panel is pluggable
  HWSplitInfo split_info;             // Panel split configuration
  char panel_name[256] = {0};         // Panel name
  HWS3DMode s3d_mode = kS3DModeNone;  // Panel's current s3d mode.
  int panel_max_brightness = 0;       // Max panel brightness

  bool operator !=(const HWPanelInfo &panel_info) {
    return ((port != panel_info.port) || (mode != panel_info.mode) ||
            (partial_update != panel_info.partial_update) ||
            (left_align != panel_info.left_align) || (width_align != panel_info.width_align) ||
            (top_align != panel_info.top_align) || (height_align != panel_info.height_align) ||
            (min_roi_width != panel_info.min_roi_width) ||
            (min_roi_height != panel_info.min_roi_height) ||
            (needs_roi_merge != panel_info.needs_roi_merge) ||
            (dynamic_fps != panel_info.dynamic_fps) || (min_fps != panel_info.min_fps) ||
            (max_fps != panel_info.max_fps) || (is_primary_panel != panel_info.is_primary_panel) ||
            (split_info != panel_info.split_info) ||
            (s3d_mode != panel_info.s3d_mode));
  }

  bool operator ==(const HWPanelInfo &panel_info) {
    return !(operator !=(panel_info));
  }
};

struct HWSessionConfig {
  LayerRect src_rect;
  LayerRect dst_rect;
  uint32_t buffer_count = 0;
  bool secure = false;
  uint32_t frame_rate = 0;
  LayerTransform transform;

  bool operator==(const HWSessionConfig& config) const {
    return (src_rect == config.src_rect &&
            dst_rect == config.dst_rect &&
            buffer_count == config.buffer_count &&
            secure == config.secure &&
            frame_rate == config.frame_rate &&
            transform == config.transform);
  }

  bool operator!=(const HWSessionConfig& config) const {
    return !operator==(config);
  }
};

struct HWRotateInfo {
  int pipe_id = -1;  // Not actual pipe id, but the relative DMA id
  int writeback_id = -1;  // Writeback block id, but this is the same as DMA id
  LayerRect src_roi;  // Source crop of each split
  LayerRect dst_roi;  // Destination crop of each split
  bool valid = false;
  int rotate_id = -1;  // Actual rotator session id with driver

  void Reset() { *this = HWRotateInfo(); }
};

struct HWRotatorSession {
  HWRotateInfo hw_rotate_info[kMaxRotatePerLayer];
  uint32_t hw_block_count = 0;  // number of rotator hw blocks used by rotator session
  int session_id = -1;  // A handle with Session Manager
  HWSessionConfig hw_session_config;
  LayerBuffer input_buffer;  // Input to rotator
  LayerBuffer output_buffer;  // Output of rotator, crop width and stride are same
  float input_compression = 1.0f;
  float output_compression = 1.0f;
  bool is_buffer_cached = false;
};

struct HWPixelExtension {
  int extension;  // Number of pixels extension in left, right, top and bottom directions for all
                  // color components. This pixel value for each color component should be sum of
                  // fetch and repeat pixels.

  int overfetch;  // Number of pixels need to be overfetched in left, right, top and bottom
                  // directions from source image for scaling.

  int repeat;     // Number of pixels need to be repeated in left, right, top and bottom directions
                  // for scaling.
};

struct HWPlane {
  int init_phase_x = 0;
  int phase_step_x = 0;
  int init_phase_y = 0;
  int phase_step_y = 0;
  HWPixelExtension left;
  HWPixelExtension top;
  HWPixelExtension right;
  HWPixelExtension bottom;
  uint32_t roi_width = 0;
};

struct ScaleData {
  uint8_t enable_pixel_ext;
  uint32_t src_width = 0;
  uint32_t src_height = 0;
  HWPlane plane[4];
};

struct HWPipeInfo {
  uint32_t pipe_id = 0;
  LayerRect src_roi;
  LayerRect dst_roi;
  uint8_t horizontal_decimation = 0;
  uint8_t vertical_decimation = 0;
  ScaleData scale_data;
  uint32_t z_order = 0;
  bool set_igc = false;
  bool valid = false;

  void Reset() { *this = HWPipeInfo(); }
};

struct HWLayerConfig {
  HWPipeInfo left_pipe;           // pipe for left side of output
  HWPipeInfo right_pipe;          // pipe for right side of output
  HWRotatorSession hw_rotator_session;
  float compression = 1.0f;

  void Reset() { *this = HWLayerConfig(); }
};

struct HWLayersInfo {
  LayerStack *stack = NULL;        // Input layer stack. Set by the caller.

  uint32_t index[kMaxSDELayers];   // Indexes of the layers from the layer stack which need to be
                                   // programmed on hardware.
  LayerRect updated_src_rect[kMaxSDELayers];  // Updated layer src rects in s3d mode
  LayerRect updated_dst_rect[kMaxSDELayers];  // Updated layer dst rects in s3d mode

  uint32_t count = 0;              // Total number of layers which need to be set on hardware.

  int sync_handle = -1;

  LayerRect left_partial_update;   // Left ROI.
  LayerRect right_partial_update;  // Right ROI.

  bool use_hw_cursor = false;      // Indicates that HWCursor pipe needs to be used for cursor layer
};

struct HWLayers {
  HWLayersInfo info;
  HWLayerConfig config[kMaxSDELayers];
  float output_compression = 1.0f;
  uint32_t bandwidth = 0;
  uint32_t clock = 0;
};

struct HWDisplayAttributes : DisplayConfigVariableInfo {
  bool is_device_split = false;
  uint32_t split_left = 0;
  uint32_t v_front_porch = 0;  //!< Vertical front porch of panel
  uint32_t v_back_porch = 0;   //!< Vertical back porch of panel
  uint32_t v_pulse_width = 0;  //!< Vertical pulse width of panel
  uint32_t h_total = 0;        //!< Total width of panel (hActive + hFP + hBP + hPulseWidth)
  uint32_t s3d_config = 0;     //!< Stores the bit mask of S3D modes

  void Reset() { *this = HWDisplayAttributes(); }

  bool operator !=(const HWDisplayAttributes &attributes) {
    return ((is_device_split != attributes.is_device_split) ||
            (split_left != attributes.split_left) ||
            (x_pixels != attributes.x_pixels) || (y_pixels != attributes.y_pixels) ||
            (x_dpi != attributes.x_dpi) || (y_dpi != attributes.y_dpi) || (fps != attributes.fps) ||
            (vsync_period_ns != attributes.vsync_period_ns) ||
            (v_front_porch != attributes.v_front_porch) ||
            (v_back_porch != attributes.v_back_porch) ||
            (v_pulse_width != attributes.v_pulse_width));
  }

  bool operator ==(const HWDisplayAttributes &attributes) {
    return !(operator !=(attributes));
  }
};

}  // namespace sdm

#endif  // __HW_INFO_TYPES_H__

