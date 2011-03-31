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
/* Helper functions */

static int get_mdp_format(int format) {
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
        return MDP_Y_CBCR_H2V2;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        return MDP_Y_CRCB_H2V2;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
        return MDP_Y_CRCB_H2V2_TILE;
    }
    return -1;
}

static int get_size(int format, int w, int h) {
    int size, aligned_height, pitch;

    size = w * h;
    switch (format) {
    case MDP_RGBA_8888:
    case MDP_BGRA_8888:
    case MDP_RGBX_8888:
        size *= 4;
        break;
    case MDP_RGB_565:
    case MDP_Y_CBCR_H2V1:
        size *= 2;
        break;
    case MDP_Y_CBCR_H2V2:
    case MDP_Y_CRCB_H2V2:
        size = (size * 3) / 2;
        break;
    case MDP_Y_CRCB_H2V2_TILE:
        aligned_height = (h + 31) & ~31;
        pitch = (w + 127) & ~127;
        size = pitch * aligned_height;
        size = (size + 8191) & ~8191;

        aligned_height = ((h >> 1) + 31) & ~31;
        size += pitch * aligned_height;
        size = (size + 8191) & ~8191;
        break;
    default:
        return 0;
    }
    return size;
}

static int get_mdp_orientation(int rotation, int flip) {
    switch(flip) {
    case HAL_TRANSFORM_FLIP_V:
        switch(rotation) {
        case 0: return MDP_FLIP_UD;
        case HAL_TRANSFORM_ROT_90:  return (MDP_ROT_90 | MDP_FLIP_UD);
        default: return -1;
        break;
        }
    break;
    case HAL_TRANSFORM_FLIP_H:
        switch(rotation) {
        case 0: return MDP_FLIP_LR;
        case HAL_TRANSFORM_ROT_90:  return (MDP_ROT_90 | MDP_FLIP_LR);
        default: return -1;
        break;
        }
    break;
    default:
        switch(rotation) {
        case 0: return MDP_ROT_NOP;
        case HAL_TRANSFORM_ROT_90:  return MDP_ROT_90;
        case HAL_TRANSFORM_ROT_180: return MDP_ROT_180;
        case HAL_TRANSFORM_ROT_270: return MDP_ROT_270;
        default: return -1;
        break;
        }
    break;
    }
    return -1;
}

static bool isRGBType(int format) {
    switch (format) {
        case MDP_RGBA_8888:
        case MDP_BGRA_8888:
        case MDP_RGBX_8888:
        case MDP_RGB_565:
            return true;
    }
    return false;
}

static int getRGBBpp(int format) {
    switch (format) {
        case MDP_RGBA_8888:
        case MDP_BGRA_8888:
        case MDP_RGBX_8888:
            return 4;
        case MDP_RGB_565:
            return 2;
    }
    return -1;
}

#define LOG_TAG "OverlayLIB"
static void reportError(const char* message) {
    LOGE( "%s", message);
}

using namespace overlay;

Overlay::Overlay() : mChannelUP(false), mHDMIConnected(false),
                     mCloseChannel(false), mS3DFormat(0) {
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

bool Overlay::startChannel(int w, int h, int format, int fbnum,
                              bool norot, bool uichannel,
                              unsigned int format3D, int channel,
                              bool ignoreFB, int num_buffers) {
    int zorder = 0;

    if (format3D)
        zorder = channel;
    mChannelUP = objOvCtrlChannel[channel].startControlChannel(w, h, format, fbnum,
                                                   norot, uichannel, format3D, zorder, ignoreFB);
    if (!mChannelUP) {
        LOGE("startChannel for fb%d failed", fbnum);
        return mChannelUP;
    }
    return objOvDataChannel[channel].startDataChannel(objOvCtrlChannel[channel], fbnum,
                                            norot, uichannel, num_buffers);
}

bool Overlay::startChannelHDMI(int w, int h, int format, bool norot) {

    bool ret = startChannel(w, h, format, FRAMEBUFFER_0, norot);
    if(ret) {
        ret = startChannel(w, h, format, FRAMEBUFFER_1, true, 0, 0, VG1_PIPE);
        overlay_rect rect;
        if(ret && objOvCtrlChannel[VG1_PIPE].getAspectRatioPosition(w, h, format, &rect)) {
            if(!setChannelPosition(rect.x, rect.y, rect.width, rect.height, VG1_PIPE)) {
                LOGE("Failed to upscale for framebuffer 1");
                return false;
            }
        }
    }
    return ret;
}

bool Overlay::startChannelS3D(int w, int h, int format, bool norot, int s3DFormat) {

    if (!mHDMIConnected) {
        // S3D without HDMI is not supported yet
        return true;
    }
    // Start  both the channels for the S3D content
    bool ret = startChannel(w, h, format, FRAMEBUFFER_1, norot, 0, mS3DFormat, VG0_PIPE);
    if (ret) {
        ret = startChannel(w, h, format, FRAMEBUFFER_1, norot, 0, mS3DFormat, VG1_PIPE);
    }
    if (ret) {
        FILE *fp = fopen(FORMAT_3D_FILE, "wb");
        if (fp) {
            fprintf(fp, "%d", mS3DFormat & OUTPUT_MASK_3D);
            fclose(fp);
            fp = NULL;
        }
    }

    if (!ret) {
        closeChannel();
    }
    return ret;
}

bool Overlay::closeChannel() {

    if (!mCloseChannel && !mChannelUP)
        return true;

    if(mS3DFormat) {
        FILE *fp = fopen(FORMAT_3D_FILE, "wb");
        if (fp) {
                fprintf(fp, "0");
            fclose(fp);
            fp = NULL;
        }
    }
    for (int i = 0; i < NUM_CHANNELS; i++) {
        objOvCtrlChannel[i].closeControlChannel();
        objOvDataChannel[i].closeDataChannel();
    }
    mChannelUP = false;
    mCloseChannel = false;
    mHDMIConnected = false;
    mS3DFormat = 0;
    return true;
}

bool Overlay::getPosition(int& x, int& y, uint32_t& w, uint32_t& h, int channel) {
    return objOvCtrlChannel[channel].getPosition(x, y, w, h);
}

bool Overlay::getOrientation(int& orientation, int channel) const {
    return objOvCtrlChannel[channel].getOrientation(orientation);
}

bool Overlay::setPosition(int x, int y, uint32_t w, uint32_t h) {
    if(mS3DFormat && mHDMIConnected) {
        return setPositionS3D(x, y, w, h);
    } else {
        return setChannelPosition(x, y, w, h, VG0_PIPE);
    }
}

bool Overlay::setChannelPosition(int x, int y, uint32_t w, uint32_t h, int channel) {
    return objOvCtrlChannel[channel].setPosition(x, y, w, h);
}

bool Overlay::setPositionS3D(int x, int y, uint32_t w, uint32_t h) {
    bool ret = false;

    for (int i = 0; i < NUM_CHANNELS; i++) {
        overlay_rect rect;
        ret = objOvCtrlChannel[i].getPositionS3D(i, mS3DFormat, &rect);
        if (ret) {
            setChannelPosition(rect.x, rect.y, rect.width, rect.height, i);
        }
    }
    return ret;
}

bool Overlay::setSource(uint32_t w, uint32_t h, int format, int orientation,
                        bool hdmiConnected, bool ignoreFB, int num_buffers) {
    if (mCloseChannel)
        closeChannel();

    // Separate the color format from the 3D format.
    // If there is 3D content; the effective format passed by the client is:
    // effectiveFormat = 3D_IN | 3D_OUT | ColorFormat
    unsigned int format3D = FORMAT_3D(format);
    int colorFormat = COLOR_FORMAT(format);
    int fIn3D = FORMAT_3D_INPUT(format3D); // MSB 2 bytes are input format
    int fOut3D = FORMAT_3D_OUTPUT(format3D); // LSB 2 bytes are output format
    format3D = fIn3D | fOut3D;
    // Use the same in/out format if not mentioned
    if (!fIn3D) {
        format3D |= fOut3D << SHIFT_3D; //Set the input format
    }
    if (!fOut3D) {
        format3D |= fIn3D >> SHIFT_3D; //Set the output format
    }

    int stateChanged = 0;
    int hw_format = get_mdp_format(colorFormat);
    int s3dChanged =0, hdmiChanged = 0;

    if (format3D != mS3DFormat)
       s3dChanged = 0x10;
    if (hdmiConnected != mHDMIConnected)
       hdmiChanged = 0x1;

    stateChanged = s3dChanged|hdmiChanged;
    if (stateChanged || !objOvCtrlChannel[0].setSource(w, h, colorFormat, orientation, ignoreFB)) {
        if (mChannelUP && isRGBType(hw_format) && (stateChanged != 0x10)) {
            mCloseChannel = true;
            return false;
        }
        closeChannel();
        mHDMIConnected = hdmiConnected;
        mS3DFormat = format3D;

        if (mHDMIConnected) {
            if (format3D) {
                // Start both the VG pipes
                return startChannelS3D(w, h, colorFormat, !orientation, format3D);
            } else {
                return startChannelHDMI(w, h, colorFormat, !orientation);
            }
        } else {
            return startChannel(w, h, colorFormat, 0, !orientation,
                                false, 0, VG0_PIPE, ignoreFB, num_buffers);
        }
    }
    else
        return true;
}

bool Overlay::setCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!mChannelUP)
        return false;
    bool ret;

    if (mHDMIConnected) {
        if (mS3DFormat) {
            overlay_rect rect;
            overlay_rect inRect;
            inRect.x = x; inRect.y = y; inRect.width = w; inRect.height = h;

            // Set the crop for both VG pipes
            for (int i = 0; i < NUM_CHANNELS; i++) {
                objOvDataChannel[i].getCropS3D(&inRect, i, mS3DFormat, &rect);
                ret = setChannelCrop(rect.x, rect.y, rect.width, rect.height, i);
            }
            return ret;
        } else {
            setChannelCrop(x, y, w, h, VG1_PIPE);
        }
    }
    return setChannelCrop(x, y, w, h, VG0_PIPE);
}

bool Overlay::setChannelCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h, int channel) {
    return objOvDataChannel[channel].setCrop(x, y, w, h);
}

bool Overlay::setParameter(int param, int value) {
    if (mS3DFormat && mHDMIConnected)
        return setParameterS3D(param, value);
    else {
        return objOvCtrlChannel[VG0_PIPE].setParameter(param, value);
    }
}

bool Overlay::setParameterS3D(int param, int value) {
    bool ret = false;
    if (mHDMIConnected) {
        // Set the S3D parameter for both VG pipes
        ret = objOvCtrlChannel[VG0_PIPE].setParameter(param, value);
        if (ret)
            ret = objOvCtrlChannel[VG1_PIPE].setParameter(param, value);
    }

    return ret;
}

bool Overlay::setOrientation(int value, int channel) {
    return objOvCtrlChannel[channel].setParameter(OVERLAY_TRANSFORM, value);
}

bool Overlay::setFd(int fd, int channel) {
    return objOvDataChannel[channel].setFd(fd);
}

bool Overlay::queueBuffer(uint32_t offset, int channel) {
    return objOvDataChannel[channel].queueBuffer(offset);
}

bool Overlay::queueBuffer(buffer_handle_t buffer) {
    private_handle_t const* hnd = reinterpret_cast
                                   <private_handle_t const*>(buffer);
    const size_t offset = hnd->offset;
    const int fd = hnd->fd;
    bool ret = true;

    if (mHDMIConnected) {
         if (mS3DFormat) {
            // Queue the buffer on VG1 pipe
            if ((mS3DFormat & HAL_3D_OUT_SIDE_BY_SIDE_HALF_MASK) ||
                (mS3DFormat & HAL_3D_OUT_TOP_BOTTOM_MASK)) {
                ret = queueBuffer(fd, offset, VG1_PIPE);
            }
        } else {
            ret = queueBuffer(fd, offset, VG1_PIPE);
        }
    }
    if (ret && setFd(fd)) {
        return queueBuffer(offset);
    }
    return false;
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

OverlayControlChannel::OverlayControlChannel() : mNoRot(false), mFD(-1), mRotFD(-1), mFormat3D(0) {
    memset(&mOVInfo, 0, sizeof(mOVInfo));
    memset(&mRotInfo, 0, sizeof(mRotInfo));
}


OverlayControlChannel::~OverlayControlChannel() {
    closeControlChannel();
}

bool OverlayControlChannel::getAspectRatioPosition(int w, int h, int format, overlay_rect *rect)
{
    int width = w, height = h, x, y;
    int fbWidthHDMI = getFBWidth();
    int fbHeightHDMI = getFBHeight();
    // width and height for YUV TILE format
    int tempWidth = w, tempHeight = h;
    /* Calculate the width and height if it is YUV TILE format*/
    if(format == HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED) {
        tempWidth = w - ( (((w-1)/64 +1)*64) - w);
        tempHeight = h - ((((h-1)/32 +1)*32) - h);
    }
    if (width * fbHeightHDMI > fbWidthHDMI * height) {
        height = fbWidthHDMI * height / width;
        EVEN_OUT(height);
        width = fbWidthHDMI;
    } else if (width * fbHeightHDMI < fbWidthHDMI * height) {
        width = fbHeightHDMI * width / height;
        EVEN_OUT(width);
        height = fbHeightHDMI;
    } else {
        width = fbWidthHDMI;
        height = fbHeightHDMI;
    }
    /* Scaling of upto a max of 8 times supported */
    if(width >(tempWidth * HW_OVERLAY_MAGNIFICATION_LIMIT)){
        width = HW_OVERLAY_MAGNIFICATION_LIMIT * tempWidth;
    }
    if(height >(tempHeight*HW_OVERLAY_MAGNIFICATION_LIMIT)) {
        height = HW_OVERLAY_MAGNIFICATION_LIMIT * tempHeight;
    }
    if (width > fbWidthHDMI) width = fbWidthHDMI;
    if (height > fbHeightHDMI) height = fbHeightHDMI;
    x = (fbWidthHDMI - width) / 2;
    y = (fbHeightHDMI - height) / 2;
    rect->x = x;
    rect->y = y;
    rect->width = width;
    rect->height = height;
    return true;
}

bool OverlayControlChannel::getPositionS3D(int channel, int format, overlay_rect *rect) {
    int wHDMI = getFBWidth();
    int hHDMI = getFBHeight();

    if (format & HAL_3D_OUT_SIDE_BY_SIDE_HALF_MASK) {
        if (channel == VG0_PIPE) {
            rect->x = 0;
            rect->y = 0;
            rect->width = wHDMI/2;
            rect->height = hHDMI;
        } else {
            rect->x = wHDMI/2;
            rect->y = 0;
            rect->width = wHDMI/2;
            rect->height = hHDMI;
        }
    } else if (format & HAL_3D_OUT_TOP_BOTTOM_MASK) {
        if (channel == VG0_PIPE) {
            rect->x = 0;
            rect->y = 0;
            rect->width = wHDMI;
            rect->height = hHDMI/2;
        } else {
            rect->x = 0;
            rect->y = hHDMI/2;
            rect->width = wHDMI;
            rect->height = hHDMI/2;
        }
    } else if (format & HAL_3D_OUT_INTERLEAVE_MASK) {
       //TBD
    } else if (format & HAL_3D_OUT_SIDE_BY_SIDE_FULL_MASK) {
        //TBD
    } else {
       reportError("Unsupported 3D output format");
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

bool OverlayControlChannel::setOverlayInformation(int w, int h,
                                  int format, int flags, int zorder,
                                  bool ignoreFB) {
    int origW, origH, xoff, yoff;

    mOVInfo.id = MSMFB_NEW_REQUEST;
    mOVInfo.src.width  = w;
    mOVInfo.src.height = h;
    mOVInfo.src_rect.x = 0;
    mOVInfo.src_rect.y = 0;
    mOVInfo.dst_rect.x = 0;
    mOVInfo.dst_rect.y = 0;
    mOVInfo.dst_rect.w = w;
    mOVInfo.dst_rect.h = h;
    if(format == MDP_Y_CRCB_H2V2_TILE) {
        if (mNoRot) {
           mOVInfo.src_rect.w = w - ( (((w-1)/64 +1)*64) - w);
           mOVInfo.src_rect.h = h - ((((h-1)/32 +1)*32) - h);
           mOVInfo.src.format = MDP_Y_CRCB_H2V2_TILE;
        } else {
           mOVInfo.src_rect.w = w;
           mOVInfo.src_rect.h = h;
           mOVInfo.src.width  = (((w-1)/64 +1)*64);
           mOVInfo.src.height = (((h-1)/32 +1)*32);
           mOVInfo.src_rect.x = mOVInfo.src.width - w;
           mOVInfo.src_rect.y = mOVInfo.src.height - h;
           mOVInfo.src.format = MDP_Y_CRCB_H2V2;
        }
    } else {
        mOVInfo.src_rect.w = w;
        mOVInfo.src_rect.h = h;
        mOVInfo.src.format = format;
    }

    if (w > mFBWidth)
        mOVInfo.dst_rect.w = mFBWidth;
    if (h > mFBHeight)
        mOVInfo.dst_rect.h = mFBHeight;
    mOVInfo.z_order = zorder;
    mOVInfo.alpha = 0xff;
    mOVInfo.transp_mask = 0xffffffff;
    mOVInfo.flags = flags;
    if (!ignoreFB)
        mOVInfo.flags |= MDP_OV_PLAY_NOWAIT;
    mSize = get_size(format, w, h);
    return true;
}

bool OverlayControlChannel::startOVRotatorSessions(int w, int h,
                                          int format) {
    bool ret = true;

    if (!mNoRot) {
        if (isRGBType(format) && mOVInfo.is_fg) {
            w = (w + 31) & ~31;
            h = (h + 31) & ~31;
        }
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
        } else {
           mRotInfo.dst.format = format;
        }
        mRotInfo.dst_x = 0;
        mRotInfo.dst_y = 0;
        mRotInfo.src_rect.x = 0;
        mRotInfo.src_rect.y = 0;
        mRotInfo.rotations = 0;
        mRotInfo.enable = 0;
        if(mUIChannel)
            mRotInfo.enable = 1;
        mRotInfo.session_id = 0;
	int result = ioctl(mRotFD, MSM_ROTATOR_IOCTL_START, &mRotInfo);
	if (result) {
            reportError("Rotator session failed");
	    ret = false;
	}
    }

    if (!mNoRot && isRGBType(format) && mOrientation && mOVInfo.is_fg) {
        mOVInfo.dst_rect.w = mFBWidth;
        mOVInfo.dst_rect.h = mFBHeight;
        ret = setParameter(OVERLAY_TRANSFORM, mOrientation, false);
        if (!ret) {
            reportError("Overlay set failed.. ");
            return false;
        }
    }
    else if (ioctl(mFD, MSMFB_OVERLAY_SET, &mOVInfo)) {
        reportError("startOVRotatorSessions, Overlay set failed");
        ret = false;
    }

    if (!ret)
        closeControlChannel();

    return ret;
}

bool OverlayControlChannel::startControlChannel(int w, int h,
                                           int format, int fbnum, bool norot,
                                           bool uichannel,
                                           unsigned int format3D, int zorder,
                                           bool ignoreFB) {
    mNoRot = norot;
    mUIChannel = uichannel;
    fb_fix_screeninfo finfo;
    fb_var_screeninfo vinfo;
    int hw_format;
    int flags = 0;
    int colorFormat = format;
    if (format & INTERLACE_MASK) {
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
    if (!mFormat3D) {
        // Set the share bit for sharing the VG pipe
        flags |= MDP_OV_PIPE_SHARE;
    }
    if (!openDevices(fbnum))
        return false;

    if (!setOverlayInformation(w, h, hw_format, flags, zorder, ignoreFB))
        return false;

    return startOVRotatorSessions(w, h, hw_format);
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
    int ret = ioctl(mFD, MSMFB_OVERLAY_UNSET, &ovid);
    close(mFD);
    memset(&mOVInfo, 0, sizeof(mOVInfo));
    memset(&mRotInfo, 0, sizeof(mRotInfo));
    mFD = -1;

    return true;
}

bool OverlayControlChannel::setSource(uint32_t w, uint32_t h,
                        int cFormat, int orientation, bool ignoreFB) {
    int format = cFormat & INTERLACE_MASK ?
                (cFormat ^ HAL_PIXEL_FORMAT_INTERLACE) : cFormat;
    format = get_mdp_format(format);
    if ((orientation == mOrientation)
            && ((orientation == OVERLAY_TRANSFORM_ROT_90)
            || (orientation == OVERLAY_TRANSFORM_ROT_270))) {
        if (format == MDP_Y_CRCB_H2V2_TILE) {
            format = MDP_Y_CRCB_H2V2;
            w = (((w-1)/64 +1)*64);
            h = (((h-1)/32 +1)*32);
        }
        int tmp = w;
        w = h;
        h = tmp;
    }
    if (w == mOVInfo.src.width && h == mOVInfo.src.height
            && format == mOVInfo.src.format && orientation == mOrientation) {
        mdp_overlay ov;
        ov.id = mOVInfo.id;
        if (ioctl(mFD, MSMFB_OVERLAY_GET, &ov))
            return false;
        mOVInfo = ov;
        int flags = mOVInfo.flags;

        if (!ignoreFB)
            mOVInfo.flags |= MDP_OV_PLAY_NOWAIT;
        else
            mOVInfo.flags &= ~MDP_OV_PLAY_NOWAIT;

        if (flags != mOVInfo.flags) {
            if (ioctl(mFD, MSMFB_OVERLAY_SET, &mOVInfo))
                return false;
        }

        return true;
    }
    mOrientation = orientation;
    return false;
}

bool OverlayControlChannel::setPosition(int x, int y, uint32_t w, uint32_t h) {

    int width = w, height = h;
    if (!isChannelUP() ||
           (x < 0) || (y < 0) || ((x + w) > mFBWidth) ||
           ((y + h) > mFBHeight)) {
        reportError("setPosition failed");
        return false;
    }

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
	return false;
    }
    mOVInfo = ov;

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

bool OverlayControlChannel::setParameter(int param, int value, bool fetch) {
    if (!isChannelUP())
        return false;

    mdp_overlay ov = mOVInfo;
    if (fetch && ioctl(mFD, MSMFB_OVERLAY_GET, &ov)) {
        reportError("setParameter, overlay GET failed");
        return false;
    }
    mOVInfo = ov;

    switch (param) {
    case OVERLAY_DITHER:
        break;
    case OVERLAY_TRANSFORM:
    {
        int val = mOVInfo.user_data[0];
        if (value && mNoRot)
            return true;

        int rot = value;
        int flip = 0;

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
            rot = 0;
            flip = value & (HAL_TRANSFORM_FLIP_H|HAL_TRANSFORM_FLIP_V);
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
            rot = HAL_TRANSFORM_ROT_90;
            flip = value & (HAL_TRANSFORM_FLIP_H|HAL_TRANSFORM_FLIP_V);
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
    int mdp_rotation = get_mdp_orientation(rot, flip);
    if (mdp_rotation == -1)
        return false;

    mOVInfo.user_data[0] = mdp_rotation;
    mRotInfo.rotations = mOVInfo.user_data[0];

    if (mOVInfo.user_data[0])
        mRotInfo.enable = 1;
    else {
        if(mRotInfo.src.format == MDP_Y_CRCB_H2V2_TILE)
            mOVInfo.src.format = MDP_Y_CRCB_H2V2_TILE;
        mRotInfo.enable = 0;
        if(mUIChannel)
            mRotInfo.enable = 1;
    }
    if (ioctl(mRotFD, MSM_ROTATOR_IOCTL_START, &mRotInfo)) {
        reportError("setParameter, rotator start failed");
        return false;
    }

    if ((mOVInfo.user_data[0] == MDP_ROT_90) ||
        (mOVInfo.user_data[0] == MDP_ROT_270))
        mOVInfo.flags |= MDP_SOURCE_ROTATED_90;

    if (ioctl(mFD, MSMFB_OVERLAY_SET, &mOVInfo)) {
        reportError("setParameter, overlay set failed");
        return false;
    }
        break;
    }
    default:
        reportError("Unsupproted param");
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

    mdp_overlay ov;
    ov.id = mOVInfo.id;
    if (ioctl(mFD, MSMFB_OVERLAY_GET, &ov)) {
        reportError("getOrientation, overlay GET failed");
        return false;
    }
    orientation = ov.user_data[0];
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
                                  mPmemFD(-1), mPmemAddr(0) {
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

        mPmemAddr = MAP_FAILED;

        if(!uichannel) {
            mPmemFD = open("/dev/pmem_smipool", O_RDWR | O_SYNC);
            if(mPmemFD >= 0)
                mPmemAddr = (void *) mmap(NULL, mPmemOffset * num_buffers, PROT_READ | PROT_WRITE,
                         MAP_SHARED, mPmemFD, 0);
        }

        if (mPmemAddr == MAP_FAILED) {
            mPmemFD = open("/dev/pmem_adsp", O_RDWR | O_SYNC);
            if (mPmemFD < 0) {
                reportError("Cant open pmem_adsp ");
                close(mFD);
                mFD = -1;
                close(mRotFD);
                mRotFD = -1;
                return false;
           } else {
                mPmemAddr = (void *) mmap(NULL, mPmemOffset * num_buffers, PROT_READ | PROT_WRITE,
                    MAP_SHARED, mPmemFD, 0);
                if (mPmemAddr == MAP_FAILED) {
                    reportError("Cant map pmem_adsp ");
                    close(mFD);
                    mFD = -1;
                    close(mPmemFD);
                    mPmemFD = -1;
                    close(mRotFD);
                    mRotFD = -1;
                    return false;
                }
            }
        }

        mOvDataRot.data.memory_id = mPmemFD;
        mRotData.dst.memory_id = mPmemFD;
        mRotData.dst.offset = 0;
        mNumBuffers = num_buffers;
        mCurrentItem = 0;
        for (int i = 0; i < num_buffers; i++)
            mRotOffset[i] = i * mPmemOffset;
    }

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
        munmap(mPmemAddr, mPmemOffset * mNumBuffers);
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
        else if (max_num_buffers == mNumBuffers) {
            reportError("Rotator failed..");
            return false;
        }
    }

    if (ioctl(mFD, MSMFB_OVERLAY_PLAY, odPtr)) {
        reportError("overlay play failed.");
	return false;
    }

    return true;
}

bool OverlayDataChannel::getCropS3D(overlay_rect *inRect, int channel, int format,
                                    overlay_rect *rect) {

    bool ret;
    // for the 3D usecase extract L and R channels from a frame
    if ( (format & HAL_3D_IN_SIDE_BY_SIDE_HALF_L_R) ||
         (format & HAL_3D_IN_SIDE_BY_SIDE_HALF_R_L) ) {
        if(channel == 0) {
            rect->x = 0;
            rect->y = 0;
            rect->width = inRect->width/2;
            rect->height = inRect->height;
        } else {
            rect->x = inRect->width/2;
            rect->y = 0;
            rect->width = inRect->width/2;
            rect->height = inRect->height;
        }
    } else if (format & HAL_3D_IN_TOP_BOTTOM) {
        if(channel == 0) {
            rect->x = 0;
            rect->y = 0;
            rect->width = inRect->width;
            rect->height = inRect->height/2;
        } else {
            rect->x = 0;
            rect->y = inRect->height/2;
            rect->width = inRect->width;
            rect->height = inRect->height/2;
        }
   } else if (format & HAL_3D_IN_INTERLEAVE) {
      //TBD
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

    if (ov.user_data[0] == MDP_ROT_90) {
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
