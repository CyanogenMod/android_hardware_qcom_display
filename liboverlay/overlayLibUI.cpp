/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
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

#include "overlayLibUI.h"
#include "gralloc_priv.h"
#define LOG_TAG "OverlayUI"

using android::sp;
using gralloc::IMemAlloc;
using gralloc::alloc_data;

namespace {
/* helper functions */
void swapOVRotWidthHeight(msm_rotator_img_info& rotInfo,
                                 mdp_overlay& ovInfo) {
    int srcWidth = ovInfo.src.width;
    ovInfo.src.width = ovInfo.src.height;
    ovInfo.src.height = srcWidth;

    int srcRectWidth = ovInfo.src_rect.w;
    ovInfo.src_rect.w = ovInfo.src_rect.h;
    ovInfo.src_rect.h = srcRectWidth;

    int dstWidth = rotInfo.dst.width;
    rotInfo.dst.width = rotInfo.dst.height;
    rotInfo.dst.height = dstWidth;
}

bool isRGBType(int format) {
    bool ret = false;
    switch(format) {
        case MDP_RGBA_8888:
        case MDP_BGRA_8888:
        case MDP_RGBX_8888:
        case MDP_RGB_565:
            ret = true;
            break;
        default:
            ret = false;
            break;
    }
    return ret;
}

int getRGBBpp(int format) {
    int ret = -1;
    switch(format) {
        case MDP_RGBA_8888:
        case MDP_BGRA_8888:
        case MDP_RGBX_8888:
            ret = 4;
            break;
        case MDP_RGB_565:
            ret = 2;
            break;
        default:
            ret = -1;
            break;
    }

    return ret;
}

};

namespace overlay {

status_t Display::openDisplay(int fbnum) {
    if (mFD != NO_INIT)
        return NO_ERROR;

    status_t ret = NO_INIT;
    char dev_name[64];
    snprintf(dev_name, 64, FB_DEVICE_TEMPLATE, fbnum);

    mFD = open(dev_name, O_RDWR, 0);
    if (mFD < 0) {
        LOGE("Failed to open FB %d", fbnum);
        return ret;
    }

    fb_var_screeninfo vinfo;
    if (ioctl(mFD, FBIOGET_VSCREENINFO, &vinfo)) {
        LOGE("FBIOGET_VSCREENINFO on failed on FB %d", fbnum);
        close(mFD);
        mFD = NO_INIT;
        return ret;
    }

    mFBWidth = vinfo.xres;
    mFBHeight = vinfo.yres;
    mFBBpp = vinfo.bits_per_pixel;
    ret = NO_ERROR;

    return ret;
}

void Display::closeDisplay() {
    if(mFD > 0) {
        close(mFD);
        mFD = NO_INIT;
    }
}

Rotator::Rotator() : mFD(NO_INIT), mSessionID(NO_INIT), mPmemFD(NO_INIT)
{
    mAlloc = gralloc::IAllocController::getInstance(false);
}

Rotator::~Rotator()
{
    closeRotSession();
}

status_t Rotator::startRotSession(msm_rotator_img_info& rotInfo,
                                   int size, int numBuffers) {
    status_t ret = NO_ERROR;
    if (mSessionID == NO_INIT && mFD == NO_INIT) {
        mNumBuffers = numBuffers;
        mFD = open("/dev/msm_rotator", O_RDWR, 0);
        if (mFD < 0) {
            LOGE("Couldnt open rotator device");
            return NO_INIT;
        }

        if (ioctl(mFD, MSM_ROTATOR_IOCTL_START, &rotInfo)) {
            close(mFD);
            mFD = NO_INIT;
            return NO_INIT;
        }

        mSessionID = rotInfo.session_id;
        alloc_data data;
        data.base = 0;
        data.fd = -1;
        data.offset = 0;
        data.size = mSize * mNumBuffers;
        data.align = getpagesize();
        data.uncached = true;

        int allocFlags = GRALLOC_USAGE_PRIVATE_MM_HEAP          |
                         GRALLOC_USAGE_PRIVATE_WRITEBACK_HEAP   |
                         GRALLOC_USAGE_PRIVATE_ADSP_HEAP        |
                         GRALLOC_USAGE_PRIVATE_IOMMU_HEAP       |
                         GRALLOC_USAGE_PRIVATE_SMI_HEAP         |
                         GRALLOC_USAGE_PRIVATE_DO_NOT_MAP;

        int err = mAlloc->allocate(data, allocFlags, 0);

        if(err) {
            LOGE("%s: Can't allocate rotator memory", __func__);
            closeRotSession();
            return NO_INIT;
        }
        mPmemFD = data.fd;
        mPmemAddr = data.base;
        mBufferType = data.allocType;

        mCurrentItem = 0;
        for (int i = 0; i < mNumBuffers; i++)
            mRotOffset[i] = i * mSize;
        ret = NO_ERROR;
    }
    return ret;
}

status_t Rotator::closeRotSession() {
    if (mSessionID != NO_INIT && mFD != NO_INIT) {
        ioctl(mFD, MSM_ROTATOR_IOCTL_FINISH, &mSessionID);
        close(mFD);
        if (NO_INIT != mPmemFD) {
            sp<IMemAlloc> memalloc = mAlloc->getAllocator(mBufferType);
            memalloc->free_buffer(mPmemAddr, mSize * mNumBuffers, 0, mPmemFD);
            close(mPmemFD);
        }
    }

    mFD = NO_INIT;
    mSessionID = NO_INIT;
    mPmemFD = NO_INIT;
    mPmemAddr = MAP_FAILED;

    return NO_ERROR;
}

status_t Rotator::rotateBuffer(msm_rotator_data_info& rotData) {
    status_t ret = NO_INIT;
    if (mSessionID != NO_INIT) {
        rotData.dst.memory_id = mPmemFD;
        rotData.dst.offset = mRotOffset[mCurrentItem];
        rotData.session_id = mSessionID;
        mCurrentItem = (mCurrentItem + 1) % mNumBuffers;
        if (ioctl(mFD, MSM_ROTATOR_IOCTL_ROTATE, &rotData)) {
            LOGE("Rotator failed to rotate");
            return BAD_VALUE;
        }
        return NO_ERROR;
    }

    return ret;
}

//===================== OverlayUI =================//

OverlayUI::OverlayUI() : mChannelState(CLOSED), mOrientation(NO_INIT),
        mFBNum(NO_INIT), mZorder(NO_INIT), mWaitForVsync(false), mIsFg(false),
        mSessionID(NO_INIT), mParamsChanged(false) {
        memset(&mOvInfo, 0, sizeof(mOvInfo));
        memset(&mRotInfo, 0, sizeof(mRotInfo));
}

OverlayUI::~OverlayUI() {
    closeChannel();
}

void OverlayUI::setSource(const overlay_buffer_info& info, int orientation) {
    status_t ret = NO_INIT;
    int format3D = FORMAT_3D(info.format);
    int colorFormat = COLOR_FORMAT(info.format);
    int format = get_mdp_format(colorFormat);

    if (format3D || !isRGBType(format)) {
        LOGE("%s: Unsupported format", __func__);
        return;
    }

    mParamsChanged |= (mSource.width ^ info.width) ||
                      (mSource.height ^ info.height) ||
                      (mSource.format ^ format) ||
                      (mSource.size ^ info.size) ||
                      (mOrientation ^ orientation);

    mSource.width = info.width;
    mSource.height = info.height;
    mSource.format = format;
    mSource.size = info.size;
    mOrientation = orientation;
    setupOvRotInfo();
}

void OverlayUI::setDisplayParams(int fbNum, bool waitForVsync, bool isFg, int
        zorder, bool isVGPipe, bool premultipliedAlpha) {
    int flags = 0;

    if(false == waitForVsync)
        flags |= MDP_OV_PLAY_NOWAIT;
    else
        flags &= ~MDP_OV_PLAY_NOWAIT;

    if(isVGPipe)
        flags |= MDP_OV_PIPE_SHARE;
    else
        flags &= ~MDP_OV_PIPE_SHARE;
    if(premultipliedAlpha)
        flags |= MDP_BLEND_FG_PREMULT;
    else
        flags &= ~MDP_BLEND_FG_PREMULT;
    //MDP needs this information to set up pixel repeat
    //for VG pipes when upscaling
    flags |= MDP_BACKEND_COMPOSITION;

    mParamsChanged |= (mFBNum ^ fbNum) ||
                      (mOvInfo.is_fg ^ isFg) ||
                      (mOvInfo.flags ^ flags) ||
                      (mOvInfo.z_order ^ zorder);

    mFBNum = fbNum;
    mOvInfo.is_fg = isFg;
    mOvInfo.flags = flags;
    mOvInfo.z_order = zorder;

    mobjDisplay.openDisplay(mFBNum);
}

void OverlayUI::setPosition(int x, int y, int w, int h) {
    mParamsChanged |= (mOvInfo.dst_rect.x ^ x) ||
                      (mOvInfo.dst_rect.y ^ y) ||
                      (mOvInfo.dst_rect.w ^ w) ||
                      (mOvInfo.dst_rect.h ^ h);

    mOvInfo.dst_rect.x = x;
    mOvInfo.dst_rect.y = y;
    mOvInfo.dst_rect.w = w;
    mOvInfo.dst_rect.h = h;
}

void OverlayUI::setCrop(int x, int y, int w, int h) {
    mParamsChanged |= (mOvInfo.src_rect.x ^ x) ||
                      (mOvInfo.src_rect.y ^ y) ||
                      (mOvInfo.src_rect.w ^ w) ||
                      (mOvInfo.src_rect.h ^ h);

    mOvInfo.src_rect.x = x;
    mOvInfo.src_rect.y = y;
    mOvInfo.src_rect.w = w;
    mOvInfo.src_rect.h = h;
}

void OverlayUI::setupOvRotInfo() {
    int w = mSource.width;
    int h = mSource.height;
    int format = mSource.format;
    int srcw = (w + 31) & ~31;
    int srch = (h + 31) & ~31;
    mOvInfo.src.width = srcw;
    mOvInfo.src.height = srch;
    mOvInfo.src.format = format;
    mOvInfo.src_rect.w = w;
    mOvInfo.src_rect.h = h;
    mOvInfo.alpha = 0xff;
    mOvInfo.transp_mask = 0xffffffff;
    mRotInfo.src.format = format;
    mRotInfo.dst.format = format;
    mRotInfo.src.width = srcw;
    mRotInfo.src.height = srch;
    mRotInfo.src_rect.w = srcw;
    mRotInfo.src_rect.h = srch;
    mRotInfo.dst.width = srcw;
    mRotInfo.dst.height = srch;

    int rot = mOrientation;
    switch(rot) {
        case 0:
        case HAL_TRANSFORM_FLIP_H:
        case HAL_TRANSFORM_FLIP_V:
            rot = 0;
            break;
        case HAL_TRANSFORM_ROT_90:
        case (HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_H):
        case (HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_V): {
            int tmp = mOvInfo.src_rect.x;
            mOvInfo.src_rect.x = mOvInfo.src.height -
                (mOvInfo.src_rect.y + mOvInfo.src_rect.h);
            mOvInfo.src_rect.y = tmp;
            swapOVRotWidthHeight(mRotInfo, mOvInfo);
            rot = HAL_TRANSFORM_ROT_90;
            break;
        }
        case HAL_TRANSFORM_ROT_180:
            break;
        case HAL_TRANSFORM_ROT_270: {
            int tmp = mOvInfo.src_rect.y;
            mOvInfo.src_rect.y = mOvInfo.src.width -
                (mOvInfo.src_rect.x + mOvInfo.src_rect.w);
            mOvInfo.src_rect.x = tmp;
            swapOVRotWidthHeight(mRotInfo, mOvInfo);
            break;
        }
        default:
            break;
    }
    int mdp_rotation = overlay::get_mdp_orientation(rot);
    if (mdp_rotation < 0)
        mdp_rotation = 0;
    mOvInfo.user_data[0] = mdp_rotation;
    mRotInfo.rotations = mOvInfo.user_data[0];
    if (mdp_rotation)
        mRotInfo.enable = 1;
}

status_t OverlayUI::commit() {
    status_t ret = BAD_VALUE;
    if(mChannelState != UP)
        mOvInfo.id = MSMFB_NEW_REQUEST;
    ret = startOVSession();
    if (ret == NO_ERROR && mOrientation) {
        ret = mobjRotator.startRotSession(mRotInfo, mSource.size);
    }
    if (ret == NO_ERROR) {
        mChannelState = UP;
    } else {
        LOGE("start channel failed.");
    }
    return ret;
}

status_t OverlayUI::closeChannel() {
    if( mChannelState != UP ) {
        return NO_ERROR;
    }
    if(NO_ERROR != closeOVSession()) {
        LOGE("%s: closeOVSession() failed.", __FUNCTION__);
        return BAD_VALUE;
    }
    if(NO_ERROR != mobjRotator.closeRotSession()) {
        LOGE("%s: closeRotSession() failed.", __FUNCTION__);
        return BAD_VALUE;
    }
    mChannelState = CLOSED;
    mParamsChanged = false;
    memset(&mOvInfo, 0, sizeof(mOvInfo));
    memset(&mRotInfo, 0, sizeof(mRotInfo));
    return NO_ERROR;
}

status_t OverlayUI::startOVSession() {
    status_t ret = NO_INIT;
    ret = mobjDisplay.openDisplay(mFBNum);

    if (ret != NO_ERROR)
        return ret;

    if(mParamsChanged) {
        mParamsChanged = false;
        mdp_overlay ovInfo = mOvInfo;
        if (ioctl(mobjDisplay.getFD(), MSMFB_OVERLAY_SET, &ovInfo)) {
            LOGE("Overlay set failed..");
            ret = BAD_VALUE;
        } else {
            mSessionID = ovInfo.id;
            mOvInfo = ovInfo;
            ret = NO_ERROR;
        }
    }
    return ret;
}

status_t OverlayUI::closeOVSession() {
    status_t ret = NO_ERROR;
    int err = 0;

    if (mSessionID == NO_INIT) {
        mobjDisplay.closeDisplay();
        LOGE("%s : session is not initialized", __FUNCTION__);
        return ret;
    }
    if(err = ioctl(mobjDisplay.getFD(), MSMFB_OVERLAY_UNSET, &mSessionID)) {
        LOGE("%s: MSMFB_OVERLAY_UNSET failed. (%d)", __FUNCTION__, err);
        ret = BAD_VALUE;
    } else {
        mobjDisplay.closeDisplay();
        mSessionID = NO_INIT;
    }
    return ret;
}

status_t OverlayUI::queueBuffer(buffer_handle_t buffer) {
    status_t ret = NO_INIT;

    if (mChannelState != UP)
        return ret;

    if (mSessionID == NO_INIT) {
        LOGE("%s : session is not inited", __FUNCTION__);
        return BAD_VALUE;
    }

    msmfb_overlay_data ovData;
    memset(&ovData, 0, sizeof(ovData));

    private_handle_t const* hnd = reinterpret_cast
                                        <private_handle_t const*>(buffer);
    ovData.data.memory_id = hnd->fd;
    ovData.data.offset = hnd->offset;
    if (mOrientation) {
        msm_rotator_data_info rotData;
        memset(&rotData, 0, sizeof(rotData));
        rotData.src.memory_id = hnd->fd;
        rotData.src.offset = hnd->offset;
        if (mobjRotator.rotateBuffer(rotData) != NO_ERROR) {
            LOGE("Rotator failed.. ");
            return BAD_VALUE;
        }
        ovData.data.memory_id = rotData.dst.memory_id;
        ovData.data.offset = rotData.dst.offset;
    }
    ovData.id = mSessionID;
    if (ioctl(mobjDisplay.getFD(), MSMFB_OVERLAY_PLAY, &ovData)) {
        LOGE("Queuebuffer failed ");
        return BAD_VALUE;
    }
    return NO_ERROR;
}

};
