/*
 * Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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
#include <cutils/log.h>
#include <linux/msm_mdp.h>
#include "mdp_version.h"
#include "qd_utils.h"

#define DEBUG 0

ANDROID_SINGLETON_STATIC_INSTANCE(qdutils::MDPVersion);
namespace qdutils {

#define TOKEN_PARAMS_DELIM  "="

#ifndef MDSS_MDP_REV
enum mdp_rev {
    MDSS_MDP_HW_REV_100 = 0x10000000, //8974 v1
    MDSS_MDP_HW_REV_101 = 0x10010000, //8x26
    MDSS_MDP_HW_REV_102 = 0x10020000, //8974 v2
    MDSS_MDP_HW_REV_103 = 0x10030000, //8084
    MDSS_MDP_HW_REV_104 = 0x10040000, //Next version
    MDSS_MDP_HW_REV_105 = 0x10050000, //Next version
    MDSS_MDP_HW_REV_107 = 0x10070000, //Next version
    MDSS_MDP_HW_REV_200 = 0x20000000, //8092
    MDSS_MDP_HW_REV_206 = 0x20060000, //Future
};
#else
enum mdp_rev {
    MDSS_MDP_HW_REV_104 = 0x10040000, //Next version
    MDSS_MDP_HW_REV_206 = 0x20060000, //Future
};
#endif

MDPVersion::MDPVersion()
{
    mMDPVersion = MDSS_V5;
    mMdpRev = 0;
    mRGBPipes = 0;
    mVGPipes = 0;
    mDMAPipes = 0;
    mFeatures = 0;
    mMDPUpscale = 0;
    mMDPDownscale = 0;
    mLowBw = 0;
    mHighBw = 0;

    updatePanelInfo();

    if(!updateSysFsInfo()) {
        ALOGE("Unable to read display sysfs node");
    }
    if (mMdpRev == MDP_V3_0_4){
        mMDPVersion = MDP_V3_0_4;
    }

    mHasOverlay = false;
    if((mMDPVersion >= MDP_V4_0) ||
       (mMDPVersion == MDP_V_UNKNOWN) ||
       (mMDPVersion == MDP_V3_0_4))
        mHasOverlay = true;
    if(!updateSplitInfo()) {
        ALOGE("Unable to read display split node");
    }
}

MDPVersion::~MDPVersion() {
    close(mFd);
}

int MDPVersion::tokenizeParams(char *inputParams, const char *delim,
                                char* tokenStr[], int *idx) {
    char *tmp_token = NULL;
    char *temp_ptr;
    int ret = 0, index = 0;
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
// This function reads the sysfs node to read the primary panel type
// and updates information accordingly
void  MDPVersion::updatePanelInfo() {
    FILE *displayDeviceFP = NULL;
    FILE *panelInfoNodeFP = NULL;
    char fbType[MAX_FRAME_BUFFER_NAME_SIZE];
    char panelInfo[MAX_FRAME_BUFFER_NAME_SIZE];
    const char *strCmdPanel = "mipi dsi cmd panel";
    const char *strVideoPanel = "mipi dsi video panel";
    const char *strLVDSPanel = "lvds panel";
    const char *strEDPPanel = "edp panel";

    displayDeviceFP = fopen("/sys/class/graphics/fb0/msm_fb_type", "r");
    if(displayDeviceFP){
        fread(fbType, sizeof(char), MAX_FRAME_BUFFER_NAME_SIZE,
                displayDeviceFP);
        if(strncmp(fbType, strCmdPanel, strlen(strCmdPanel)) == 0) {
            mPanelInfo.mType = MIPI_CMD_PANEL;
        }
        else if(strncmp(fbType, strVideoPanel, strlen(strVideoPanel)) == 0) {
            mPanelInfo.mType = MIPI_VIDEO_PANEL;
        }
        else if(strncmp(fbType, strLVDSPanel, strlen(strLVDSPanel)) == 0) {
            mPanelInfo.mType = LVDS_PANEL;
        }
        else if(strncmp(fbType, strEDPPanel, strlen(strEDPPanel)) == 0) {
            mPanelInfo.mType = EDP_PANEL;
        }
        fclose(displayDeviceFP);
    } else {
        ALOGE("Unable to read Primary Panel Information");
    }

    panelInfoNodeFP = fopen("/sys/class/graphics/fb0/msm_fb_panel_info", "r");
    if(panelInfoNodeFP){
        size_t len = PAGE_SIZE;
        ssize_t read;
        char *readLine = (char *) malloc (len);
        while((read = getline((char **)&readLine, &len,
                              panelInfoNodeFP)) != -1) {
            int token_ct=0;
            char *tokens[10];
            memset(tokens, 0, sizeof(tokens));

            if(!tokenizeParams(readLine, TOKEN_PARAMS_DELIM, tokens,
                               &token_ct)) {
                if(!strncmp(tokens[0], "pu_en", strlen("pu_en"))) {
                    mPanelInfo.mPartialUpdateEnable = atoi(tokens[1]);
                    ALOGI("PartialUpdate status: %s",
                          mPanelInfo.mPartialUpdateEnable? "Enabled" :
                          "Disabled");
                }
                if(!strncmp(tokens[0], "xstart", strlen("xstart"))) {
                    mPanelInfo.mLeftAlign = atoi(tokens[1]);
                    ALOGI("Left Align: %d", mPanelInfo.mLeftAlign);
                }
                if(!strncmp(tokens[0], "walign", strlen("walign"))) {
                    mPanelInfo.mWidthAlign = atoi(tokens[1]);
                    ALOGI("Width Align: %d", mPanelInfo.mWidthAlign);
                }
                if(!strncmp(tokens[0], "ystart", strlen("ystart"))) {
                    mPanelInfo.mTopAlign = atoi(tokens[1]);
                    ALOGI("Top Align: %d", mPanelInfo.mTopAlign);
                }
                if(!strncmp(tokens[0], "halign", strlen("halign"))) {
                    mPanelInfo.mHeightAlign = atoi(tokens[1]);
                    ALOGI("Height Align: %d", mPanelInfo.mHeightAlign);
                }
                if(!strncmp(tokens[0], "min_w", strlen("min_w"))) {
                    mPanelInfo.mMinROIWidth = atoi(tokens[1]);
                    ALOGI("Min ROI Width: %d", mPanelInfo.mMinROIWidth);
                }
                if(!strncmp(tokens[0], "min_h", strlen("min_h"))) {
                    mPanelInfo.mMinROIHeight = atoi(tokens[1]);
                    ALOGI("Min ROI Height: %d", mPanelInfo.mMinROIHeight);
                }
                if(!strncmp(tokens[0], "dyn_fps_en", strlen("dyn_fps_en"))) {
                    mPanelInfo.mDynFpsSupported = atoi(tokens[1]);
                    ALOGI("Dynamic Fps: %s", mPanelInfo.mDynFpsSupported ?
                                            "Enabled" : "Disabled");
                }
                if(!strncmp(tokens[0], "min_fps", strlen("min_fps"))) {
                    mPanelInfo.mMinFps = atoi(tokens[1]);
                    ALOGI("Min Panel fps: %d", mPanelInfo.mMinFps);
                }
                if(!strncmp(tokens[0], "max_fps", strlen("max_fps"))) {
                    mPanelInfo.mMaxFps = atoi(tokens[1]);
                    ALOGI("Max Panel fps: %d", mPanelInfo.mMaxFps);
                }
            }
        }
        fclose(panelInfoNodeFP);
        free(readLine);
    } else {
        ALOGE("Failed to open msm_fb_panel_info node");
    }
}

// This function reads the sysfs node to read MDP capabilities
// and parses and updates information accordingly.
bool MDPVersion::updateSysFsInfo() {
    FILE *sysfsFd;
    size_t len = PAGE_SIZE;
    ssize_t read;
    char *line = NULL;
    char sysfsPath[255];
    memset(sysfsPath, 0, sizeof(sysfsPath));
    snprintf(sysfsPath , sizeof(sysfsPath),
            "/sys/class/graphics/fb0/mdp/caps");

    sysfsFd = fopen(sysfsPath, "rb");

    if (sysfsFd == NULL) {
        ALOGE("%s: sysFsFile file '%s' not found",
                __FUNCTION__, sysfsPath);
        return false;
    } else {
        line = (char *) malloc(len);
        while((read = getline(&line, &len, sysfsFd)) != -1) {
            int index=0;
            char *tokens[10];
            memset(tokens, 0, sizeof(tokens));

            // parse the line and update information accordingly
            if(!tokenizeParams(line, TOKEN_PARAMS_DELIM, tokens, &index)) {
                if(!strncmp(tokens[0], "hw_rev", strlen("hw_rev"))) {
                    mMdpRev = atoi(tokens[1]);
                }
                else if(!strncmp(tokens[0], "rgb_pipes", strlen("rgb_pipes"))) {
                    mRGBPipes = atoi(tokens[1]);
                }
                else if(!strncmp(tokens[0], "vig_pipes", strlen("vig_pipes"))) {
                    mVGPipes = atoi(tokens[1]);
                }
                else if(!strncmp(tokens[0], "dma_pipes", strlen("dma_pipes"))) {
                    mDMAPipes = atoi(tokens[1]);
                }
                else if(!strncmp(tokens[0], "max_downscale_ratio",
                                strlen("max_downscale_ratio"))) {
                    mMDPDownscale = atoi(tokens[1]);
                }
                else if(!strncmp(tokens[0], "max_upscale_ratio",
                                strlen("max_upscale_ratio"))) {
                    mMDPUpscale = atoi(tokens[1]);
                } else if(!strncmp(tokens[0], "max_bandwidth_low",
                        strlen("max_bandwidth_low"))) {
                    mLowBw = atol(tokens[1]);
                } else if(!strncmp(tokens[0], "max_bandwidth_high",
                        strlen("max_bandwidth_high"))) {
                    mHighBw = atol(tokens[1]);
                } else if(!strncmp(tokens[0], "features", strlen("features"))) {
                    for(int i=1; i<index;i++) {
                        if(!strncmp(tokens[i], "bwc", strlen("bwc"))) {
                           mFeatures |= MDP_BWC_EN;
                        }
                        else if(!strncmp(tokens[i], "decimation",
                                    strlen("decimation"))) {
                           mFeatures |= MDP_DECIMATION_EN;
                        }
                    }
                }
            }
        }
        free(line);
        fclose(sysfsFd);
    }
    ALOGD_IF(DEBUG, "%s: mMDPVersion: %d mMdpRev: %x mRGBPipes:%d,"
                    "mVGPipes:%d", __FUNCTION__, mMDPVersion, mMdpRev,
                    mRGBPipes, mVGPipes);
    ALOGD_IF(DEBUG, "%s:mDMAPipes:%d \t mMDPDownscale:%d, mFeatures:%d",
                     __FUNCTION__,  mDMAPipes, mMDPDownscale, mFeatures);
    ALOGD_IF(DEBUG, "%s:mLowBw: %lu mHighBw: %lu", __FUNCTION__,  mLowBw,
            mHighBw);

    return true;
}

// This function reads the sysfs node to read MDP capabilities
// and parses and updates information accordingly.
bool MDPVersion::updateSplitInfo() {
    if(mMDPVersion >= MDSS_V5) {
        char split[64] = {0};
        FILE* fp = fopen("/sys/class/graphics/fb0/msm_fb_split", "r");
        if(fp){
            //Format "left right" space as delimiter
            if(fread(split, sizeof(char), 64, fp)) {
                split[sizeof(split) - 1] = '\0';
                mSplit.mLeft = atoi(split);
                ALOGI_IF(mSplit.mLeft, "Left Split=%d", mSplit.mLeft);
                char *rght = strpbrk(split, " ");
                if(rght)
                    mSplit.mRight = atoi(rght + 1);
                ALOGI_IF(mSplit.mRight, "Right Split=%d", mSplit.mRight);
            }
        } else {
            ALOGE("Failed to open mdss_fb_split node");
            return false;
        }
        if(fp)
            fclose(fp);
    }
    return true;
}


bool MDPVersion::supportsDecimation() {
    return mFeatures & MDP_DECIMATION_EN;
}

uint32_t MDPVersion::getMaxMDPDownscale() {
    return mMDPDownscale;
}

bool MDPVersion::supportsBWC() {
    // BWC - Bandwidth Compression
    return (mFeatures & MDP_BWC_EN);
}

bool MDPVersion::is8x26() {
    return (mMdpRev >= MDSS_MDP_HW_REV_101 and
            mMdpRev < MDSS_MDP_HW_REV_102);
}

bool MDPVersion::is8x74v2() {
    return (mMdpRev >= MDSS_MDP_HW_REV_102 and
            mMdpRev < MDSS_MDP_HW_REV_103);
}

bool MDPVersion::is8084() {
    return (mMdpRev >= MDSS_MDP_HW_REV_103 and
            mMdpRev < MDSS_MDP_HW_REV_104);
}

bool MDPVersion::is8092() {
    return (mMdpRev >= MDSS_MDP_HW_REV_200 and
            mMdpRev < MDSS_MDP_HW_REV_206);
}

}; //namespace qdutils

