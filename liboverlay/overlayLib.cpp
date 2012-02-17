/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "overlayLib.h"
#include "gralloc_priv.h"

#define INTERLACE_MASK 0x80
#define DEBUG_OVERLAY true
/* Helper functions */
static inline size_t ALIGN(size_t x, size_t align) {
    return (x + align-1) & ~(align-1);
}

using namespace overlay;
using android::sp;
using gralloc::IMemAlloc;
using gralloc::IonController;
using gralloc::alloc_data;

#ifdef HDMI_AS_PRIMARY
bool Overlay::sHDMIAsPrimary = true;
#else
bool Overlay::sHDMIAsPrimary = false;
#endif

int overlay::get_mdp_format(int format) {
    switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888 :
        return MDP_RGBA_8888;
    case HAL_PIXEL_FORMAT_BGRA_8888:
        return MDP_BGRA_8888;
    case HAL_PIXEL_FORMAT_RGB_565:
        return MDP_RGB_565;
    case HAL_PIXEL_FORMAT_RGBX_8888:
        return MDP_RGBX_8888;
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        return MDP_Y_CBCR_H2V1;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        return MDP_Y_CRCB_H2V2;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        return MDP_Y_CBCR_H2V2;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
        return MDP_Y_CRCB_H2V2_TILE;
    case HAL_PIXEL_FORMAT_YV12:
        return MDP_Y_CR_CB_GH2V2;
    default:
        LOGE("%s: unknown color format [0x%x]", __FUNCTION__, format);
        return -1;
    }
    return -1;
}

int overlay::get_mdp_orientation(int value) {
    switch(value) {
        case 0: return 0;
        case HAL_TRANSFORM_FLIP_V:  return MDP_FLIP_UD;
        case HAL_TRANSFORM_FLIP_H:  return MDP_FLIP_LR;
        case HAL_TRANSFORM_ROT_90:  return MDP_ROT_90;
        case HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_V:
                                    return MDP_ROT_90|MDP_FLIP_LR;
        case HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_H:
                                    return MDP_ROT_90|MDP_FLIP_UD;
        case HAL_TRANSFORM_ROT_180: return MDP_ROT_180;
        case HAL_TRANSFORM_ROT_270: return MDP_ROT_270;
        default:
            LOGE("%s: invalid rotation value (value = 0x%x",
                  __FUNCTION__, value);
            return -1;
    }
    return -1;
}

// Rotator - input to output mapping
int overlay::get_rot_output_format(int format) {
    switch (format) {
    case MDP_Y_CRCB_H2V2_TILE:
        return MDP_Y_CRCB_H2V2;
    case MDP_Y_CB_CR_H2V2:
        return MDP_Y_CBCR_H2V2;
    case MDP_Y_CR_CB_GH2V2:
        return MDP_Y_CRCB_H2V2;
    default:
        return format;
    }
    return -1;
}

// This function normalizes the crop values to be all even
void overlay::normalize_crop(uint32_t& xy, uint32_t& wh) {

    if (xy & 0x0001) {
        // x or y is odd, increment it's value
        xy += 1;
        // Since we've incremented x(y), we need to decrement
        // w(h) accordingly
        if (wh & 0x0001) {
            // w or h is odd, decrement it by 1, to make it even
            EVEN_OUT(wh);
        } else {
            // w(h) is already even, hence we decrement by 2
            wh -=2;
        }
    } else {
        EVEN_OUT(wh);
    }
}

#define LOG_TAG "OverlayLIB"
static void reportError(const char* message) {
    LOGE( "%s", message);
}

void overlay::dump(mdp_overlay& mOVInfo) {
    if (!DEBUG_OVERLAY)
        return;
    LOGE("mOVInfo:");
    LOGE("src: width %d height %d format %s user_data[0] %d", mOVInfo.src.width,
        mOVInfo.src.height, getFormatString(mOVInfo.src.format),
        mOVInfo.user_data[0]);
    LOGE("src_rect: x %d y %d w %d h %d", mOVInfo.src_rect.x,
        mOVInfo.src_rect.y, mOVInfo.src_rect.w, mOVInfo.src_rect.h);
    LOGE("dst_rect: x %d y %d w %d h %d", mOVInfo.dst_rect.x,
        mOVInfo.dst_rect.y, mOVInfo.dst_rect.w, mOVInfo.dst_rect.h);
    LOGE("z_order %d is_fg %d alpha %d transp_mask %d flags %x id %d",
        mOVInfo.z_order, mOVInfo.is_fg, mOVInfo.alpha, mOVInfo.transp_mask,
        mOVInfo.flags, mOVInfo.id);
}

void overlay::dump(msm_rotator_img_info& mRotInfo) {
    if (!DEBUG_OVERLAY)
        return;
    LOGE("mRotInfo:");
    LOGE("session_id %d dst_x %d dst_y %d rotations %d enable %d",
        mRotInfo.session_id, mRotInfo.dst_x, mRotInfo.dst_y,
        mRotInfo.rotations, mRotInfo.enable);
    LOGE("src: width %d height %d format %s", mRotInfo.src.width,
        mRotInfo.src.height, getFormatString(mRotInfo.src.format));
    LOGE("dst: width %d height %d format %s", mRotInfo.dst.width,
        mRotInfo.dst.height, getFormatString(mRotInfo.src.format));
    LOGE("src_rect: x %d y %d w %d h %d", mRotInfo.src_rect.x,
        mRotInfo.src_rect.y, mRotInfo.src_rect.w, mRotInfo.src_rect.h);
}

const char* overlay::getFormatString(int format){
    static const char* formats[] = {
             "MDP_RGB_565",
             "MDP_XRGB_8888",
             "MDP_Y_CBCR_H2V2",
             "MDP_ARGB_8888",
             "MDP_RGB_888",
             "MDP_Y_CRCB_H2V2",
             "MDP_YCRYCB_H2V1",
             "MDP_Y_CRCB_H2V1",
             "MDP_Y_CBCR_H2V1",
             "MDP_RGBA_8888",
             "MDP_BGRA_8888",
             "MDP_RGBX_8888",
             "MDP_Y_CRCB_H2V2_TILE",
             "MDP_Y_CBCR_H2V2_TILE",
             "MDP_Y_CR_CB_H2V2",
             "MDP_Y_CR_CB_GH2V2",
             "MDP_Y_CB_CR_H2V2",
             "MDP_Y_CRCB_H1V1",
             "MDP_Y_CBCR_H1V1",
             "MDP_IMGTYPE_LIMIT",
             "MDP_BGR_565",
             "MDP_FB_FORMAT",
             "MDP_IMGTYPE_LIMIT2"
        };
    return formats[format];
}

bool overlay::isHDMIConnected () {
    char value[PROPERTY_VALUE_MAX];
    property_get("hw.hdmiON", value, "0");
    int isHDMI = atoi(value);
    return isHDMI ? true : false;
}

bool overlay::is3DTV() {
    char is3DTV = '0';
    FILE *fp = fopen(EDID_3D_INFO_FILE, "r");
    if (fp) {
        fread(&is3DTV, 1, 1, fp);
        fclose(fp);
    }
    LOGI("3DTV EDID flag: %c", is3DTV);
    return (is3DTV == '0') ? false : true;
}

bool overlay::isPanel3D() {
    int fd = open("/dev/graphics/fb0", O_RDWR, 0);
    if (fd < 0) {
        reportError("Can't open framebuffer 0");
        return false;
    }
    fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        reportError("FBIOGET_FSCREENINFO on fb0 failed");
        close(fd);
        fd = -1;
        return false;
    }
    close(fd);
    return (FB_TYPE_3D_PANEL == finfo.type) ? true : false;
}

bool overlay::usePanel3D() {
    if (Overlay::sHDMIAsPrimary)
        return is3DTV();

    if(!isPanel3D())
        return false;
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.user.panel3D", value, "0");
    int usePanel3D = atoi(value);
    return usePanel3D ? true : false;
}

bool overlay::send3DInfoPacket (unsigned int format3D) {
    FILE *fp = fopen(FORMAT_3D_FILE, "wb");
    if (fp) {
        fprintf(fp, "%d", format3D);
        fclose(fp);
        fp = NULL;
        return true;
    }
    LOGE("%s:no sysfs entry for setting 3d mode!", __FUNCTION__);
    return false;
}

bool overlay::enableBarrier (unsigned int orientation) {
    FILE *fp = fopen(BARRIER_FILE, "wb");
    if (fp) {
        fprintf(fp, "%d", orientation);
        fclose(fp);
        fp = NULL;
        return true;
    }
    LOGE("%s:no sysfs entry for enabling barriers on 3D panel!", __FUNCTION__);
    return false;
}

int overlay::getColorFormat(int format)
{
    if (format == HAL_PIXEL_FORMAT_YV12)
        return format;
    else if (format & INTERLACE_MASK)
        return format ^ HAL_PIXEL_FORMAT_INTERLACE;
    else
        return COLOR_FORMAT(format);
}

bool overlay::isInterlacedContent(int format)
{
    if ((format != HAL_PIXEL_FORMAT_YV12) &&
        (format & INTERLACE_MASK))
        return true;

    return false;
}

unsigned int overlay::getOverlayConfig (unsigned int format3D, bool poll,
        bool isHDMI) {
    bool isTV3D = false;
    unsigned int curState = 0;
    if (poll)
        isHDMI = isHDMIConnected();
    if (isHDMI) {
        LOGD("%s: HDMI connected... checking the TV type", __FUNCTION__);
        if (format3D) {
            if (is3DTV())
                curState = OV_3D_VIDEO_3D_TV;
            else
                curState = OV_3D_VIDEO_2D_TV;
        } else
            curState = OV_2D_VIDEO_ON_TV;
    } else {
        LOGD("%s: HDMI not connected...", __FUNCTION__);
        if(format3D) {
            if (usePanel3D())
                curState = OV_3D_VIDEO_3D_PANEL;
            else
                curState = OV_3D_VIDEO_2D_PANEL;
        }
        else
            curState = OV_2D_VIDEO_ON_PANEL;
    }
    return curState;
}

Overlay::Overlay() : mChannelUP(false), mHDMIConnected(false),
                     mS3DFormat(0), mCroppedSrcWidth(0),
                     mCroppedSrcHeight(0), mState(-1) {
    mOVBufferInfo.width = mOVBufferInfo.height = 0;
    mOVBufferInfo.format = mOVBufferInfo.size = 0;
}

Overlay::~Overlay() {
    closeChannel();
}

int Overlay::getFBWidth(int channel) const {
    return objOvCtrlChannel[channel].getFBWidth();
}

int Overlay::getFBHeight(int channel) const {
    return objOvCtrlChannel[channel].getFBHeight();
}

bool Overlay::startChannel(const overlay_buffer_info& info, int fbnum,
                              bool norot, bool uichannel,
                              unsigned int format3D, int channel,
                              bool ignoreFB, int num_buffers) {
    int zorder = 0;
    int format = getColorFormat(info.format);
    mCroppedSrcWidth = info.width;
    mCroppedSrcHeight = info.height;
    if (format3D)
        zorder = channel;
    if (mState == -1)
        mState = OV_UI_MIRROR_TV;

    mChannelUP = objOvCtrlChannel[channel].startControlChannel(info.width,
                                                       info.height, format, fbnum,
                                                       norot, uichannel,
                                                       format3D, zorder, ignoreFB);
    if (!mChannelUP) {
        LOGE("startChannel for fb%d failed", fbnum);
        return mChannelUP;
    }
    objOvCtrlChannel[channel].setSize(info.size);
    return objOvDataChannel[channel].startDataChannel(objOvCtrlChannel[channel], fbnum,
                                            norot, uichannel, num_buffers);
}

bool Overlay::closeChannel() {

    if (!mChannelUP)
        return true;

    if(mS3DFormat) {
        if (mHDMIConnected)
            overlay::send3DInfoPacket(0);
        else if (mState == OV_3D_VIDEO_3D_PANEL) {
            if (sHDMIAsPrimary)
                overlay::send3DInfoPacket(0);
            else
                enableBarrier(0);
        }
    }
    for (int i = 0; i < NUM_CHANNELS; i++) {
        objOvCtrlChannel[i].closeControlChannel();
        objOvDataChannel[i].closeDataChannel();
    }
    mChannelUP = false;
    mS3DFormat = 0;
    mOVBufferInfo.width = 0;
    mOVBufferInfo.height = 0;
    mOVBufferInfo.format = 0;
    mOVBufferInfo.size = 0;
    mState = -1;
    return true;
}

bool Overlay::getPosition(int& x, int& y, uint32_t& w, uint32_t& h, int channel) {
    return objOvCtrlChannel[channel].getPosition(x, y, w, h);
}

bool Overlay::getOrientation(int& orientation, int channel) const {
    return objOvCtrlChannel[channel].getOrientation(orientation);
}

bool Overlay::setPosition(int x, int y, uint32_t w, uint32_t h) {
    bool ret = false;
    overlay_rect rect;
    switch (mState) {
        case OV_UI_MIRROR_TV:
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
            return setChannelPosition(x, y, w, h, VG0_PIPE);
            break;
        case OV_2D_VIDEO_ON_TV:
            objOvCtrlChannel[VG1_PIPE].getAspectRatioPosition(mCroppedSrcWidth,
                            mCroppedSrcHeight, &rect);
            setChannelPosition(rect.x, rect.y, rect.w, rect.h, VG1_PIPE);
            return setChannelPosition(x, y, w, h, VG0_PIPE);
            break;
        case OV_3D_VIDEO_3D_PANEL:
            for (int i = 0; i < NUM_CHANNELS; i++) {
                if (sHDMIAsPrimary)
                    objOvCtrlChannel[i].getPositionS3D(i, mS3DFormat, &rect);
                else {
                    if (!objOvCtrlChannel[i].useVirtualFB()) {
                        LOGE("%s: failed virtual fb for channel %d", __FUNCTION__, i);
                        return false;
                    }
                    objOvCtrlChannel[i].getPositionS3D(i, 0x1, &rect);
                }
                if(!setChannelPosition(rect.x, rect.y, rect.w, rect.h, i)) {
                    LOGE("%s: failed for channel %d", __FUNCTION__, i);
                    return false;
                }
            }
            break;
        case OV_3D_VIDEO_2D_TV:
        case OV_3D_VIDEO_3D_TV:
            for (int i = 0; i < NUM_CHANNELS; i++) {
                ret = objOvCtrlChannel[i].getPositionS3D(i, mS3DFormat, &rect);
                if (!ret)
                    ret = setChannelPosition(x, y, w, h, i);
                else
                    ret = setChannelPosition(rect.x, rect.y, rect.w, rect.h, i);
                if (!ret) {
                    LOGE("%s: failed for channel %d", __FUNCTION__, i);
                    return ret;
                }
            }
            break;
        default:
            LOGE("%s:Unknown state %d", __FUNCTION__, mState);
            break;
    }
    return true;
}

bool Overlay::setChannelPosition(int x, int y, uint32_t w, uint32_t h, int channel) {
    return objOvCtrlChannel[channel].setPosition(x, y, w, h);
}

bool Overlay::updateOverlaySource(const overlay_buffer_info& info, int orientation,
                                  bool waitForVsync) {
    bool ret = false;
    int currentFlags = 0;
    if (objOvCtrlChannel[0].isChannelUP()) {
        currentFlags = objOvCtrlChannel[0].getOverlayFlags();
    }

    bool needUpdateFlags = false;
    if (waitForVsync) {
        if (currentFlags & MDP_OV_PLAY_NOWAIT) {
            needUpdateFlags = true;
        }
    } else {
        if (!(currentFlags & MDP_OV_PLAY_NOWAIT)) {
            needUpdateFlags = true;
        }
    }

    bool geometryChanged = true;
    if (info.width == mOVBufferInfo.width &&
        info.height == mOVBufferInfo.height &&
        info.format == mOVBufferInfo.format) {
        geometryChanged = false;
    }

    if (sHDMIAsPrimary)
        needUpdateFlags = false;

    if ((false == needUpdateFlags) && (false == geometryChanged)) {
        objOvDataChannel[0].updateDataChannel(0, 0);
        return true;
    }

    // Disable rotation for the HDMI channels
    int orientHdmi = 0;
    int orientPrimary = sHDMIAsPrimary ? 0 : orientation;
    int orient[2] = {orientPrimary, orientHdmi};
    // disable waitForVsync on HDMI, since we call the wait ioctl
    bool waitForHDMI = false;
    bool waitForPrimary = sHDMIAsPrimary ? true : waitForVsync;
    bool waitCond[2] = {waitForPrimary, waitForHDMI};

    switch(mState) {
        case OV_3D_VIDEO_3D_PANEL:
            orient[1] = sHDMIAsPrimary ? 0 : orientation;
            break;
        case OV_3D_VIDEO_3D_TV:
            orient[0] = 0;
            break;
        default:
            break;
    }

    int numChannelsToUpdate = NUM_CHANNELS;
    if (!geometryChanged) {
        // Only update the primary channel - we only need to update the
        // wait/no-wait flags
        if (objOvCtrlChannel[0].isChannelUP()) {
            return objOvCtrlChannel[0].updateWaitForVsyncFlags(waitForVsync);
        }
    }

    // Set the overlay source info
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (objOvCtrlChannel[i].isChannelUP()) {
            ret = objOvCtrlChannel[i].updateOverlaySource(info, orient[i], waitCond[i]);
            if (!ret) {
                LOGE("objOvCtrlChannel[%d].updateOverlaySource failed", i);
                return false;
            }
            objOvCtrlChannel[i].setSize(info.size);
            int updateDataChannel = orientation ? 1:0;
            ret = objOvDataChannel[i].updateDataChannel(updateDataChannel, info.size);
        }
    }
    if (ret) {
        mOVBufferInfo = info;
    } else
        LOGE("update failed");
    return ret;
}

int Overlay::getS3DFormat(int format) {
    // The S3D is part of the HAL_PIXEL_FORMAT_YV12 value. Add
    // an explicit check for the format
    if (format == HAL_PIXEL_FORMAT_YV12) {
        return 0;
    }
    int format3D = FORMAT_3D(format);
    int fIn3D = FORMAT_3D_INPUT(format3D); // MSB 2 bytes are input format
    int fOut3D = FORMAT_3D_OUTPUT(format3D); // LSB 2 bytes are output format
    format3D = fIn3D | fOut3D;
    if (!fIn3D) {
        format3D |= fOut3D << SHIFT_3D; //Set the input format
    }
    if (!fOut3D) {
        format3D |= fIn3D >> SHIFT_3D; //Set the output format
    }
    return format3D;
}

bool Overlay::setSource(const overlay_buffer_info& info, int orientation,
                        bool hdmiConnected, bool waitForVsync, int num_buffers) {
    // Separate the color format from the 3D format.
    // If there is 3D content; the effective format passed by the client is:
    // effectiveFormat = 3D_IN | 3D_OUT | ColorFormat
    int newState = mState;
    bool stateChange = false, ret = false;
    unsigned int format3D = getS3DFormat(info.format);
    int colorFormat = getColorFormat(info.format);
    if (-1 == mState) {
        newState = getOverlayConfig (format3D, false, hdmiConnected);
        stateChange = (mState == newState) ? false : true;
    }

    if (stateChange) {
        closeChannel();
        mHDMIConnected = hdmiConnected;
        mState = newState;
        mS3DFormat = format3D;
        if (mState == OV_3D_VIDEO_2D_PANEL || mState == OV_3D_VIDEO_2D_TV) {
            LOGI("3D content on 2D display: set the output format as monoscopic");
            mS3DFormat = FORMAT_3D_INPUT(format3D) | HAL_3D_OUT_MONOSCOPIC_MASK;
        }
        // We always enable the rotator for the primary.
        bool noRot = false;
        bool uiChannel = false;
        switch(mState) {
            case OV_2D_VIDEO_ON_PANEL:
            case OV_3D_VIDEO_2D_PANEL:
                return startChannel(info, FRAMEBUFFER_0, noRot, false,
                        mS3DFormat, VG0_PIPE, waitForVsync, num_buffers);
                break;
            case OV_3D_VIDEO_3D_PANEL:
                if (sHDMIAsPrimary) {
                    noRot = true;
                    waitForVsync = true;
                    send3DInfoPacket(mS3DFormat & OUTPUT_MASK_3D);
                }
                for (int i=0; i<NUM_CHANNELS; i++) {
                    if(!startChannel(info, FRAMEBUFFER_0, noRot, uiChannel,
                                mS3DFormat, i, waitForVsync, num_buffers)) {
                        LOGE("%s:failed to open channel %d", __FUNCTION__, i);
                        return false;
                    }
                }
                break;
            case OV_2D_VIDEO_ON_TV:
            case OV_3D_VIDEO_2D_TV:
                for (int i=0; i<NUM_CHANNELS; i++) {
                    if (FRAMEBUFFER_1 == i) {
                        // Disable rotation for HDMI
                        noRot = true;
                        waitForVsync = false;
                    }
                    if(!startChannel(info, i, noRot, false, mS3DFormat,
                                i, waitForVsync, num_buffers)) {
                        LOGE("%s:failed to open channel %d", __FUNCTION__, i);
                        return false;
                    }
                }
                overlay_rect rect;
                objOvCtrlChannel[VG1_PIPE].getAspectRatioPosition(info.width, info.height, &rect);
                return setChannelPosition(rect.x, rect.y, rect.w, rect.h, VG1_PIPE);
                break;
            case OV_3D_VIDEO_3D_TV:
                for (int i=0; i<NUM_CHANNELS; i++) {
                    if(!startChannel(info, FRAMEBUFFER_1, true, false,
                                mS3DFormat, i, waitForVsync, num_buffers)) {
                        LOGE("%s:failed to open channel %d", __FUNCTION__, i);
                        return false;
                    }
                    send3DInfoPacket(mS3DFormat & OUTPUT_MASK_3D);
                }
                break;
            default:
                LOGE("%s:Unknown state %d", __FUNCTION__, mState);
                break;
        }
    } else {
        ret = updateOverlaySource(info, orientation, waitForVsync);
    }
    return true;
}

bool Overlay::setCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!mChannelUP) {
        LOGE("%s: channel not set", __FUNCTION__);
        return false;
    }
    overlay_rect rect, inRect;
    inRect.x = x; inRect.y = y; inRect.w = w; inRect.h = h;
    mCroppedSrcWidth = w;
    mCroppedSrcHeight = h;

    switch (mState) {
        case OV_UI_MIRROR_TV:
        case OV_2D_VIDEO_ON_PANEL:
            return setChannelCrop(x, y, w, h, VG0_PIPE);
            break;
        case OV_3D_VIDEO_2D_PANEL:
            objOvDataChannel[VG0_PIPE].getCropS3D(&inRect, VG0_PIPE, mS3DFormat, &rect);
            return setChannelCrop(rect.x, rect.y, rect.w, rect.h, VG0_PIPE);
            break;
        case OV_2D_VIDEO_ON_TV:
            for (int i=0; i<NUM_CHANNELS; i++) {
                if(!setChannelCrop(x, y, w, h, i)) {
                    LOGE("%s: failed for pipe %d", __FUNCTION__, i);
                    return false;
                }
            }
            break;
        case OV_3D_VIDEO_3D_PANEL:
        case OV_3D_VIDEO_2D_TV:
        case OV_3D_VIDEO_3D_TV:
            for (int i=0; i<NUM_CHANNELS; i++) {
                objOvDataChannel[i].getCropS3D(&inRect, i, mS3DFormat, &rect);
                if(!setChannelCrop(rect.x, rect.y, rect.w, rect.h, i)) {
                    LOGE("%s: failed for pipe %d", __FUNCTION__, i);
                    return false;
                }
            }
            break;
        default:
            LOGE("%s:Unknown state %d", __FUNCTION__, mState);
            break;
    }
    return true;
}

bool Overlay::setChannelCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int channel) {
    return objOvDataChannel[channel].setCrop(x, y, w, h);
}

bool Overlay::setTransform(int value) {
    int barrier = 0;
    switch (mState) {
        case OV_UI_MIRROR_TV:
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
            return objOvCtrlChannel[VG0_PIPE].setTransform(value);
            break;
        case OV_2D_VIDEO_ON_TV:
        case OV_3D_VIDEO_2D_TV:
        case OV_3D_VIDEO_3D_TV:
            for (int i=0; i<NUM_CHANNELS; i++) {
                if(!objOvCtrlChannel[i].setTransform(value)) {
                    LOGE("%s:failed for channel %d", __FUNCTION__, i);
                    return false;
                }
            }
            break;
        case OV_3D_VIDEO_3D_PANEL:
            switch (value) {
                case HAL_TRANSFORM_ROT_90:
                case HAL_TRANSFORM_ROT_270:
                    barrier = BARRIER_LANDSCAPE;
                    break;
                default:
                    barrier = BARRIER_PORTRAIT;
                    break;
                    if(!enableBarrier(barrier))
                        LOGE("%s:failed to enable barriers for 3D video", __FUNCTION__);
            }
            for (int i=0; i<NUM_CHANNELS; i++) {
                if(!objOvCtrlChannel[i].setTransform(value)) {
                    LOGE("%s:failed for channel %d", __FUNCTION__, i);
                    return false;
               }
            }
            break;
        default:
            LOGE("%s:Unknown state %d", __FUNCTION__, mState);
            break;
    }
    return true;
}

bool Overlay::setFd(int fd, int channel) {
    return objOvDataChannel[channel].setFd(fd);
}

bool Overlay::queueBuffer(uint32_t offset, int channel) {
    return objOvDataChannel[channel].queueBuffer(offset);
}
#if 0
bool Overlay::waitForHdmiVsync(int channel) {
    return objOvDataChannel[channel].waitForHdmiVsync();
}
#endif
bool Overlay::queueBuffer(buffer_handle_t buffer) {
    private_handle_t const* hnd = reinterpret_cast
                                   <private_handle_t const*>(buffer);
    if (!hnd) {
        LOGE("Overlay::queueBuffer invalid handle");
        return false;
    }
    const size_t offset = hnd->offset;
    const int fd = hnd->fd;
    switch (mState) {
        case OV_UI_MIRROR_TV:
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
            if(!queueBuffer(fd, offset, VG0_PIPE)) {
                LOGE("%s:failed for channel 0", __FUNCTION__);
                return false;
            }
            break;
        case OV_2D_VIDEO_ON_TV:
        case OV_3D_VIDEO_3D_PANEL:
        case OV_3D_VIDEO_2D_TV:
        case OV_3D_VIDEO_3D_TV:
            for (int i=NUM_CHANNELS-1; i>=0; i--) {
                if(!queueBuffer(fd, offset, i)) {
                    LOGE("%s:failed for channel %d", __FUNCTION__, i);
                    return false;
                }
            }
#if 0
            //Wait for HDMI done..
            if(!waitForHdmiVsync(VG1_PIPE)) {
                LOGE("%s: waitforHdmiVsync failed", __FUNCTION__);
                return false;
            }
#endif
            break;
        default:
            LOGE("%s:Unknown state %d", __FUNCTION__, mState);
            break;
    }
    return true;
}

bool Overlay::queueBuffer(int fd, uint32_t offset, int channel) {
    bool ret = false;
    ret = setFd(fd, channel);
    if(!ret) {
        LOGE("Overlay::queueBuffer channel %d setFd failed", channel);
        return false;
    }
    ret = queueBuffer(offset, channel);
    if(!ret) {
        LOGE("Overlay::queueBuffer channel %d queueBuffer failed", channel);
        return false;
    }
    return ret;
}

OverlayControlChannel::OverlayControlChannel() : mNoRot(false), mFD(-1), mRotFD(-1),
                                                 mFormat3D(0), mIsChannelUpdated(true) {
    memset(&mOVInfo, 0, sizeof(mOVInfo));
    memset(&m3DOVInfo, 0, sizeof(m3DOVInfo));
    memset(&mRotInfo, 0, sizeof(mRotInfo));
}


OverlayControlChannel::~OverlayControlChannel() {
    closeControlChannel();
}

bool OverlayControlChannel::getAspectRatioPosition(int w, int h, overlay_rect *rect)
{
    int width = w, height = h, x, y;
    int fbWidth  = getFBWidth();
    int fbHeight = getFBHeight();
    // width and height for YUV TILE format
    int tempWidth = w, tempHeight = h;
    /* Calculate the width and height if it is YUV TILE format*/
    if(getFormat() == HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED) {
        tempWidth = w - ( (((w-1)/64 +1)*64) - w);
        tempHeight = h - ((((h-1)/32 +1)*32) - h);
    }
    if (width * fbHeight > fbWidth * height) {
        height = fbWidth * height / width;
        EVEN_OUT(height);
        width = fbWidth;
    } else if (width * fbHeight < fbWidth * height) {
        width = fbHeight * width / height;
        EVEN_OUT(width);
        height = fbHeight;
    } else {
        width = fbWidth;
        height = fbHeight;
    }
    /* Scaling of upto a max of 8 times supported */
    if(width >(tempWidth * HW_OVERLAY_MAGNIFICATION_LIMIT)){
        width = HW_OVERLAY_MAGNIFICATION_LIMIT * tempWidth;
    }
    if(height >(tempHeight*HW_OVERLAY_MAGNIFICATION_LIMIT)) {
        height = HW_OVERLAY_MAGNIFICATION_LIMIT * tempHeight;
    }
    if (width > fbWidth) width = fbWidth;
    if (height > fbHeight) height = fbHeight;
    x = (fbWidth - width) / 2;
    y = (fbHeight - height) / 2;
    rect->x = x;
    rect->y = y;
    rect->w = width;
    rect->h = height;
    return true;
}

bool OverlayControlChannel::getPositionS3D(int channel, int format, overlay_rect *rect) {
    int wDisp = getFBWidth();
    int hDisp = getFBHeight();
    switch (format & OUTPUT_MASK_3D) {
    case HAL_3D_OUT_SIDE_BY_SIDE_MASK:
        if (channel == VG0_PIPE) {
            rect->x = 0;
            rect->y = 0;
            rect->w = wDisp/2;
            rect->h = hDisp;
        } else {
            rect->x = wDisp/2;
            rect->y = 0;
            rect->w = wDisp/2;
            rect->h = hDisp;
        }
        break;
    case HAL_3D_OUT_TOP_BOTTOM_MASK:
        if (channel == VG0_PIPE) {
            rect->x = 0;
            rect->y = 0;
            rect->w = wDisp;
            rect->h = hDisp/2;
        } else {
            rect->x = 0;
            rect->y = hDisp/2;
            rect->w = wDisp;
            rect->h = hDisp/2;
        }
        break;
    case HAL_3D_OUT_MONOSCOPIC_MASK:
        if (channel == VG1_PIPE) {
            rect->x = 0;
            rect->y = 0;
            rect->w = wDisp;
            rect->h = hDisp;
        }
        else
            return false;
        break;
    case HAL_3D_OUT_INTERLEAVE_MASK:
        break;
    default:
        reportError("Unsupported 3D output format");
        break;
    }
    return true;
}

bool OverlayControlChannel::openDevices(int fbnum) {
    if (fbnum < 0)
        return false;

    char const * const device_template =
                       "/dev/graphics/fb%u";
    char dev_name[64];
    snprintf(dev_name, 64, device_template, fbnum);

    mFD = open(dev_name, O_RDWR, 0);
    if (mFD < 0) {
        reportError("Cant open framebuffer ");
        return false;
    }

    fb_fix_screeninfo finfo;
    if (ioctl(mFD, FBIOGET_FSCREENINFO, &finfo) == -1) {
        reportError("FBIOGET_FSCREENINFO on fb1 failed");
        close(mFD);
        mFD = -1;
        return false;
    }

    fb_var_screeninfo vinfo;
    if (ioctl(mFD, FBIOGET_VSCREENINFO, &vinfo) == -1) {
        reportError("FBIOGET_VSCREENINFO on fb1 failed");
        close(mFD);
        mFD = -1;
        return false;
    }
    mFBWidth = vinfo.xres;
    mFBHeight = vinfo.yres;
    mFBbpp = vinfo.bits_per_pixel;
    mFBystride = finfo.line_length;

    if (!mNoRot) {
        mRotFD = open("/dev/msm_rotator", O_RDWR, 0);
        if (mRotFD < 0) {
            reportError("Cant open rotator device");
            close(mFD);
            mFD = -1;
            return false;
        }
    }

    return true;
}

bool OverlayControlChannel::setOverlayInformation(const overlay_buffer_info& info,
                                  int flags, int orientation, int zorder,
                                  bool ignoreFB, int requestType) {
    int w = info.width;
    int h = info.height;
    int format = info.format;

    mOVInfo.src.width  = w;
    mOVInfo.src.height = h;
    mOVInfo.src_rect.x = 0;
    mOVInfo.src_rect.y = 0;
    mOVInfo.dst_rect.x = 0;
    mOVInfo.dst_rect.y = 0;
    mOVInfo.dst_rect.w = w;
    mOVInfo.dst_rect.h = h;
    if(format == MDP_Y_CRCB_H2V2_TILE) {
        if (!orientation) {
           mOVInfo.src_rect.w = w - ((((w-1)/64 +1)*64) - w);
           mOVInfo.src_rect.h = h - ((((h-1)/32 +1)*32) - h);
        } else {
           mOVInfo.src_rect.w = w;
           mOVInfo.src_rect.h = h;
           mOVInfo.src.width  = (((w-1)/64 +1)*64);
           mOVInfo.src.height = (((h-1)/32 +1)*32);
           mOVInfo.src_rect.x = mOVInfo.src.width - w;
           mOVInfo.src_rect.y = mOVInfo.src.height - h;
        }
    } else {
        mOVInfo.src_rect.w = w;
        mOVInfo.src_rect.h = h;
    }

    mOVInfo.src.format = format;
    if (w > mFBWidth)
        mOVInfo.dst_rect.w = mFBWidth;
    if (h > mFBHeight)
        mOVInfo.dst_rect.h = mFBHeight;

    mOVInfo.user_data[0] = 0;
    if (requestType == NEW_REQUEST) {
        mOVInfo.id = MSMFB_NEW_REQUEST;
        mOVInfo.z_order = zorder;
        mOVInfo.alpha = 0xff;
        mOVInfo.transp_mask = 0xffffffff;
    }
    mOVInfo.flags = flags;
    if (!ignoreFB)
        mOVInfo.flags |= MDP_OV_PLAY_NOWAIT;
    else
        mOVInfo.flags &= ~MDP_OV_PLAY_NOWAIT;

    return true;
}

bool OverlayControlChannel::startOVRotatorSessions(
                           const overlay_buffer_info& info,
                           int orientation, int requestType) {
    bool ret = true;
    int w = info.width;
    int h = info.height;
    int format = info.format;

    if (orientation) {
        mRotInfo.src.format = format;
        mRotInfo.src.width = w;
        mRotInfo.src.height = h;
        mRotInfo.src_rect.w = w;
        mRotInfo.src_rect.h = h;
        mRotInfo.dst.width = w;
        mRotInfo.dst.height = h;
        if(format == MDP_Y_CRCB_H2V2_TILE) {
            mRotInfo.src.width =  (((w-1)/64 +1)*64);
            mRotInfo.src.height = (((h-1)/32 +1)*32);
            mRotInfo.src_rect.w = (((w-1)/64 +1)*64);
            mRotInfo.src_rect.h = (((h-1)/32 +1)*32);
            mRotInfo.dst.width = (((w-1)/64 +1)*64);
            mRotInfo.dst.height = (((h-1)/32 +1)*32);
            mRotInfo.dst.format = MDP_Y_CRCB_H2V2;
        }
        mRotInfo.dst.format = get_rot_output_format(format);
        mRotInfo.dst_x = 0;
        mRotInfo.dst_y = 0;
        mRotInfo.src_rect.x = 0;
        mRotInfo.src_rect.y = 0;
        mRotInfo.rotations = 0;

        if (requestType == NEW_REQUEST) {
            mRotInfo.enable = 0;
            if(mUIChannel)
                mRotInfo.enable = 1;
            mRotInfo.session_id = 0;
        } else
            mRotInfo.enable = 1;

        int result = ioctl(mRotFD, MSM_ROTATOR_IOCTL_START, &mRotInfo);
        if (result) {
            reportError("Rotator session failed");
            dump(mRotInfo);
            ret = false;
        }
    }

    if (ret && ioctl(mFD, MSMFB_OVERLAY_SET, &mOVInfo)) {
        reportError("startOVRotatorSessions, Overlay set failed");
        dump(mOVInfo);
        ret = false;
    }

    if (!ret)
        closeControlChannel();
    else
        mIsChannelUpdated = true;
    return ret;
}

bool OverlayControlChannel::updateOverlaySource(const overlay_buffer_info& info,
                                                int orientation, bool waitForVsync)
{
    int colorFormat = getColorFormat(info.format);
    int hw_format = get_mdp_format(colorFormat);
    overlay_buffer_info ovBufInfo;
    ovBufInfo.width = info.width;
    ovBufInfo.height = info.height;
    ovBufInfo.format = hw_format;

    int flags = isInterlacedContent(info.format) ? MDP_DEINTERLACE : 0;
    if (!setOverlayInformation(ovBufInfo, flags, orientation, 0, waitForVsync,
                               UPDATE_REQUEST))
        return false;

    return startOVRotatorSessions(ovBufInfo, orientation, UPDATE_REQUEST);
}

bool OverlayControlChannel::startControlChannel(int w, int h,
                                           int format, int fbnum, bool norot,
                                           bool uichannel,
                                           unsigned int format3D, int zorder,
                                           bool ignoreFB) {
    mNoRot = norot;
    mFormat = format;
    mUIChannel = uichannel;
    fb_fix_screeninfo finfo;
    fb_var_screeninfo vinfo;
    int hw_format;
    int flags = 0;
    int colorFormat = format;
    // The interlace mask is part of the HAL_PIXEL_FORMAT_YV12 value. Add
    // an explicit check for the format
    if (isInterlacedContent(format)) {
        flags |= MDP_DEINTERLACE;

        // Get the actual format
        colorFormat = format ^ HAL_PIXEL_FORMAT_INTERLACE;
    }
    hw_format = get_mdp_format(colorFormat);
    if (hw_format < 0) {
        reportError("Unsupported format");
        return false;
    }

    mFormat3D = format3D;
    if ( !mFormat3D || (mFormat3D & HAL_3D_OUT_MONOSCOPIC_MASK) ) {
        // Set the share bit for sharing the VG pipe
        flags |= MDP_OV_PIPE_SHARE;
    }
    if (!openDevices(fbnum))
        return false;

    int orientation = mNoRot ? 0: 1;
    overlay_buffer_info ovBufInfo;
    ovBufInfo.width = w;
    ovBufInfo.height = h;
    ovBufInfo.format = hw_format;
    if (!setOverlayInformation(ovBufInfo, flags, orientation, zorder, ignoreFB, NEW_REQUEST))
        return false;

    return startOVRotatorSessions(ovBufInfo, orientation, NEW_REQUEST);
}

bool OverlayControlChannel::closeControlChannel() {
    if (!isChannelUP())
        return true;

    if (!mNoRot && mRotFD > 0) {
        ioctl(mRotFD, MSM_ROTATOR_IOCTL_FINISH, &(mRotInfo.session_id));
        close(mRotFD);
        mRotFD = -1;
    }

    int ovid = mOVInfo.id;
    ioctl(mFD, MSMFB_OVERLAY_UNSET, &ovid);
    if (m3DOVInfo.is_3d) {
        m3DOVInfo.is_3d = 0;
        ioctl(mFD, MSMFB_OVERLAY_3D, &m3DOVInfo);
    }

    close(mFD);
    memset(&mOVInfo, 0, sizeof(mOVInfo));
    memset(&mRotInfo, 0, sizeof(mRotInfo));
    memset(&m3DOVInfo, 0, sizeof(m3DOVInfo));
    mFD = -1;

    return true;
}

bool OverlayControlChannel::updateWaitForVsyncFlags(bool waitForVsync) {
    if (!waitForVsync)
        mOVInfo.flags |= MDP_OV_PLAY_NOWAIT;
    else
        mOVInfo.flags &= ~MDP_OV_PLAY_NOWAIT;

    if (ioctl(mFD, MSMFB_OVERLAY_SET, &mOVInfo)) {
        LOGE("%s: OVERLAY_SET failed", __FUNCTION__);
        dump(mOVInfo);
        return false;
    }
    return true;
}

bool OverlayControlChannel::setPosition(int x, int y, uint32_t w, uint32_t h) {

    if (!isChannelUP() ||
           (x < 0) || (y < 0) || ((x + w) > mFBWidth) ||
           ((y + h) > mFBHeight)) {
        reportError("setPosition failed");
        LOGW("x %d y %d (x+w) %d (y+h) %d FBWidth %d FBHeight %d", x, y, x+w, y+h,
                                                        mFBWidth,mFBHeight);
        return false;
    }
    if( x != mOVInfo.dst_rect.x || y != mOVInfo.dst_rect.y ||
        w != mOVInfo.dst_rect.w || h !=  mOVInfo.dst_rect.h ) {
        mdp_overlay ov;
        ov.id = mOVInfo.id;
        if (ioctl(mFD, MSMFB_OVERLAY_GET, &ov)) {
            reportError("setPosition, overlay GET failed");
            return false;
        }

        /* Scaling of upto a max of 8 times supported */
        if(w >(ov.src_rect.w * HW_OVERLAY_MAGNIFICATION_LIMIT)){
            w = HW_OVERLAY_MAGNIFICATION_LIMIT * ov.src_rect.w;
            x = (mFBWidth - w) / 2;
        }
        if(h >(ov.src_rect.h * HW_OVERLAY_MAGNIFICATION_LIMIT)) {
            h = HW_OVERLAY_MAGNIFICATION_LIMIT * ov.src_rect.h;
            y = (mFBHeight - h) / 2;
        }
        ov.dst_rect.x = x;
        ov.dst_rect.y = y;
        ov.dst_rect.w = w;
        ov.dst_rect.h = h;
        if (ioctl(mFD, MSMFB_OVERLAY_SET, &ov)) {
            reportError("setPosition, Overlay SET failed");
            dump(ov);
            return false;
        }
        mOVInfo = ov;
    }
    return true;
}

void OverlayControlChannel::swapOVRotWidthHeight() {
    int tmp = mOVInfo.src.width;
    mOVInfo.src.width = mOVInfo.src.height;
    mOVInfo.src.height = tmp;

    tmp = mOVInfo.src_rect.h;
    mOVInfo.src_rect.h = mOVInfo.src_rect.w;
    mOVInfo.src_rect.w = tmp;

    tmp = mRotInfo.dst.width;
    mRotInfo.dst.width = mRotInfo.dst.height;
    mRotInfo.dst.height = tmp;
}

bool OverlayControlChannel::useVirtualFB() {
    if(!m3DOVInfo.is_3d) {
        m3DOVInfo.is_3d = 1;
        mFBWidth *= 2;
        mFBHeight /= 2;
        m3DOVInfo.width = mFBWidth;
        m3DOVInfo.height = mFBHeight;
        return ioctl(mFD, MSMFB_OVERLAY_3D, &m3DOVInfo) ? false : true;
    }
    return true;
}

bool OverlayControlChannel::setTransform(int value, bool fetch) {
    if (!isChannelUP()) {
        LOGE("%s: channel is not up", __FUNCTION__);
        return false;
    }

    mdp_overlay ov = mOVInfo;
    if (fetch && ioctl(mFD, MSMFB_OVERLAY_GET, &ov)) {
        reportError("setParameter, overlay GET failed");
        return false;
    }
    mOVInfo = ov;
    if (!mIsChannelUpdated) {
        int orientation = get_mdp_orientation(value);
        if (orientation == mOVInfo.user_data[0]) {
            return true;
        }
    }
    mIsChannelUpdated = false;

    int val = mOVInfo.user_data[0];
    if (mNoRot)
        return true;

    int rot = value;

    switch(rot) {
        case 0:
        case HAL_TRANSFORM_FLIP_H:
        case HAL_TRANSFORM_FLIP_V:
        {
            if (val == MDP_ROT_90) {
                    int tmp = mOVInfo.src_rect.y;
                    mOVInfo.src_rect.y = mOVInfo.src.width -
                            (mOVInfo.src_rect.x + mOVInfo.src_rect.w);
                    mOVInfo.src_rect.x = tmp;
                    swapOVRotWidthHeight();
            }
            else if (val == MDP_ROT_270) {
                    int tmp = mOVInfo.src_rect.x;
                    mOVInfo.src_rect.x = mOVInfo.src.height - (
                            mOVInfo.src_rect.y + mOVInfo.src_rect.h);
                    mOVInfo.src_rect.y = tmp;
                    swapOVRotWidthHeight();
            }
            break;
        }
        case HAL_TRANSFORM_ROT_90:
        case (HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_H):
        case (HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_V):
        {
            if (val == MDP_ROT_270) {
                    mOVInfo.src_rect.x = mOVInfo.src.width - (
                            mOVInfo.src_rect.x + mOVInfo.src_rect.w);
                    mOVInfo.src_rect.y = mOVInfo.src.height - (
                    mOVInfo.src_rect.y + mOVInfo.src_rect.h);
            }
            else if (val == MDP_ROT_NOP || val == MDP_ROT_180) {
                    int tmp = mOVInfo.src_rect.x;
                    mOVInfo.src_rect.x = mOVInfo.src.height -
                               (mOVInfo.src_rect.y + mOVInfo.src_rect.h);
                    mOVInfo.src_rect.y = tmp;
                    swapOVRotWidthHeight();
            }
            break;
        }
        case HAL_TRANSFORM_ROT_180:
        {
            if (val == MDP_ROT_270) {
                    int tmp = mOVInfo.src_rect.y;
                    mOVInfo.src_rect.y = mOVInfo.src.width -
                               (mOVInfo.src_rect.x + mOVInfo.src_rect.w);
                    mOVInfo.src_rect.x = tmp;
                    swapOVRotWidthHeight();
            }
            else if (val == MDP_ROT_90) {
                    int tmp = mOVInfo.src_rect.x;
                    mOVInfo.src_rect.x = mOVInfo.src.height - (
                             mOVInfo.src_rect.y + mOVInfo.src_rect.h);
                    mOVInfo.src_rect.y = tmp;
                    swapOVRotWidthHeight();
            }
            break;
        }
        case HAL_TRANSFORM_ROT_270:
        {
            if (val == MDP_ROT_90) {
                    mOVInfo.src_rect.y = mOVInfo.src.height -
                               (mOVInfo.src_rect.y + mOVInfo.src_rect.h);
                    mOVInfo.src_rect.x = mOVInfo.src.width -
                               (mOVInfo.src_rect.x + mOVInfo.src_rect.w);
            }
            else if (val == MDP_ROT_NOP || val == MDP_ROT_180) {
                    int tmp = mOVInfo.src_rect.y;
                    mOVInfo.src_rect.y = mOVInfo.src.width - (
                        mOVInfo.src_rect.x + mOVInfo.src_rect.w);
                    mOVInfo.src_rect.x = tmp;
                    swapOVRotWidthHeight();
            }
            break;
        }
        default: return false;
    }

    int mdp_rotation = get_mdp_orientation(rot);
    if (mdp_rotation == -1)
        return false;

    mOVInfo.user_data[0] = mdp_rotation;
    mRotInfo.rotations = mOVInfo.user_data[0];

    /* Rotator always outputs non-tiled formats.
    If rotator is used, set Overlay input to non-tiled
    Else, overlay input remains tiled */
    if (mOVInfo.user_data[0]) {
        mOVInfo.src.format = get_rot_output_format(mRotInfo.src.format);
        mRotInfo.enable = 1;
    }
    else {
        //We can switch between rotator ON and OFF. Reset overlay
        //i/p format whenever this happens
        if(mRotInfo.dst.format == mOVInfo.src.format)
            mOVInfo.src.format = mRotInfo.src.format;
        mRotInfo.enable = 0;
        //Always enable rotation for UI mirror usecase
        if(mUIChannel)
            mRotInfo.enable = 1;
    }

    if (ioctl(mRotFD, MSM_ROTATOR_IOCTL_START, &mRotInfo)) {
        reportError("setTransform, rotator start failed");
        return false;
    }

    if ((mOVInfo.user_data[0] == MDP_ROT_90) ||
        (mOVInfo.user_data[0] == MDP_ROT_270))
        mOVInfo.flags |= MDP_SOURCE_ROTATED_90;
    else
        mOVInfo.flags &= ~MDP_SOURCE_ROTATED_90;

    if (ioctl(mFD, MSMFB_OVERLAY_SET, &mOVInfo)) {
        reportError("setTransform, overlay set failed");
        dump(mOVInfo);
        return false;
    }

    return true;
}

bool OverlayControlChannel::getPosition(int& x, int& y,
                                  uint32_t& w, uint32_t& h) {
    if (!isChannelUP())
        return false;
    //mOVInfo has the current Overlay Position
    x = mOVInfo.dst_rect.x;
    y = mOVInfo.dst_rect.y;
    w = mOVInfo.dst_rect.w;
    h = mOVInfo.dst_rect.h;

    return true;
}

bool OverlayControlChannel::getOrientation(int& orientation) const {
    if (!isChannelUP())
        return false;
    // mOVInfo has the current orientation
    orientation = mOVInfo.user_data[0];
    return true;
}
bool OverlayControlChannel::getOvSessionID(int& sessionID) const {
    if (!isChannelUP())
        return false;
    sessionID = mOVInfo.id;
    return true;
}

bool OverlayControlChannel::getRotSessionID(int& sessionID) const {
    if (!isChannelUP())
        return false;
    sessionID = mRotInfo.session_id;
    return true;
}

bool OverlayControlChannel::getSize(int& size) const {
    if (!isChannelUP())
        return false;
    size = mSize;
    return true;
}

OverlayDataChannel::OverlayDataChannel() : mNoRot(false), mFD(-1), mRotFD(-1),
                                  mPmemFD(-1), mPmemAddr(0), mUpdateDataChannel(0)
{
    //XXX: getInstance(false) implies that it should only
    // use the kernel allocator. Change it to something
    // more descriptive later.
    mAlloc = gralloc::IAllocController::getInstance(false);
}

OverlayDataChannel::~OverlayDataChannel() {
    closeDataChannel();
}

bool OverlayDataChannel::startDataChannel(
               const OverlayControlChannel& objOvCtrlChannel,
               int fbnum, bool norot, bool uichannel, int num_buffers) {
    int ovid, rotid, size;
    mNoRot = norot;
    memset(&mOvData, 0, sizeof(mOvData));
    memset(&mOvDataRot, 0, sizeof(mOvDataRot));
    memset(&mRotData, 0, sizeof(mRotData));
    if (objOvCtrlChannel.getOvSessionID(ovid) &&
            objOvCtrlChannel.getRotSessionID(rotid) &&
            objOvCtrlChannel.getSize(size)) {
        return startDataChannel(ovid, rotid, size, fbnum,
                      norot, uichannel, num_buffers);
    }
    else
        return false;
}

bool OverlayDataChannel::openDevices(int fbnum, bool uichannel, int num_buffers) {
    if (fbnum < 0)
        return false;
    char const * const device_template =
                      "/dev/graphics/fb%u";
    char dev_name[64];
    snprintf(dev_name, 64, device_template, fbnum);

    mFD = open(dev_name, O_RDWR, 0);
    if (mFD < 0) {
        reportError("Cant open framebuffer ");
        return false;
    }
    if (!mNoRot) {
        mRotFD = open("/dev/msm_rotator", O_RDWR, 0);
        if (mRotFD < 0) {
            reportError("Cant open rotator device");
            close(mFD);
            mFD = -1;
            return false;
        }

        return mapRotatorMemory(num_buffers, uichannel, NEW_REQUEST);
    }
    return true;
}

bool OverlayDataChannel::mapRotatorMemory(int num_buffers, bool uiChannel, int requestType)
{
    mPmemAddr = MAP_FAILED;

    alloc_data data;
    data.base = 0;
    data.fd = -1;
    data.offset = 0;
    data.size = mPmemOffset * num_buffers;
    data.align = getpagesize();
    data.uncached = true;

    int allocFlags = GRALLOC_USAGE_PRIVATE_MM_HEAP          |
                     GRALLOC_USAGE_PRIVATE_WRITEBACK_HEAP   |
                     GRALLOC_USAGE_PRIVATE_ADSP_HEAP        |
                     GRALLOC_USAGE_PRIVATE_IOMMU_HEAP;
    if((requestType == NEW_REQUEST) && !uiChannel)
        allocFlags |= GRALLOC_USAGE_PRIVATE_SMI_HEAP;

    int err = mAlloc->allocate(data, allocFlags, 0);
    if(err) {
        reportError("Cant allocate rotatory memory");
        close(mFD);
        mFD = -1;
        close(mRotFD);
        mRotFD = -1;
        return false;
    }
    mPmemFD = data.fd;
    mPmemAddr = data.base;
    mBufferType = data.allocType;
#if 0
    // Set this flag if source memory is fb
    if(uiChannel)
        mRotData.src.flags |= MDP_MEMORY_ID_TYPE_FB;
#endif
    mOvDataRot.data.memory_id = mPmemFD;
    mRotData.dst.memory_id = mPmemFD;
    mRotData.dst.offset = 0;
    mNumBuffers = num_buffers;
    mCurrentItem = 0;
    for (int i = 0; i < num_buffers; i++)
        mRotOffset[i] = i * mPmemOffset;

    return true;
}

bool OverlayDataChannel::updateDataChannel(int updateStatus, int size) {
    mUpdateDataChannel = updateStatus;
    mNewPmemOffset = size;
    return true;
}

bool OverlayDataChannel::startDataChannel(int ovid, int rotid, int size,
                                   int fbnum, bool norot,
                                   bool uichannel, int num_buffers) {
    memset(&mOvData, 0, sizeof(mOvData));
    memset(&mOvDataRot, 0, sizeof(mOvDataRot));
    memset(&mRotData, 0, sizeof(mRotData));
    mNoRot = norot;
    mOvData.data.memory_id = -1;
    mOvData.id = ovid;
    mOvDataRot = mOvData;
    mPmemOffset = size;
    mRotData.session_id = rotid;
    mNumBuffers = 0;
    mCurrentItem = 0;

    return openDevices(fbnum, uichannel, num_buffers);
}

bool OverlayDataChannel::closeDataChannel() {
    if (!isChannelUP())
        return true;

    if (!mNoRot && mRotFD > 0) {
        sp<IMemAlloc> memalloc = mAlloc->getAllocator(mBufferType);
        memalloc->free_buffer(mPmemAddr, mPmemOffset * mNumBuffers, 0, mPmemFD);
        close(mPmemFD);
        mPmemFD = -1;
        close(mRotFD);
        mRotFD = -1;
    }
    close(mFD);
    mFD = -1;
    memset(&mOvData, 0, sizeof(mOvData));
    memset(&mOvDataRot, 0, sizeof(mOvDataRot));
    memset(&mRotData, 0, sizeof(mRotData));

    mNumBuffers = 0;
    mCurrentItem = 0;

    return true;
}

bool OverlayDataChannel::setFd(int fd) {
    mOvData.data.memory_id = fd;
    return true;
}

bool OverlayDataChannel::queueBuffer(uint32_t offset) {
    if ((!isChannelUP()) || mOvData.data.memory_id < 0) {
        reportError("QueueBuffer failed, either channel is not set or no file descriptor to read from");
        return false;
    }

    int oldPmemFD = -1;
    void* oldPmemAddr = MAP_FAILED;
    uint32_t oldPmemOffset = -1;
    bool result;
    if (!mNoRot) {
        if (mUpdateDataChannel) {
            oldPmemFD = mPmemFD;
            oldPmemAddr = mPmemAddr;
            oldPmemOffset = mPmemOffset;
            mPmemOffset = mNewPmemOffset;
            mNewPmemOffset = -1;
            // Map the new PMEM memory
            result = mapRotatorMemory(mNumBuffers, 0, UPDATE_REQUEST);
            if (!result) {
                LOGE("queueBuffer: mapRotatorMemory failed");
                return false;
            }
        }
    }

    result = queue(offset);

    // Unmap the old PMEM memory after the queueBuffer has returned
    if (oldPmemFD != -1 && oldPmemAddr != MAP_FAILED) {
        sp<IMemAlloc> memalloc = mAlloc->getAllocator(mBufferType);
        memalloc->free_buffer(oldPmemAddr, oldPmemOffset * mNumBuffers, 0, oldPmemFD);
        oldPmemFD = -1;
    }
    return result;
}

bool OverlayDataChannel::queue(uint32_t offset) {
    msmfb_overlay_data *odPtr;
    mOvData.data.offset = offset;
    odPtr = &mOvData;
    if (!mNoRot) {
        mRotData.src.memory_id = mOvData.data.memory_id;
        mRotData.src.offset = offset;
        mRotData.dst.offset = (mRotData.dst.offset) ? 0 : mPmemOffset;
        mRotData.dst.offset = mRotOffset[mCurrentItem];
        mCurrentItem = (mCurrentItem + 1) % mNumBuffers;

        int result = ioctl(mRotFD,
                       MSM_ROTATOR_IOCTL_ROTATE, &mRotData);

        if (!result) {
            mOvDataRot.data.offset = (uint32_t) mRotData.dst.offset;
            odPtr = &mOvDataRot;
        }
    }

    if (ioctl(mFD, MSMFB_OVERLAY_PLAY, odPtr)) {
        reportError("overlay play failed.");
        return false;
    }

    return true;
}
#if 0
bool OverlayDataChannel::waitForHdmiVsync() {
    if (!isChannelUP()) {
        reportError("waitForHdmiVsync: channel not up");
        return false;
    }
    if (ioctl(mFD, MSMFB_OVERLAY_PLAY_WAIT, &mOvData)) {
        reportError("waitForHdmiVsync: MSMFB_OVERLAY_PLAY_WAIT failed");
        return false;
    }
    return true;
}
#endif
bool OverlayDataChannel::getCropS3D(overlay_rect *inRect, int channel, int format,
                                    overlay_rect *rect) {
    // for the 3D usecase extract channels from a frame
    switch (format & INPUT_MASK_3D) {
    case HAL_3D_IN_SIDE_BY_SIDE_L_R:
        if(channel == 0) {
            rect->x = 0;
            rect->y = 0;
            rect->w = inRect->w/2;
            rect->h = inRect->h;
        } else {
            rect->x = inRect->w/2;
            rect->y = 0;
            rect->w = inRect->w/2;
            rect->h = inRect->h;
        }
        break;
    case HAL_3D_IN_SIDE_BY_SIDE_R_L:
         if(channel == 1) {
            rect->x = 0;
            rect->y = 0;
            rect->w = inRect->w/2;
            rect->h = inRect->h;
        } else {
            rect->x = inRect->w/2;
            rect->y = 0;
            rect->w = inRect->w/2;
            rect->h = inRect->h;
        }
         break;
    case HAL_3D_IN_TOP_BOTTOM:
        if(channel == 0) {
            rect->x = 0;
            rect->y = 0;
            rect->w = inRect->w;
            rect->h = inRect->h/2;
        } else {
            rect->x = 0;
            rect->y = inRect->h/2;
            rect->w = inRect->w;
            rect->h = inRect->h/2;
        }
        break;
    case HAL_3D_IN_INTERLEAVE:
      break;
    default:
        reportError("Unsupported 3D format...");
        break;
   }
   return true;
}

bool OverlayDataChannel::setCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!isChannelUP()) {
        reportError("Channel not set");
        return false;
    }

    mdp_overlay ov;
    ov.id = mOvData.id;
    if (ioctl(mFD, MSMFB_OVERLAY_GET, &ov)) {
        reportError("setCrop, overlay GET failed");
        return false;
    }

    if ((ov.user_data[0] == MDP_ROT_90) ||
        (ov.user_data[0] == (MDP_ROT_90 | MDP_FLIP_UD)) ||
        (ov.user_data[0] == (MDP_ROT_90 | MDP_FLIP_LR))){
        if (ov.src.width < (y + h))
            return false;

        uint32_t tmp = x;
        x = ov.src.width - (y + h);
        y = tmp;

        tmp = w;
        w = h;
        h = tmp;
    }
    else if (ov.user_data[0] == MDP_ROT_270) {
        if (ov.src.height < (x + w))
            return false;

        uint32_t tmp = y;
        y = ov.src.height - (x + w);
        x = tmp;

        tmp = w;
        w = h;
        h = tmp;
    }
    else if(ov.user_data[0] == MDP_ROT_180) {
        if ((ov.src.height < (y + h)) || (ov.src.width < ( x + w)))
            return false;

        x = ov.src.width - (x + w);
        y = ov.src.height - (y + h);
    }


    if ((ov.src_rect.x == x) &&
           (ov.src_rect.y == y) &&
           (ov.src_rect.w == w) &&
           (ov.src_rect.h == h))
        return true;

    normalize_crop(x, w);
    normalize_crop(y, h);

    ov.src_rect.x = x;
    ov.src_rect.y = y;
    ov.src_rect.w = w;
    ov.src_rect.h = h;

    /* Scaling of upto a max of 8 times supported */
    if(ov.dst_rect.w >(ov.src_rect.w * HW_OVERLAY_MAGNIFICATION_LIMIT)){
        ov.dst_rect.w = HW_OVERLAY_MAGNIFICATION_LIMIT * ov.src_rect.w;
    }
    if(ov.dst_rect.h >(ov.src_rect.h * HW_OVERLAY_MAGNIFICATION_LIMIT)) {
        ov.dst_rect.h = HW_OVERLAY_MAGNIFICATION_LIMIT * ov.src_rect.h;
    }
    if (ioctl(mFD, MSMFB_OVERLAY_SET, &ov)) {
        reportError("setCrop, overlay set error");
        return false;
    }

    return true;
}
