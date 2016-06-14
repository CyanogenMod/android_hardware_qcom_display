/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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

#include "qd_utils.h"

namespace qdutils {

int parseLine(char *input, char *tokens[], const uint32_t maxToken, uint32_t *count) {
    char *tmpToken = NULL;
    char *tmpPtr;
    uint32_t index = 0;
    const char *delim = ", =\n";
    if (!input) {
      return -1;
    }
    tmpToken = strtok_r(input, delim, &tmpPtr);
    while (tmpToken && index < maxToken) {
      tokens[index++] = tmpToken;
      tmpToken = strtok_r(NULL, delim, &tmpPtr);
    }
    *count = index;

    return 0;
}

int querySDEInfo(HWQueryType type, int *value) {
    FILE *fileptr = NULL;
    const char *featureName;
    char stringBuffer[MAX_STRING_LENGTH];
    uint32_t tokenCount = 0;
    const uint32_t maxCount = 10;
    char *tokens[maxCount] = { NULL };

    switch(type) {
    case HAS_MACRO_TILE:
        featureName = "tile_format";
        break;

    case HAS_UBWC:
        featureName = "ubwc";
        break;

    default:
        ALOGE("Invalid query type %d", type);
        return -EINVAL;
    }

    fileptr = fopen("/sys/devices/virtual/graphics/fb0/mdp/caps", "rb");
    if (!fileptr) {
        ALOGE("File '%s' not found", stringBuffer);
        return -EINVAL;
    }

    size_t len = MAX_STRING_LENGTH;
    ssize_t read;
    char *line = stringBuffer;
    while ((read = getline(&line, &len, fileptr)) != -1) {
        // parse the line and update information accordingly
        if (parseLine(line, tokens, maxCount, &tokenCount)) {
            continue;
        }

        if (strncmp(tokens[0], "features", strlen("features"))) {
            continue;
        }

        for (uint32_t i = 0; i < tokenCount; i++) {
            if (!strncmp(tokens[i], featureName, strlen(featureName))) {
              *value = 1;
            }
        }
    }
    fclose(fileptr);

    return 0;
}

int getHDMINode(void)
{
    FILE *displayDeviceFP = NULL;
    char fbType[MAX_FRAME_BUFFER_NAME_SIZE];
    char msmFbTypePath[MAX_FRAME_BUFFER_NAME_SIZE];
    int j = 0;

    for(j = 0; j < HWC_NUM_DISPLAY_TYPES; j++) {
        snprintf (msmFbTypePath, sizeof(msmFbTypePath),
                  "/sys/class/graphics/fb%d/msm_fb_type", j);
        displayDeviceFP = fopen(msmFbTypePath, "r");
        if(displayDeviceFP) {
            fread(fbType, sizeof(char), MAX_FRAME_BUFFER_NAME_SIZE,
                    displayDeviceFP);
            if(strncmp(fbType, "dtv panel", strlen("dtv panel")) == 0) {
                ALOGD("%s: HDMI is at fb%d", __func__, j);
                fclose(displayDeviceFP);
                break;
            }
            fclose(displayDeviceFP);
        } else {
            ALOGE("%s: Failed to open fb node %d", __func__, j);
        }
    }

    if (j < HWC_NUM_DISPLAY_TYPES)
        return j;
    else
        ALOGE("%s: Failed to find HDMI node", __func__);

    return -1;
}

int getEdidRawData(char *buffer)
{
    int size;
    int edidFile;
    char msmFbTypePath[MAX_FRAME_BUFFER_NAME_SIZE];
    int node_id = getHDMINode();

    if (node_id < 0) {
        ALOGE("%s no HDMI node found", __func__);
        return 0;
    }

    snprintf(msmFbTypePath, sizeof(msmFbTypePath),
                 "/sys/class/graphics/fb%d/edid_raw_data", node_id);

    edidFile = open(msmFbTypePath, O_RDONLY, 0);

    if (edidFile < 0) {
        ALOGE("%s no edid raw data found", __func__);
        return 0;
    }

    size = (int)read(edidFile, (char*)buffer, EDID_RAW_DATA_SIZE);
    close(edidFile);
    return size;
}

}; //namespace qdutils
