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

#include "hw_info.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/sys.h>

#define __CLASS__ "HWInfo"

namespace sdm {

int HWInfo::ParseLine(char *input, char *tokens[], const uint32_t max_token, uint32_t *count) {
  char *tmp_token = NULL;
  char *temp_ptr;
  uint32_t index = 0;
  const char *delim = ":, =\n";
  if (!input) {
    return -1;
  }
  tmp_token = strtok_r(input, delim, &temp_ptr);
  while (tmp_token && index < max_token) {
    tokens[index++] = tmp_token;
    tmp_token = strtok_r(NULL, delim, &temp_ptr);
  }
  *count = index;

  return 0;
}

DisplayError HWInfoInterface::Create(HWInfoInterface **intf) {
  DisplayError error = kErrorNone;
  HWInfo *hw_info = NULL;

  hw_info = new HWInfo();
  if (!hw_info) {
    error = kErrorMemory;
  } else {
    *intf = hw_info;
  }

  return error;
}

DisplayError HWInfoInterface::Destroy(HWInfoInterface *intf) {
  HWInfo *hw_info = static_cast<HWInfo *>(intf);
  delete hw_info;

  return kErrorNone;
}

DisplayError HWInfo::GetDynamicBWLimits(HWResourceInfo *hw_resource) {
  const char *bw_info_node = "/sys/devices/virtual/graphics/fb0/mdp/bw_mode_bitmap";
  FILE *fileptr = NULL;
  uint32_t token_count = 0;
  const uint32_t max_count = kBwModeMax;
  char *tokens[max_count] = { NULL };
  fileptr = Sys::fopen_(bw_info_node, "r");

  if (!fileptr) {
    DLOGE("File '%s' not found", bw_info_node);
    return kErrorHardware;
  }

  HWDynBwLimitInfo* bw_info = &hw_resource->dyn_bw_info;
  for (int index = 0; index < kBwModeMax; index++) {
    bw_info->total_bw_limit[index] = hw_resource->max_bandwidth_low;
    bw_info->pipe_bw_limit[index] = hw_resource->max_pipe_bw;
  }

  char *stringbuffer = reinterpret_cast<char *>(malloc(kMaxStringLength));
  if (stringbuffer == NULL) {
    DLOGE("Failed to allocate stringbuffer");
    return kErrorMemory;
  }

  size_t len = kMaxStringLength;
  ssize_t read;
  char *line = stringbuffer;
  while ((read = Sys::getline_(&line, &len, fileptr)) != -1) {
    if (!ParseLine(line, tokens, max_count, &token_count)) {
      if (!strncmp(tokens[0], "default_pipe", strlen("default_pipe"))) {
        bw_info->pipe_bw_limit[kBwDefault] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "camera_pipe", strlen("camera_pipe"))) {
        bw_info->pipe_bw_limit[kBwCamera] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "vflip_pipe", strlen("vflip_pipe"))) {
        bw_info->pipe_bw_limit[kBwVFlip] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "hflip_pipe", strlen("hflip_pipe"))) {
        bw_info->pipe_bw_limit[kBwHFlip] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "default", strlen("default"))) {
        bw_info->total_bw_limit[kBwDefault] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "camera", strlen("camera"))) {
        bw_info->total_bw_limit[kBwCamera] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "vflip", strlen("vflip"))) {
        bw_info->total_bw_limit[kBwVFlip] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "hflip", strlen("hflip"))) {
        bw_info->total_bw_limit[kBwHFlip] = UINT32(atoi(tokens[1]));
      }
    }
  }
  free(stringbuffer);
  Sys::fclose_(fileptr);

  return kErrorNone;
}

DisplayError HWInfo::GetHWResourceInfo(HWResourceInfo *hw_resource) {
  if (!hw_resource) {
    DLOGE("HWResourceInfo pointer in invalid.");
    return kErrorParameters;
  }
  const char *fb_path = "/sys/devices/virtual/graphics/fb";
  FILE *fileptr = NULL;
  uint32_t token_count = 0;
  const uint32_t max_count = 10;
  char *tokens[max_count] = { NULL };
  char *stringbuffer = reinterpret_cast<char *>(malloc(kMaxStringLength));

  if (stringbuffer == NULL) {
    DLOGE("Failed to allocate stringbuffer");
    return kErrorMemory;
  }

  snprintf(stringbuffer , kMaxStringLength, "%s%d/mdp/caps", fb_path, kHWCapabilitiesNode);
  fileptr = Sys::fopen_(stringbuffer, "r");

  if (!fileptr) {
    DLOGE("File '%s' not found", stringbuffer);
    free(stringbuffer);
    return kErrorHardware;
  }

  size_t len = kMaxStringLength;
  ssize_t read;
  char *line = stringbuffer;
  hw_resource->hw_version = kHWMdssVersion5;
  while ((read = Sys::getline_(&line, &len, fileptr)) != -1) {
    // parse the line and update information accordingly
    if (!ParseLine(line, tokens, max_count, &token_count)) {
      if (!strncmp(tokens[0], "hw_rev", strlen("hw_rev"))) {
        hw_resource->hw_revision = UINT32(atoi(tokens[1]));  // HW Rev, v1/v2
      } else if (!strncmp(tokens[0], "blending_stages", strlen("blending_stages"))) {
        hw_resource->num_blending_stages = UINT8(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_downscale_ratio", strlen("max_downscale_ratio"))) {
        hw_resource->max_scale_down = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_upscale_ratio", strlen("max_upscale_ratio"))) {
        hw_resource->max_scale_up = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_bandwidth_low", strlen("max_bandwidth_low"))) {
        hw_resource->max_bandwidth_low = UINT64(atol(tokens[1]));
      } else if (!strncmp(tokens[0], "max_bandwidth_high", strlen("max_bandwidth_high"))) {
        hw_resource->max_bandwidth_high = UINT64(atol(tokens[1]));
      } else if (!strncmp(tokens[0], "max_mixer_width", strlen("max_mixer_width"))) {
        hw_resource->max_mixer_width = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_pipe_width", strlen("max_pipe_width"))) {
        hw_resource->max_pipe_width = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_cursor_size", strlen("max_cursor_size"))) {
        hw_resource->max_cursor_size = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_pipe_bw", strlen("max_pipe_bw"))) {
        hw_resource->max_pipe_bw = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_mdp_clk", strlen("max_mdp_clk"))) {
        hw_resource->max_sde_clk = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "clk_fudge_factor", strlen("clk_fudge_factor"))) {
        hw_resource->clk_fudge_factor = FLOAT(atoi(tokens[1])) / FLOAT(atoi(tokens[2]));
      } else if (!strncmp(tokens[0], "fmt_mt_nv12_factor", strlen("fmt_mt_nv12_factor"))) {
        hw_resource->macrotile_nv12_factor = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "fmt_mt_factor", strlen("fmt_mt_factor"))) {
        hw_resource->macrotile_factor = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "fmt_linear_factor", strlen("fmt_linear_factor"))) {
        hw_resource->linear_factor = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "scale_factor", strlen("scale_factor"))) {
        hw_resource->scale_factor = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "xtra_ff_factor", strlen("xtra_ff_factor"))) {
        hw_resource->extra_fudge_factor = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "features", strlen("features"))) {
        for (uint32_t i = 0; i < token_count; i++) {
          if (!strncmp(tokens[i], "bwc", strlen("bwc"))) {
            hw_resource->has_bwc = true;
          } else if (!strncmp(tokens[i], "ubwc", strlen("ubwc"))) {
            hw_resource->has_ubwc = true;
          } else if (!strncmp(tokens[i], "decimation", strlen("decimation"))) {
            hw_resource->has_decimation = true;
          } else if (!strncmp(tokens[i], "tile_format", strlen("tile_format"))) {
            hw_resource->has_macrotile = true;
          } else if (!strncmp(tokens[i], "src_split", strlen("src_split"))) {
            hw_resource->is_src_split = true;
          } else if (!strncmp(tokens[i], "non_scalar_rgb", strlen("non_scalar_rgb"))) {
            hw_resource->has_non_scalar_rgb = true;
          } else if (!strncmp(tokens[i], "perf_calc", strlen("perf_calc"))) {
            hw_resource->perf_calc = true;
          } else if (!strncmp(tokens[i], "dynamic_bw_limit", strlen("dynamic_bw_limit"))) {
            hw_resource->has_dyn_bw_support = true;
          }
        }
      } else if (!strncmp(tokens[0], "pipe_count", strlen("pipe_count"))) {
        uint32_t pipe_count = UINT8(atoi(tokens[1]));
        for (uint32_t i = 0; i < pipe_count; i++) {
          read = Sys::getline_(&line, &len, fileptr);
          if (!ParseLine(line, tokens, max_count, &token_count)) {
            HWPipeCaps pipe_caps;
            for (uint32_t j = 0; j < token_count; j += 2) {
              if (!strncmp(tokens[j], "pipe_type", strlen("pipe_type"))) {
                if (!strncmp(tokens[j+1], "vig", strlen("vig"))) {
                  pipe_caps.type = kPipeTypeVIG;
                  hw_resource->num_vig_pipe++;
                } else if (!strncmp(tokens[j+1], "rgb", strlen("rgb"))) {
                  pipe_caps.type = kPipeTypeRGB;
                  hw_resource->num_rgb_pipe++;
                } else if (!strncmp(tokens[j+1], "dma", strlen("dma"))) {
                  pipe_caps.type = kPipeTypeDMA;
                  hw_resource->num_dma_pipe++;
                } else if (!strncmp(tokens[j+1], "cursor", strlen("cursor"))) {
                  pipe_caps.type = kPipeTypeCursor;
                  hw_resource->num_cursor_pipe++;
                }
              } else if (!strncmp(tokens[j], "pipe_ndx", strlen("pipe_ndx"))) {
                pipe_caps.id = UINT32(atoi(tokens[j+1]));
              }
            }
            hw_resource->hw_pipes.push_back(pipe_caps);
          }
        }
      }
    }
  }

  Sys::fclose_(fileptr);

  DLOGI("SDE Version = %d, SDE Revision = %x, RGB = %d, VIG = %d, DMA = %d, Cursor = %d",
        hw_resource->hw_version, hw_resource->hw_revision, hw_resource->num_rgb_pipe,
        hw_resource->num_vig_pipe, hw_resource->num_dma_pipe, hw_resource->num_cursor_pipe);
  DLOGI("Upscale Ratio = %d, Downscale Ratio = %d, Blending Stages = %d", hw_resource->max_scale_up,
        hw_resource->max_scale_down, hw_resource->num_blending_stages);
  DLOGI("BWC = %d, UBWC = %d, Decimation = %d, Tile Format = %d", hw_resource->has_bwc,
        hw_resource->has_ubwc, hw_resource->has_decimation, hw_resource->has_macrotile);
  DLOGI("SourceSplit = %d", hw_resource->is_src_split);
  DLOGI("MaxLowBw = %" PRIu64 " , MaxHighBw = % " PRIu64 "", hw_resource->max_bandwidth_low,
        hw_resource->max_bandwidth_high);
  DLOGI("MaxPipeBw = %" PRIu64 " KBps, MaxSDEClock = % " PRIu64 " Hz, ClockFudgeFactor = %f",
        hw_resource->max_pipe_bw, hw_resource->max_sde_clk, hw_resource->clk_fudge_factor);
  DLOGI("Prefill factors: Tiled_NV12 = %d, Tiled = %d, Linear = %d, Scale = %d, Fudge_factor = %d",
        hw_resource->macrotile_nv12_factor, hw_resource->macrotile_factor,
        hw_resource->linear_factor, hw_resource->scale_factor, hw_resource->extra_fudge_factor);

  const char *rotator_caps_path = "/sys/devices/virtual/rotator/mdss_rotator/caps";
  snprintf(stringbuffer , kMaxStringLength, "%s", rotator_caps_path);
  fileptr = Sys::fopen_(stringbuffer, "r");

  if (!fileptr) {
    DLOGW("File '%s' not found", stringbuffer);
    free(stringbuffer);
    return kErrorNone;
  }

  while ((read = Sys::getline_(&line, &len, fileptr)) != -1) {
    if (!ParseLine(line, tokens, max_count, &token_count)) {
      if (!strncmp(tokens[0], "wb_count", strlen("wb_count"))) {
        hw_resource->num_rotator = UINT8(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "downscale", strlen("downscale"))) {
        hw_resource->has_rotator_downscale = UINT8(atoi(tokens[1]));
      }
    }
  }

  free(stringbuffer);
  Sys::fclose_(fileptr);

  DLOGI("ROTATOR = %d, Rotator Downscale = %d", hw_resource->num_rotator,
        hw_resource->has_rotator_downscale);

  if (hw_resource->has_dyn_bw_support) {
    DisplayError ret = GetDynamicBWLimits(hw_resource);
    if (ret != kErrorNone) {
      DLOGE("Failed to read dynamic band width info");
      return ret;
    }

    DLOGI("Has Support for multiple bw limits shown below");
    for (int index = 0; index < kBwModeMax; index++) {
      DLOGI("Mode-index=%d  total_bw_limit=%d and pipe_bw_limit=%d",
            index, hw_resource->dyn_bw_info.total_bw_limit[index],
            hw_resource->dyn_bw_info.pipe_bw_limit[index]);
    }
  }

  return kErrorNone;
}

}  // namespace sdm

