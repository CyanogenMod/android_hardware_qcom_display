/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

/*! @file hw_info_types.h
    @brief Definitions for types that contain information pertaining to hardware devices.
*/

#ifndef __HW_INFO_TYPES_H__
#define __HW_INFO_TYPES_H__

#include <stdint.h>
#include <core/sde_types.h>

namespace sde {

/*! @brief This enumeration holds the possible hardware device types. */
enum HWDeviceType {
  kDevicePrimary,
  kDeviceHDMI,
  kDeviceVirtual,
  kDeviceRotator,
  kDeviceMax,
};

/*! @brief This structure is a representation of hardware capabilities of the target device display
  driver.
*/
struct HWResourceInfo {
  uint32_t hw_version;
  uint32_t hw_revision;
  uint32_t num_dma_pipe;
  uint32_t num_vig_pipe;
  uint32_t num_rgb_pipe;
  uint32_t num_cursor_pipe;
  uint32_t num_blending_stages;
  uint32_t num_rotator;
  uint32_t num_control;
  uint32_t num_mixer_to_disp;
  uint32_t smp_total;
  uint32_t smp_size;
  uint32_t num_smp_per_pipe;
  uint32_t max_scale_up;
  uint32_t max_scale_down;
  uint64_t max_bandwidth_low;
  uint64_t max_bandwidth_high;
  uint32_t max_mixer_width;
  uint32_t max_pipe_width;
  uint32_t max_pipe_bw;
  uint32_t max_sde_clk;
  float clk_fudge_factor;
  bool has_bwc;
  bool has_ubwc;
  bool has_decimation;
  bool has_macrotile;
  bool has_rotator_downscale;
  bool has_non_scalar_rgb;
  bool is_src_split;

  HWResourceInfo()
    : hw_version(0), hw_revision(0), num_dma_pipe(0), num_vig_pipe(0), num_rgb_pipe(0),
      num_cursor_pipe(0), num_blending_stages(0), num_rotator(0), num_control(0),
      num_mixer_to_disp(0), smp_total(0), smp_size(0), num_smp_per_pipe(0), max_scale_up(1),
      max_scale_down(1), max_bandwidth_low(0), max_bandwidth_high(0), max_mixer_width(2048),
      max_pipe_width(2048), max_pipe_bw(0), max_sde_clk(0), clk_fudge_factor(1.0f), has_bwc(false),
      has_ubwc(false), has_decimation(false), has_macrotile(false), has_rotator_downscale(false),
      has_non_scalar_rgb(false), is_src_split(false) { }

  void Reset() { *this = HWResourceInfo(); }
};

/*! @brief This enumeration holds the possible display modes. */
enum HWDisplayMode {
  kModeDefault,
  kModeVideo,
  kModeCommand,
};

/*! @brief This enumeration holds the all possible display port types. */
enum HWDisplayPort {
  kPortDefault,
  kPortDSI,
  kPortDTv,
  kPortWriteBack,
  kPortLVDS,
  kPortEDP,
};

/*! @brief This structure describes the split configuration of a display panel. */
struct HWSplitInfo {
  uint32_t left_split;
  uint32_t right_split;
  bool always_src_split;

  HWSplitInfo() : left_split(0), right_split(0), always_src_split(false) { }

  bool operator !=(const HWSplitInfo &split_info) {
    return ((left_split != split_info.left_split) || (right_split != split_info.right_split) ||
            (always_src_split != split_info.always_src_split));
  }

  bool operator ==(const HWSplitInfo &split_info) {
    return !(operator !=(split_info));
  }
};

/*! @brief This structure describes properties of a display panel. */
struct HWPanelInfo {
  HWDisplayPort port;      //!< Display port
  HWDisplayMode mode;      //!< Display mode
  bool partial_update;     //!< Partial update feature
  int left_align;          //!< ROI left alignment restriction
  int width_align;         //!< ROI width alignment restriction
  int top_align;;          //!< ROI top alignment restriction
  int height_align;        //!< ROI height alignment restriction
  int min_roi_width;       //!< Min width needed for ROI
  int min_roi_height;      //!< Min height needed for ROI
  bool needs_roi_merge;    //!< Merge ROI's of both the DSI's
  bool dynamic_fps;        //!< Panel Supports dynamic fps
  uint32_t min_fps;        //!< Min fps supported by panel
  uint32_t max_fps;        //!< Max fps supported by panel
  bool is_primary_panel;   //!< Panel is primary display
  HWSplitInfo split_info;  //!< Panel split configuration

  HWPanelInfo() : port(kPortDefault), mode(kModeDefault), partial_update(false), left_align(false),
    width_align(false), top_align(false), height_align(false), min_roi_width(0), min_roi_height(0),
    needs_roi_merge(false), dynamic_fps(false), min_fps(0), max_fps(0) { }

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
            (split_info != panel_info.split_info));
  }

  bool operator ==(const HWPanelInfo &panel_info) {
    return !(operator !=(panel_info));
  }
};

}  // namespace sde

#endif  // __HW_INFO_TYPES_H__

