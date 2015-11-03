/*
 * Copyright (c) 2013,2016 The Linux Foundation. All rights reserved.

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
#define QD_UTILS_DEBUG 0
#define TOKEN_PARAMS_DELIM  "="

namespace qdutils {

static int tokenizeParams(char *inputParams, const char *delim,
        char* tokenStr[], int *idx) {
    char *tmp_token = NULL;
    char *temp_ptr;
    int index = 0;
    if (!inputParams) {
        return -1;
    }
    tmp_token = strtok_r(inputParams, delim, &temp_ptr);
    while (tmp_token != NULL) {
        tokenStr[index++] = tmp_token;
        tmp_token = strtok_r(NULL, " ", &temp_ptr);
    }
    *idx = index;
    return 0;
}

int getPluggableNode() {
    FILE *panelInfoNodeFP = NULL;
    int pluggableNode = -1;
    char msmFbTypePath[MAX_FRAME_BUFFER_NAME_SIZE];

    for(int j = 0; j < HWC_NUM_DISPLAY_TYPES; j++) {
        snprintf (msmFbTypePath, sizeof(msmFbTypePath),
                "/sys/class/graphics/fb%d/msm_fb_panel_info", j);
        panelInfoNodeFP = fopen(msmFbTypePath, "r");
        if(panelInfoNodeFP){
            size_t len = PAGE_SIZE;
            ssize_t read;
            char *readLine = (char *) malloc (len);
            while((read = getline((char **)&readLine, &len,
                            panelInfoNodeFP)) != -1) {
                int token_ct = 0;
                char *tokens[10];
                memset(tokens, 0, sizeof(tokens));

                if(!tokenizeParams(readLine, TOKEN_PARAMS_DELIM, tokens,
                            &token_ct)) {
                    if(!strncmp(tokens[0], "is_pluggable",
                                strlen("is_pluggable"))) {
                        if (atoi(tokens[1]) == 1) {
                            pluggableNode = j;
                            break;
                        }
                    }
                }
            }
            fclose(panelInfoNodeFP);
            free(readLine);
        }
    }

    if (pluggableNode == -1) {
        ALOGE("%s: Failed to find HDMI node.", __FUNCTION__);
    }

    return pluggableNode;
}

int getEdidRawData(char *buffer)
{
    int size;
    int edidFile;
    char msmFbTypePath[MAX_FRAME_BUFFER_NAME_SIZE];
    int node_id = getPluggableNode();

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

/* Calculates the aspect ratio for based on src & dest */
void getAspectRatioPosition(int destWidth, int destHeight, int srcWidth,
                                int srcHeight, hwc_rect_t& rect) {
   int x =0, y =0;

   if (srcWidth * destHeight > destWidth * srcHeight) {
        srcHeight = destWidth * srcHeight / srcWidth;
        srcWidth = destWidth;
    } else if (srcWidth * destHeight < destWidth * srcHeight) {
        srcWidth = destHeight * srcWidth / srcHeight;
        srcHeight = destHeight;
    } else {
        srcWidth = destWidth;
        srcHeight = destHeight;
    }
    if (srcWidth > destWidth) srcWidth = destWidth;
    if (srcHeight > destHeight) srcHeight = destHeight;
    x = (destWidth - srcWidth) / 2;
    y = (destHeight - srcHeight) / 2;
    ALOGD_IF(QD_UTILS_DEBUG, "%s: AS Position: x = %d, y = %d w = %d h = %d",
             __FUNCTION__, x, y, srcWidth , srcHeight);
    // Convert it back to hwc_rect_t
    rect.left = x;
    rect.top = y;
    rect.right = srcWidth + rect.left;
    rect.bottom = srcHeight + rect.top;
}

}; //namespace qdutils
