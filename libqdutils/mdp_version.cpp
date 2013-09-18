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
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/msm_mdp.h>
#include "mdp_version.h"

#define DEBUG 0

ANDROID_SINGLETON_STATIC_INSTANCE(qdutils::MDPVersion);
namespace qdutils {

#define TOKEN_PARAMS_DELIM  "="

MDPVersion::MDPVersion()
{
    int fb_fd = open("/dev/graphics/fb0", O_RDWR);
    char panel_type = 0;
    struct fb_fix_screeninfo fb_finfo;

    mMDPVersion = MDP_V_UNKNOWN;
    mMdpRev = 0;
    mRGBPipes = 0;
    mVGPipes = 0;
    mDMAPipes = 0;
    mFeatures = 0;
    mMDPUpscale = 0;
    //TODO get this from driver, default for A-fam to 8
    mMDPDownscale = 8;
    mFd = fb_fd;

    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fb_finfo) < 0) {
        ALOGE("FBIOGET_FSCREENINFO failed");
    } else {
        if(!strncmp(fb_finfo.id, "msmfb", 5)) {
            char str_ver[4] = { 0 };
            memcpy(str_ver, &fb_finfo.id[5], 3);
            str_ver[3] = '\0';
            mMDPVersion = atoi(str_ver);
            //Normalize MDP version to ease comparison.
            //This is needed only because
            //MDP 3.0.3 reports value as 303 which
            //is more than all the others
            if (mMDPVersion < 100)
                mMDPVersion *= 10;

            mRGBPipes = mVGPipes = 2;

        } else if (!strncmp(fb_finfo.id, "mdssfb", 6)) {
            mMDPVersion = MDSS_V5;
            if(!updateSysFsInfo()) {
                ALOGE("Unable to read updateSysFsInfo");
            }
            if (mMdpRev == MDP_V3_0_4){
                mMDPVersion = MDP_V3_0_4;
            }
        }

        /* Assumes panel type is 2nd element in '_' delimited id string */
        char * ptype = strstr(fb_finfo.id, "_");
        if (!ptype || (*(++ptype) == '\0')) {
            ALOGE("Invalid framebuffer info string: %s", fb_finfo.id);
            ptype = fb_finfo.id;
        }
        panel_type = *ptype;
    }
    mPanelType = panel_type;
    mHasOverlay = false;
    if((mMDPVersion >= MDP_V4_0) ||
       (mMDPVersion == MDP_V_UNKNOWN) ||
       (mMDPVersion == MDP_V3_0_4))
        mHasOverlay = true;
    if(mMDPVersion >= MDSS_V5) {
        char split[64] = {0};
        FILE* fp = fopen("/sys/class/graphics/fb0/msm_fb_split", "r");
        if(fp){
            //Format "left right" space as delimiter
            if(fread(split, sizeof(char), 64, fp)) {
                mSplit.mLeft = atoi(split);
                ALOGI_IF(mSplit.mLeft, "Left Split=%d", mSplit.mLeft);
                char *rght = strpbrk(split, " ");
                if(rght)
                    mSplit.mRight = atoi(rght + 1);
                ALOGI_IF(rght, "Right Split=%d", mSplit.mRight);
            }
        } else {
            ALOGE("Failed to open mdss_fb_split node");
        }

        if(fp)
            fclose(fp);
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


// This function reads the sysfs node to read MDP capabilities
// and parses and updates information accordingly.
bool MDPVersion::updateSysFsInfo() {
    FILE *sysfsFd;
    size_t len = 0;
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
                }
                else if(!strncmp(tokens[0], "features", strlen("features"))) {
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
            free(line);
            line = NULL;
        }
        fclose(sysfsFd);
    }
    ALOGD_IF(DEBUG, "%s: mMDPVersion: %d mMdpRev: %x mRGBPipes:%d,"
                    "mVGPipes:%d", __FUNCTION__, mMDPVersion, mMdpRev,
                    mRGBPipes, mVGPipes);
    ALOGD_IF(DEBUG, "%s:mDMAPipes:%d \t mMDPDownscale:%d, mFeatures:%d",
                     __FUNCTION__,  mDMAPipes, mMDPDownscale, mFeatures);
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
    // check for 8x26 variants
    // chip variants have same major number and minor numbers usually vary
    // for e.g., MDSS_MDP_HW_REV_101 is 0x10010000
    //                                    1001       -  major number
    //                                        0000   -  minor number
    // 8x26 v1 minor number is 0000
    //      v2 minor number is 0001 etc..
    if( mMdpRev >= MDSS_MDP_HW_REV_101 && mMdpRev < MDSS_MDP_HW_REV_102) {
        return true;
    }
    return false;
}

bool MDPVersion::is8x74v2() {
    if( mMdpRev >= MDSS_MDP_HW_REV_102 && mMdpRev < MDSS_MDP_HW_REV_103) {
        return true;
    }
    return false;
}

}; //namespace qdutils

