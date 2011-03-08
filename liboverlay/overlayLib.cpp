/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.
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

#define LOG_TAG "OverlayLIB"
static void reportError(const char* message) {
    LOGE( "%s", message);
}

using namespace overlay;

Overlay::Overlay() : mChannelUP(false) {
}

Overlay::~Overlay() {
    closeChannel();
}

int Overlay::getFBWidth() const {
    return objOvCtrlChannel.getFBWidth();
}

int Overlay::getFBHeight() const {
    return objOvCtrlChannel.getFBHeight();
}

bool Overlay::startChannel(int w, int h, int format, int fbnum, bool norot, bool uichannel, unsigned int format3D) {
    mChannelUP = objOvCtrlChannel.startControlChannel(w, h, format, fbnum, norot, format3D);
    if (!mChannelUP) {
        return mChannelUP;
    }
    return objOvDataChannel.startDataChannel(objOvCtrlChannel, fbnum, norot, uichannel);
}

bool Overlay::closeChannel() {
    if (!mChannelUP)
        return true;
    objOvCtrlChannel.closeControlChannel();
    objOvDataChannel.closeDataChannel();
    mChannelUP = false;
    return true;
}

bool Overlay::getPosition(int& x, int& y, uint32_t& w, uint32_t& h) {
    return objOvCtrlChannel.getPosition(x, y, w, h);
}

bool Overlay::getOrientation(int& orientation) const {
    return objOvCtrlChannel.getOrientation(orientation);
}

bool Overlay::setPosition(int x, int y, uint32_t w, uint32_t h) {
    return objOvCtrlChannel.setPosition(x, y, w, h);
}

bool Overlay::setSource(uint32_t w, uint32_t h, int format, int orientation) {
    if (!objOvCtrlChannel.setSource(w, h, format, orientation)) {
        objOvCtrlChannel.closeControlChannel();
        objOvDataChannel.closeDataChannel();
        mChannelUP = false;
        return startChannel(w, h, format, 0, !orientation);
    }
    else
        return true;
}

bool Overlay::setCrop(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!mChannelUP)
        return false;
    return objOvDataChannel.setCrop(x, y, w, h);
}

bool Overlay::setParameter(int param, int value) {
    return objOvCtrlChannel.setParameter(param, value);
}

bool Overlay::setOrientation(int value) {
    return objOvCtrlChannel.setParameter(OVERLAY_TRANSFORM, value);
}

bool Overlay::setFd(int fd) {
    return objOvDataChannel.setFd(fd);
}

bool Overlay::queueBuffer(uint32_t offset) {
    return objOvDataChannel.queueBuffer(offset);
}

bool Overlay::queueBuffer(buffer_handle_t buffer) {
    private_handle_t const* hnd = reinterpret_cast
                                   <private_handle_t const*>(buffer);
    const size_t offset = hnd->offset;
    const int fd = hnd->fd;
    if (setFd(fd)) {
        return queueBuffer(offset);
    }
    return false;
}

OverlayControlChannel::OverlayControlChannel() : mNoRot(false), mFD(-1), mRotFD(-1), mFormat3D(0) {
    memset(&mOVInfo, 0, sizeof(mOVInfo));
    memset(&mRotInfo, 0, sizeof(mRotInfo));
}


OverlayControlChannel::~OverlayControlChannel() {
    closeControlChannel();
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
                                                 int format, int flags, int zorder) {
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
    mOVInfo.is_fg = 0;
    mSize = get_size(format, w, h);
    return true;
}

bool OverlayControlChannel::startOVRotatorSessions(int w, int h,
                                          int format) {
    bool ret = true;
    if (ioctl(mFD, MSMFB_OVERLAY_SET, &mOVInfo)) {
        reportError("startOVRotatorSessions, Overlay set failed");
        ret = false;
    }
    else if (mNoRot)
        return true;

    if (ret) {
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
        mRotInfo.session_id = 0;
	int result = ioctl(mRotFD, MSM_ROTATOR_IOCTL_START, &mRotInfo);
	if (result) {
            reportError("Rotator session failed");
	    ret = false;
	}
	else
	    return ret;
    }

    closeControlChannel();

    return ret;
}

bool OverlayControlChannel::startControlChannel(int w, int h,
                                           int format, int fbnum, bool norot,
                                           unsigned int format3D, int zorder) {
    mNoRot = norot;
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

    if (!setOverlayInformation(w, h, hw_format, flags, zorder))
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

bool OverlayControlChannel::setSource(uint32_t w, uint32_t h, int format, int orientation) {
    format = get_mdp_format(format);
    if ((orientation == mOrientation) && ((orientation == MDP_ROT_90)
                       || (orientation == MDP_ROT_270))) {
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
            && format == mOVInfo.src.format && orientation == mOrientation)
        return true;
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

bool OverlayControlChannel::setParameter(int param, int value) {
    if (!isChannelUP())
        return false;

    mdp_overlay ov;
    ov.id = mOVInfo.id;
    if (ioctl(mFD, MSMFB_OVERLAY_GET, &ov)) {
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

        switch(value) {
	case 0:
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
		mOVInfo.user_data[0] = MDP_ROT_NOP;
	        break;
	    }
	case OVERLAY_TRANSFORM_ROT_90:
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
		mOVInfo.user_data[0] = MDP_ROT_90;
	        break;
	    }
	case OVERLAY_TRANSFORM_ROT_180:
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
		mOVInfo.user_data[0] = MDP_ROT_180;
	        break;
	    }
	case OVERLAY_TRANSFORM_ROT_270:
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

		mOVInfo.user_data[0] = MDP_ROT_270;
	        break;
	    }
	default: return false;
	}
	mRotInfo.rotations = mOVInfo.user_data[0];
	if (mOVInfo.user_data[0])
	    mRotInfo.enable = 1;
	else {
	    if(mRotInfo.src.format == MDP_Y_CRCB_H2V2_TILE)
		mOVInfo.src.format = MDP_Y_CRCB_H2V2_TILE;
	    mRotInfo.enable = 0;
        }
	if (ioctl(mRotFD, MSM_ROTATOR_IOCTL_START, &mRotInfo)) {
	    reportError("setParameter, rotator start failed");
	    return false;
	}

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

    mdp_overlay ov;
    ov.id = mOVInfo.id;
    if (ioctl(mFD, MSMFB_OVERLAY_GET, &ov)) {
        reportError("getPosition, overlay GET failed");
        return false;
    }
    mOVInfo = ov;

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
	       int fbnum, bool norot, bool uichannel) {
    int ovid, rotid, size;
    mNoRot = norot;
    memset(&mOvData, 0, sizeof(mOvData));
    memset(&mOvDataRot, 0, sizeof(mOvDataRot));
    memset(&mRotData, 0, sizeof(mRotData));
    if (objOvCtrlChannel.getOvSessionID(ovid) &&
            objOvCtrlChannel.getRotSessionID(rotid) &&
	    objOvCtrlChannel.getSize(size)) {
        return startDataChannel(ovid, rotid, size, fbnum, norot, uichannel);
    }
    else
        return false;
}

bool OverlayDataChannel::openDevices(int fbnum, bool uichannel) {
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
                mPmemAddr = (void *) mmap(NULL, mPmemOffset * 2, PROT_READ | PROT_WRITE,
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
                mPmemAddr = (void *) mmap(NULL, mPmemOffset * 2, PROT_READ | PROT_WRITE,
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
    }

    return true;
}

bool OverlayDataChannel::startDataChannel(int ovid, int rotid, int size,
                                   int fbnum, bool norot, bool uichannel) {
    memset(&mOvData, 0, sizeof(mOvData));
    memset(&mOvDataRot, 0, sizeof(mOvDataRot));
    memset(&mRotData, 0, sizeof(mRotData));
    mNoRot = norot;
    mOvData.data.memory_id = -1;
    mOvData.id = ovid;
    mOvDataRot = mOvData;
    mPmemOffset = size;
    mRotData.session_id = rotid;

    return openDevices(fbnum, uichannel);
}

bool OverlayDataChannel::closeDataChannel() {
    if (!isChannelUP())
        return true;

    if (!mNoRot && mRotFD > 0) {
        munmap(mPmemAddr, mPmemOffset * 2);
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
