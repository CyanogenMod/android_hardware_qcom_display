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

#define __CLASS__ "HWInfo"

namespace sdm {

int HWInfo::ParseLine(char *input, char *tokens[], const uint32_t max_token, uint32_t *count) {
  char *tmp_token = NULL;
  char *temp_ptr;
  uint32_t index = 0;
  const char *delim = ", =\n";
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

DisplayError HWInfo::GetHWResourceInfo(HWResourceInfo *hw_resource) {
  if (!hw_resource) {
    DLOGE("HWResourceInfo pointer in invalid.");
    return kErrorParameters;
  }
  const char *kHWCapabilitiesPath = "/sys/devices/virtual/graphics/fb";
  FILE *fileptr = NULL;
  char stringbuffer[kMaxStringLength];
  uint32_t token_count = 0;
  const uint32_t max_count = 10;
  char *tokens[max_count] = { NULL };
  snprintf(stringbuffer , sizeof(stringbuffer), "%s%d/mdp/caps",
           kHWCapabilitiesPath, kHWCapabilitiesNode);
  fileptr = fopen(stringbuffer, "rb");

  if (!fileptr) {
    DLOGE("File '%s' not found", stringbuffer);
    return kErrorHardware;
  }

  size_t len = kMaxStringLength;
  ssize_t read;
  char *line = stringbuffer;
  hw_resource->hw_version = kHWMdssVersion5;
  while ((read = getline(&line, &len, fileptr)) != -1) {
    // parse the line and update information accordingly
    if (!ParseLine(line, tokens, max_count, &token_count)) {
      if (!strncmp(tokens[0], "hw_rev", strlen("hw_rev"))) {
        hw_resource->hw_revision = atoi(tokens[1]);  // HW Rev, v1/v2
      } else if (!strncmp(tokens[0], "rgb_pipes", strlen("rgb_pipes"))) {
        hw_resource->num_rgb_pipe = UINT8(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "vig_pipes", strlen("vig_pipes"))) {
        hw_resource->num_vig_pipe = UINT8(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "dma_pipes", strlen("dma_pipes"))) {
        hw_resource->num_dma_pipe = UINT8(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "cursor_pipes", strlen("cursor_pipes"))) {
        hw_resource->num_cursor_pipe = UINT8(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "blending_stages", strlen("blending_stages"))) {
        hw_resource->num_blending_stages = UINT8(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_downscale_ratio", strlen("max_downscale_ratio"))) {
        hw_resource->max_scale_down = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_upscale_ratio", strlen("max_upscale_ratio"))) {
        hw_resource->max_scale_up = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_bandwidth_low", strlen("max_bandwidth_low"))) {
        hw_resource->max_bandwidth_low = atol(tokens[1]);
      } else if (!strncmp(tokens[0], "max_bandwidth_high", strlen("max_bandwidth_high"))) {
        hw_resource->max_bandwidth_high = atol(tokens[1]);
      } else if (!strncmp(tokens[0], "max_mixer_width", strlen("max_mixer_width"))) {
        hw_resource->max_mixer_width = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_pipe_width", strlen("max_pipe_width"))) {
        hw_resource->max_pipe_width = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_pipe_bw", strlen("max_pipe_bw"))) {
        hw_resource->max_pipe_bw = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "max_mdp_clk", strlen("max_mdp_clk"))) {
        hw_resource->max_sde_clk = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "clk_fudge_factor", strlen("clk_fudge_factor"))) {
        hw_resource->clk_fudge_factor = FLOAT(atoi(tokens[1])) / FLOAT(atoi(tokens[2]));
      } else if (!strncmp(tokens[0], "fmt_mt_nv12_factor", strlen("fmt_mt_nv12_factor"))) {
        hw_resource->macrotile_nv12_factor = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "fmt_mt_factor", strlen("fmt_mt_factor"))) {
        hw_resource->macrotile_factor = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "fmt_linear_factor", strlen("fmt_linear_factor"))) {
        hw_resource->linear_factor = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "scale_factor", strlen("scale_factor"))) {
        hw_resource->scale_factor = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "xtra_ff_factor", strlen("xtra_ff_factor"))) {
        hw_resource->extra_fudge_factor = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "features", strlen("features"))) {
        for (uint32_t i = 0; i < token_count; i++) {
          if (!strncmp(tokens[i], "bwc", strlen("bwc"))) {
            hw_resource->has_bwc = true;
          } else if (!strncmp(tokens[i], "decimation", strlen("decimation"))) {
            hw_resource->has_decimation = true;
          } else if (!strncmp(tokens[i], "tile_format", strlen("tile_format"))) {
            hw_resource->has_macrotile = true;
          } else if (!strncmp(tokens[i], "src_split", strlen("src_split"))) {
            hw_resource->is_src_split = true;
          } else if (!strncmp(tokens[i], "non_scalar_rgb", strlen("non_scalar_rgb"))) {
            hw_resource->has_non_scalar_rgb = true;
          } else if (!strncmp(tokens[i], "rotator_downscale", strlen("rotator_downscale"))) {
            hw_resource->has_rotator_downscale = true;
          }
        }
      }
    }
  }
  fclose(fileptr);

  DLOGI("SDE Version = %d, SDE Revision = %x, RGB = %d, VIG = %d, DMA = %d, Cursor = %d",
        hw_resource->hw_version, hw_resource->hw_revision, hw_resource->num_rgb_pipe,
        hw_resource->num_vig_pipe, hw_resource->num_dma_pipe, hw_resource->num_cursor_pipe);
  DLOGI("Upscale Ratio = %d, Downscale Ratio = %d, Blending Stages = %d", hw_resource->max_scale_up,
        hw_resource->max_scale_down, hw_resource->num_blending_stages);
  DLOGI("BWC = %d, Decimation = %d, Tile Format = %d, Rotator Downscale = %d", hw_resource->has_bwc,
        hw_resource->has_decimation, hw_resource->has_macrotile,
        hw_resource->has_rotator_downscale);
  DLOGI("SourceSplit = %d", hw_resource->is_src_split);
  DLOGI("MaxLowBw = %"PRIu64", MaxHighBw = %"PRIu64"", hw_resource->max_bandwidth_low,
        hw_resource->max_bandwidth_high);
  DLOGI("MaxPipeBw = %"PRIu64" KBps, MaxSDEClock = %"PRIu64" Hz, ClockFudgeFactor = %f",
        hw_resource->max_pipe_bw, hw_resource->max_sde_clk, hw_resource->clk_fudge_factor);
  DLOGI("Prefill factors: Tiled_NV12 = %d, Tiled = %d, Linear = %d, Scale = %d, Fudge_factor = %d",
        hw_resource->macrotile_nv12_factor, hw_resource->macrotile_factor,
        hw_resource->linear_factor, hw_resource->scale_factor, hw_resource->extra_fudge_factor);

  return kErrorNone;
}

}  // namespace sdm

