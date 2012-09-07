/*
* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of Code Aurora Forum, Inc. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

#include <stdlib.h>
#include <utils/Log.h>
#include <linux/msm_mdp.h>
#include <cutils/properties.h>
#include "gralloc_priv.h"
#include "fb_priv.h"
#include "overlayUtils.h"
#include "mdpWrapper.h"
#include "mdp_version.h"

// just a helper static thingy
namespace {
struct IOFile {
    IOFile(const char* s, const char* mode) : fp(0) {
        fp = ::fopen(s, mode);
        if(!fp) {
            ALOGE("Failed open %s", s);
        }
    }
    template <class T>
            size_t read(T& r, size_t elem) {
                if(fp) {
                    return ::fread(&r, sizeof(T), elem, fp);
                }
                return 0;
            }
    size_t write(const char* s, uint32_t val) {
        if(fp) {
            return ::fprintf(fp, s, val);
        }
        return 0;
    }
    bool valid() const { return fp != 0; }
    ~IOFile() {
        if(fp) ::fclose(fp);
        fp=0;
    }
    FILE* fp;
};
}

namespace overlay {

//----------From class Res ------------------------------
const char* const Res::fbPath = "/dev/graphics/fb%u";
const char* const Res::rotPath = "/dev/msm_rotator";
const char* const Res::format3DFile =
        "/sys/class/graphics/fb1/format_3d";
const char* const Res::edid3dInfoFile =
        "/sys/class/graphics/fb1/3d_present";
const char* const Res::barrierFile =
        "/sys/devices/platform/mipi_novatek.0/enable_3d_barrier";
//--------------------------------------------------------



namespace utils {
//------------------ defines -----------------------------
#define FB_DEVICE_TEMPLATE "/dev/graphics/fb%u"
#define NUM_FB_DEVICES 3

//--------------------------------------------------------
FrameBufferInfo::FrameBufferInfo() {
    mFBWidth = 0;
    mFBHeight = 0;
    mBorderFillSupported = false;

    OvFD mFd;

    // Use open defined in overlayFD file to open fd for fb0
    if(!overlay::open(mFd, 0, Res::fbPath)) {
        ALOGE("FrameBufferInfo: failed to open fd");
        return;
    }

    if (!mFd.valid()) {
        ALOGE("FrameBufferInfo: FD not valid");
        return;
    }

    fb_var_screeninfo vinfo;
    if (!mdp_wrapper::getVScreenInfo(mFd.getFD(), vinfo)) {
        ALOGE("FrameBufferInfo: failed getVScreenInfo on fb0");
        mFd.close();
        return;
    }

    int mdpVersion = qdutils::MDPVersion::getInstance().getMDPVersion();
    if (mdpVersion < qdutils::MDSS_V5) {
        mdp_overlay ov;
        memset(&ov, 0, sizeof(ov));
        ov.id = 1;
        if (!mdp_wrapper::getOverlay(mFd.getFD(), ov)) {
            ALOGE("FrameBufferInfo: failed getOverlay on fb0");
            mFd.close();
            return;
        }
        mBorderFillSupported = (ov.flags & MDP_BORDERFILL_SUPPORTED) ?
                               true : false;
    } else {
        // badger always support border fill
        mBorderFillSupported = true;
    }

    mFd.close();
    mFBWidth = vinfo.xres;
    mFBHeight = vinfo.yres;
}

FrameBufferInfo* FrameBufferInfo::getInstance() {
    if (!sFBInfoInstance) {
        sFBInfoInstance = new FrameBufferInfo;
    }
    return sFBInfoInstance;
}

int FrameBufferInfo::getWidth() const {
    return mFBWidth;
}

int FrameBufferInfo::getHeight() const {
    return mFBHeight;
}

bool FrameBufferInfo::supportTrueMirroring() const {
    char value[PROPERTY_VALUE_MAX] = {0};
    property_get("hw.trueMirrorSupported", value, "0");
    int trueMirroringSupported = atoi(value);
    return (trueMirroringSupported && mBorderFillSupported);
}

/* clears any VG pipes allocated to the fb devices */
int initOverlay() {
    msmfb_mixer_info_req  req;
    mdp_mixer_info *minfo = NULL;
    char name[64];
    int fd = -1;
    for(int i = 0; i < NUM_FB_DEVICES; i++) {
        snprintf(name, 64, FB_DEVICE_TEMPLATE, i);
        ALOGD("initoverlay:: opening the device:: %s", name);
        fd = ::open(name, O_RDWR, 0);
        if(fd < 0) {
            ALOGE("cannot open framebuffer(%d)", i);
            return -1;
        }
        //Get the mixer configuration */
        req.mixer_num = i;
        if (ioctl(fd, MSMFB_MIXER_INFO, &req) == -1) {
            ALOGE("ERROR: MSMFB_MIXER_INFO ioctl failed");
            close(fd);
            return -1;
        }
        minfo = req.info;
        for (int j = 0; j < req.cnt; j++) {
            ALOGD("ndx=%d num=%d z_order=%d", minfo->pndx, minfo->pnum,
                    minfo->z_order);
            // except the RGB base layer with z_order of -1, clear any
            // other pipes connected to mixer.
            if((minfo->z_order) != -1) {
                int index = minfo->pndx;
                ALOGD("Unset overlay with index: %d at mixer %d", index, i);
                if(ioctl(fd, MSMFB_OVERLAY_UNSET, &index) == -1) {
                    ALOGE("ERROR: MSMFB_OVERLAY_UNSET failed");
                    close(fd);
                    return -1;
                }
            }
            minfo++;
        }
        close(fd);
        fd = -1;
    }
    return 0;
}

//--------------------------------------------------------
//Refer to graphics.h, gralloc_priv.h, msm_mdp.h
int getMdpFormat(int format) {
    switch (format) {
        //From graphics.h
        case HAL_PIXEL_FORMAT_RGBA_8888 :
            return MDP_RGBA_8888;
        case HAL_PIXEL_FORMAT_RGBX_8888:
            return MDP_RGBX_8888;
        case HAL_PIXEL_FORMAT_RGB_888:
            return MDP_RGB_888;
        case HAL_PIXEL_FORMAT_RGB_565:
            return MDP_RGB_565;
        case HAL_PIXEL_FORMAT_BGRA_8888:
            return MDP_BGRA_8888;
        case HAL_PIXEL_FORMAT_YV12:
            return MDP_Y_CR_CB_GH2V2;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            return MDP_Y_CBCR_H2V1;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            return MDP_Y_CRCB_H2V2;

        //From gralloc_priv.h
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
            return MDP_Y_CBCR_H2V2_TILE;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            return MDP_Y_CBCR_H2V2;
        case HAL_PIXEL_FORMAT_YCrCb_422_SP:
            return MDP_Y_CRCB_H2V1;
        case HAL_PIXEL_FORMAT_YCbCr_444_SP:
            return MDP_Y_CBCR_H1V1;
        case HAL_PIXEL_FORMAT_YCrCb_444_SP:
            return MDP_Y_CRCB_H1V1;

        default:
            //Unsupported by MDP
            //---graphics.h--------
            //HAL_PIXEL_FORMAT_RGBA_5551
            //HAL_PIXEL_FORMAT_RGBA_4444
            //HAL_PIXEL_FORMAT_YCbCr_422_I
            //---gralloc_priv.h-----
            //HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO    = 0x7FA30C01
            //HAL_PIXEL_FORMAT_R_8                    = 0x10D
            //HAL_PIXEL_FORMAT_RG_88                  = 0x10E
            ALOGE("%s: Unsupported format = 0x%x", __func__, format);
            return -1;
    }
    // not reached
    return -1;
}

//Set by client as HDMI/WFD
void setExtType(const int& type) {
    if(type != HDMI && type != WFD) {
        ALOGE("%s: Unrecognized type %d", __func__, type);
        return;
    }
    sExtType = type;
}

//Return External panel type set by client.
int getExtType() {
    return sExtType;
}

bool is3DTV() {
    char is3DTV = '0';
    IOFile fp(Res::edid3dInfoFile, "r");
    (void)fp.read(is3DTV, 1);
    ALOGI("3DTV EDID flag: %d", is3DTV);
    return (is3DTV == '0') ? false : true;
}

bool isPanel3D() {
    OvFD fd;
    if(!overlay::open(fd, 0 /*fb*/, Res::fbPath)){
        ALOGE("isPanel3D Can't open framebuffer 0");
        return false;
    }
    fb_fix_screeninfo finfo;
    if(!mdp_wrapper::getFScreenInfo(fd.getFD(), finfo)) {
        ALOGE("isPanel3D read fb0 failed");
    }
    fd.close();
    return (FB_TYPE_3D_PANEL == finfo.type) ? true : false;
}

bool usePanel3D() {
    if(!isPanel3D())
        return false;
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.user.panel3D", value, "0");
    int usePanel3D = atoi(value);
    return usePanel3D ? true : false;
}

bool send3DInfoPacket (uint32_t format3D) {
    IOFile fp(Res::format3DFile, "wb");
    (void)fp.write("%d", format3D);
    if(!fp.valid()) {
        ALOGE("send3DInfoPacket: no sysfs entry for setting 3d mode");
        return false;
    }
    return true;
}

bool enableBarrier (uint32_t orientation) {
    IOFile fp(Res::barrierFile, "wb");
    (void)fp.write("%d", orientation);
    if(!fp.valid()) {
        ALOGE("enableBarrier no sysfs entry for "
                "enabling barriers on 3D panel");
        return false;
    }
    return true;
}

uint32_t getS3DFormat(uint32_t fmt) {
    // The S3D is part of the HAL_PIXEL_FORMAT_YV12 value. Add
    // an explicit check for the format
    if (fmt == HAL_PIXEL_FORMAT_YV12) {
        return 0;
    }
    uint32_t fmt3D = format3D(fmt);
    uint32_t fIn3D = format3DInput(fmt3D); // MSB 2 bytes - inp
    uint32_t fOut3D = format3DOutput(fmt3D); // LSB 2 bytes - out
    fmt3D = fIn3D | fOut3D;
    if (!fIn3D) {
        fmt3D |= fOut3D << SHIFT_TOT_3D; //Set the input format
    }
    if (!fOut3D) {
        switch (fIn3D) {
            case HAL_3D_IN_SIDE_BY_SIDE_L_R:
            case HAL_3D_IN_SIDE_BY_SIDE_R_L:
                // For all side by side formats, set the output
                // format as Side-by-Side i.e 0x1
                fmt3D |= HAL_3D_IN_SIDE_BY_SIDE_L_R >> SHIFT_TOT_3D;
                break;
            default:
                fmt3D |= fIn3D >> SHIFT_TOT_3D; //Set the output format
        }
    }
    return fmt3D;
}

} // utils

} // overlay
