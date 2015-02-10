/*
 *  Copyright (c) 2013-14, The Linux Foundation. All rights reserved.
 *
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
 * SUBSTITUTE GOODS OR CLIENTS; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <hwc_qclient.h>
#include <IQService.h>
#include <hwc_utils.h>
#include <mdp_version.h>
#include <hwc_mdpcomp.h>
#include <hwc_virtual.h>
#include <overlay.h>
#include <display_config.h>
#include <hdmi.h>
#include <video/msm_hdmi_modes.h>

#define QCLIENT_DEBUG 0

using namespace android;
using namespace qService;
using namespace qhwc;
using namespace overlay;
using namespace qdutils;

namespace qClient {

// ----------------------------------------------------------------------------
QClient::QClient(hwc_context_t *ctx) : mHwcContext(ctx),
        mMPDeathNotifier(new MPDeathNotifier(ctx))
{
    ALOGD_IF(QCLIENT_DEBUG, "QClient Constructor invoked");
}

QClient::~QClient()
{
    ALOGD_IF(QCLIENT_DEBUG,"QClient Destructor invoked");
}

static void securing(hwc_context_t *ctx, uint32_t startEnd) {
    //The only way to make this class in this process subscribe to media
    //player's death.
    IMediaDeathNotifier::getMediaPlayerService();

    ctx->mDrawLock.lock();
    ctx->mSecuring = startEnd;
    //We're done securing
    if(startEnd == IQService::END)
        ctx->mSecureMode = true;
    ctx->mDrawLock.unlock();

    if(ctx->proc)
        ctx->proc->invalidate(ctx->proc);
}

static void unsecuring(hwc_context_t *ctx, uint32_t startEnd) {
    ctx->mDrawLock.lock();
    ctx->mSecuring = startEnd;
    //We're done unsecuring
    if(startEnd == IQService::END)
        ctx->mSecureMode = false;
    ctx->mDrawLock.unlock();

    if(ctx->proc)
        ctx->proc->invalidate(ctx->proc);
}

void QClient::MPDeathNotifier::died() {
    mHwcContext->mDrawLock.lock();
    ALOGD_IF(QCLIENT_DEBUG, "Media Player died");
    mHwcContext->mSecuring = false;
    mHwcContext->mSecureMode = false;
    mHwcContext->mDrawLock.unlock();
    if(mHwcContext->proc)
        mHwcContext->proc->invalidate(mHwcContext->proc);
}

static android::status_t screenRefresh(hwc_context_t *ctx) {
    status_t result = NO_INIT;
    if(ctx->proc) {
        ctx->proc->invalidate(ctx->proc);
        result = NO_ERROR;
    }
    return result;
}

static void setExtOrientation(hwc_context_t *ctx, uint32_t orientation) {
    ctx->mExtOrientation = orientation;
}

static void isExternalConnected(hwc_context_t* ctx, Parcel* outParcel) {
    int connected;
    connected = ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].connected ? 1 : 0;
    outParcel->writeInt32(connected);
}

static void getDisplayAttributes(hwc_context_t* ctx, const Parcel* inParcel,
        Parcel* outParcel) {
    int dpy = inParcel->readInt32();
    outParcel->writeInt32(ctx->dpyAttr[dpy].vsync_period);
    outParcel->writeInt32(ctx->dpyAttr[dpy].xres);
    outParcel->writeInt32(ctx->dpyAttr[dpy].yres);
    outParcel->writeFloat(ctx->dpyAttr[dpy].xdpi);
    outParcel->writeFloat(ctx->dpyAttr[dpy].ydpi);
    //XXX: Need to check what to return for HDMI
    outParcel->writeInt32(ctx->mMDP.panel);
}
static void setHSIC(const Parcel* inParcel) {
    int dpy = inParcel->readInt32();
    ALOGD_IF(0, "In %s: dpy = %d", __FUNCTION__, dpy);
    HSICData_t hsic_data;
    hsic_data.hue = inParcel->readInt32();
    hsic_data.saturation = inParcel->readFloat();
    hsic_data.intensity = inParcel->readInt32();
    hsic_data.contrast = inParcel->readFloat();
    //XXX: Actually set the HSIC data through ABL lib
}


static void setBufferMirrorMode(hwc_context_t *ctx, uint32_t enable) {
    ctx->mBufferMirrorMode = enable;
}

static status_t getDisplayVisibleRegion(hwc_context_t* ctx, int dpy,
                                Parcel* outParcel) {
    // Get the info only if the dpy is valid
    if(dpy >= HWC_DISPLAY_PRIMARY && dpy <= HWC_DISPLAY_VIRTUAL) {
        Locker::Autolock _sl(ctx->mDrawLock);
        if(dpy && (ctx->mExtOrientation || ctx->mBufferMirrorMode)) {
            // Return the destRect on external, if external orienation
            // is enabled
            outParcel->writeInt32(ctx->dpyAttr[dpy].mDstRect.left);
            outParcel->writeInt32(ctx->dpyAttr[dpy].mDstRect.top);
            outParcel->writeInt32(ctx->dpyAttr[dpy].mDstRect.right);
            outParcel->writeInt32(ctx->dpyAttr[dpy].mDstRect.bottom);
        } else {
            outParcel->writeInt32(ctx->mViewFrame[dpy].left);
            outParcel->writeInt32(ctx->mViewFrame[dpy].top);
            outParcel->writeInt32(ctx->mViewFrame[dpy].right);
            outParcel->writeInt32(ctx->mViewFrame[dpy].bottom);
        }
        return NO_ERROR;
    } else {
        ALOGE("In %s: invalid dpy index %d", __FUNCTION__, dpy);
        return BAD_VALUE;
    }
}

// USed for setting the secondary(hdmi/wfd) status
static void setSecondaryDisplayStatus(hwc_context_t *ctx,
                                      const Parcel* inParcel) {
    uint32_t dpy = inParcel->readInt32();
    uint32_t status = inParcel->readInt32();
    ALOGD_IF(QCLIENT_DEBUG, "%s: dpy = %d status = %s", __FUNCTION__,
                                        dpy, getExternalDisplayState(status));

    if(dpy > HWC_DISPLAY_PRIMARY && dpy <= HWC_DISPLAY_VIRTUAL) {
        if(dpy == HWC_DISPLAY_VIRTUAL && status == qdutils::EXTERNAL_OFFLINE) {
            ctx->mWfdSyncLock.lock();
            ctx->mWfdSyncLock.signal();
            ctx->mWfdSyncLock.unlock();
        } else if(status == qdutils::EXTERNAL_PAUSE) {
            handle_pause(ctx, dpy);
        } else if(status == qdutils::EXTERNAL_RESUME) {
            handle_resume(ctx, dpy);
        }
    } else {
        ALOGE("%s: Invalid dpy %d", __FUNCTION__, dpy);
        return;
    }
}


static status_t setViewFrame(hwc_context_t* ctx, const Parcel* inParcel) {
    int dpy = inParcel->readInt32();
    if(dpy >= HWC_DISPLAY_PRIMARY && dpy <= HWC_DISPLAY_VIRTUAL) {
        Locker::Autolock _sl(ctx->mDrawLock);
        ctx->mViewFrame[dpy].left   = inParcel->readInt32();
        ctx->mViewFrame[dpy].top    = inParcel->readInt32();
        ctx->mViewFrame[dpy].right  = inParcel->readInt32();
        ctx->mViewFrame[dpy].bottom = inParcel->readInt32();
        ALOGD_IF(QCLIENT_DEBUG, "%s: mViewFrame[%d] = [%d %d %d %d]",
            __FUNCTION__, dpy,
            ctx->mViewFrame[dpy].left, ctx->mViewFrame[dpy].top,
            ctx->mViewFrame[dpy].right, ctx->mViewFrame[dpy].bottom);
        return NO_ERROR;
    } else {
        ALOGE("In %s: invalid dpy index %d", __FUNCTION__, dpy);
        return BAD_VALUE;
    }
}

static void toggleDynamicDebug(hwc_context_t* ctx, const Parcel* inParcel) {
    int debug_type = inParcel->readInt32();
    bool enable = !!inParcel->readInt32();
    ALOGD("%s: debug_type: %d enable:%d",
            __FUNCTION__, debug_type, enable);
    Locker::Autolock _sl(ctx->mDrawLock);
    switch (debug_type) {
        //break is ignored for DEBUG_ALL to toggle all of them at once
        case IQService::DEBUG_ALL:
        case IQService::DEBUG_MDPCOMP:
            qhwc::MDPComp::dynamicDebug(enable);
            if (debug_type != IQService::DEBUG_ALL)
                break;
        case IQService::DEBUG_VSYNC:
            ctx->vstate.debug = enable;
            if (debug_type != IQService::DEBUG_ALL)
                break;
        case IQService::DEBUG_VD:
            HWCVirtualVDS::dynamicDebug(enable);
            if (debug_type != IQService::DEBUG_ALL)
                break;
        case IQService::DEBUG_PIPE_LIFECYCLE:
            Overlay::debugPipeLifecycle(enable);
            if (debug_type != IQService::DEBUG_ALL)
                break;
    }
}

static void setIdleTimeout(hwc_context_t* ctx, const Parcel* inParcel) {
    uint32_t timeout = (uint32_t)inParcel->readInt32();
    ALOGD("%s :%u ms", __FUNCTION__, timeout);
    Locker::Autolock _sl(ctx->mDrawLock);
    MDPComp::setIdleTimeout(timeout);
}

static void setMaxPipesPerMixer(hwc_context_t* ctx, const Parcel* inParcel) {
    uint32_t value = (uint32_t)inParcel->readInt32();
    ALOGD("%s : setting MaxPipesPerMixer: %d ", __FUNCTION__, value);
    Locker::Autolock _sl(ctx->mDrawLock);
    MDPComp::setMaxPipesPerMixer(value);
}

static void toggleBWC(hwc_context_t* ctx, const Parcel* inParcel) {
    uint32_t enable = (uint32_t)inParcel->readInt32();
    if(MDPVersion::getInstance().supportsBWC()) {
        Locker::Autolock _sl(ctx->mDrawLock);
        ctx->mBWCEnabled = (bool) enable;
        ALOGI("%s: Set BWC to %d", __FUNCTION__, enable);
    } else {
        ALOGI("%s: Target doesn't support BWC", __FUNCTION__);
    }
}

static void configureDynRefreshRate(hwc_context_t* ctx,
                                    const Parcel* inParcel) {
    uint32_t op = (uint32_t)inParcel->readInt32();
    uint32_t refresh_rate = (uint32_t)inParcel->readInt32();
    MDPVersion& mdpHw = MDPVersion::getInstance();
    uint32_t dpy = HWC_DISPLAY_PRIMARY;

    if(mdpHw.isDynFpsSupported()) {
        Locker::Autolock _sl(ctx->mDrawLock);

        switch (op) {
        case DISABLE_METADATA_DYN_REFRESH_RATE:
            ctx->mUseMetaDataRefreshRate = false;
            setRefreshRate(ctx, dpy, ctx->dpyAttr[dpy].refreshRate);
            break;
        case ENABLE_METADATA_DYN_REFRESH_RATE:
            ctx->mUseMetaDataRefreshRate = true;
            setRefreshRate(ctx, dpy, ctx->dpyAttr[dpy].refreshRate);
            break;
        case SET_BINDER_DYN_REFRESH_RATE:
            if(ctx->mUseMetaDataRefreshRate)
                ALOGW("%s: Ignoring binder request to change refresh-rate",
                      __FUNCTION__);
            else {
                uint32_t rate = roundOff(refresh_rate);
                if((rate >= mdpHw.getMinFpsSupported() &&
                    rate <= mdpHw.getMaxFpsSupported())) {
                    setRefreshRate(ctx, dpy, rate);
                } else {
                    ALOGE("%s: Requested refresh-rate should be between \
                          (%d) and (%d). Given (%d)", __FUNCTION__,
                          mdpHw.getMinFpsSupported(),
                          mdpHw.getMaxFpsSupported(), rate);
                }
            }
            break;
        default:
            ALOGE("%s: Invalid op %d",__FUNCTION__,op);
        }
    }
}

static status_t setPartialUpdatePref(hwc_context_t *ctx, uint32_t enable) {
    ALOGD("%s: enable: %d", __FUNCTION__, enable);
    if(qhwc::MDPComp::setPartialUpdatePref(ctx, (bool)enable) < 0)
        return NO_INIT;
    return NO_ERROR;
}

static void toggleScreenUpdate(hwc_context_t* ctx, uint32_t on) {
    ALOGD("%s: toggle update: %d", __FUNCTION__, on);
    if (on == 0) {
        ctx->mDrawLock.lock();
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].isPause = true;
        ctx->mOverlay->configBegin();
        ctx->mOverlay->configDone();
        ctx->mRotMgr->clear();
        if(!Overlay::displayCommit(ctx->dpyAttr[0].fd)) {
            ALOGE("%s: Display commit failed", __FUNCTION__);
        }
        ctx->mDrawLock.unlock();
    } else {
        ctx->mDrawLock.lock();
        ctx->dpyAttr[HWC_DISPLAY_PRIMARY].isPause = false;
        ctx->mDrawLock.unlock();
        ctx->proc->invalidate(ctx->proc);
    }
}

static void setS3DMode(hwc_context_t* ctx, int mode) {
    if (ctx->mHDMIDisplay) {
        if(ctx->mHDMIDisplay->isS3DModeSupported(mode)) {
            ALOGD("%s: Force S3D mode to %d", __FUNCTION__, mode);
            Locker::Autolock _sl(ctx->mDrawLock);
            ctx->dpyAttr[HWC_DISPLAY_EXTERNAL].s3dModeForced = true;
            setup3DMode(ctx, HWC_DISPLAY_EXTERNAL, mode);
        } else {
            ALOGD("%s: mode %d is not supported", __FUNCTION__, mode);
        }
    } else {
        ALOGE("%s: No HDMI Display detected", __FUNCTION__);
    }
}

static status_t setActiveConfig(hwc_context_t* ctx, const Parcel *inParcel,
        Parcel *outParcel) {
    uint32_t index = inParcel->readInt32();
    int dpy = inParcel->readInt32();
    //Currently only primary supported
    if(dpy > HWC_DISPLAY_PRIMARY) {
        return BAD_VALUE;
    }

    Configs *configs = Configs::getInstance();
    if(configs == NULL) {
        ALOGE("%s(): Unable to acquire a Configs instance", __FUNCTION__);
        return INVALID_OPERATION;
    }

    if(configs->getActiveConfig() == index) {
        ALOGI("%s(): Config %u is already set", __FUNCTION__, index);
        return ALREADY_EXISTS;
    }

    ctx->mDrawLock.lock();
    //Updates the necessary sysfs nodes and reads split info again which is
    //needed to reinitialize composition resources.
    if(configs->setActiveConfig(index) == false) {
        ALOGE("%s(): Failed to set config %u", __FUNCTION__, index);
        ctx->mDrawLock.unlock();
        return UNKNOWN_ERROR;
    }

    qdutils::DisplayAttributes attr = configs->getAttributes(index);

    ctx->dpyAttr[dpy].xres = attr.xres;
    ctx->dpyAttr[dpy].yres = attr.yres;

    ctx->dpyAttr[dpy].fbScaling = ((ctx->dpyAttr[dpy].xres !=
            ctx->dpyAttr[dpy].xresFB) || (ctx->dpyAttr[dpy].yres !=
            ctx->dpyAttr[dpy].yresFB));

    destroyCompositionResources(ctx, dpy);
    initCompositionResources(ctx, dpy);
    ctx->dpyAttr[dpy].configSwitched = true;
    ctx->mDrawLock.unlock();
    ctx->proc->invalidate(ctx->proc);
    return NO_ERROR;
}

static status_t getActiveConfig(hwc_context_t* ctx, const Parcel *inParcel,
        Parcel *outParcel) {
    Locker::Autolock _sl(ctx->mDrawLock);
    int dpy = inParcel->readInt32();
    //Currently only primary supported
    if(dpy > HWC_DISPLAY_PRIMARY) {
        return BAD_VALUE;
    }

    Configs *configs = Configs::getInstance();
    if(configs == NULL) {
        ALOGE("%s(): Unable to acquire a Configs instance", __FUNCTION__);
        return INVALID_OPERATION;
    }

    outParcel->writeInt32(configs->getActiveConfig());
    return NO_ERROR;
}

static status_t getConfigCount(hwc_context_t* ctx, const Parcel *inParcel,
        Parcel *outParcel) {
    Locker::Autolock _sl(ctx->mDrawLock);
    int dpy = inParcel->readInt32();
    //Currently only primary supported
    if(dpy > HWC_DISPLAY_PRIMARY) {
        return BAD_VALUE;
    }

    Configs *configs = Configs::getInstance();
    if(configs == NULL) {
        ALOGE("%s(): Unable to acquire a Configs instance", __FUNCTION__);
        return INVALID_OPERATION;
    }

    outParcel->writeInt32(configs->getConfigCount());
    return NO_ERROR;
}

static status_t getDisplayAttributesForConfig(hwc_context_t* ctx,
        const Parcel *inParcel, Parcel *outParcel) {
    Locker::Autolock _sl(ctx->mDrawLock);
    uint32_t index = inParcel->readInt32();
    int dpy = inParcel->readInt32();
    //Currently only primary supported
    if(dpy > HWC_DISPLAY_PRIMARY) {
        return BAD_VALUE;
    }

    Configs *configs = Configs::getInstance();
    if(configs == NULL) {
        ALOGE("%s(): Unable to acquire a Configs instance", __FUNCTION__);
        return INVALID_OPERATION;
    }

    //xres, yres are used from the Config class, we assume for now that the
    //other params are the same. This might change in the future.
    outParcel->writeInt32(ctx->dpyAttr[dpy].vsync_period);

    qdutils::DisplayAttributes attr = configs->getAttributes(index);
    outParcel->writeInt32(attr.xres);
    outParcel->writeInt32(attr.yres);

    outParcel->writeFloat(ctx->dpyAttr[dpy].xdpi);
    outParcel->writeFloat(ctx->dpyAttr[dpy].ydpi);
    outParcel->writeInt32(ctx->mMDP.panel);

    return NO_ERROR;
}

status_t QClient::notifyCallback(uint32_t command, const Parcel* inParcel,
        Parcel* outParcel) {
    status_t ret = NO_ERROR;

    switch(command) {
        case IQService::SECURING:
            securing(mHwcContext, inParcel->readInt32());
            break;
        case IQService::UNSECURING:
            unsecuring(mHwcContext, inParcel->readInt32());
            break;
        case IQService::SCREEN_REFRESH:
            return screenRefresh(mHwcContext);
            break;
        case IQService::EXTERNAL_ORIENTATION:
            setExtOrientation(mHwcContext, inParcel->readInt32());
            break;
        case IQService::BUFFER_MIRRORMODE:
            setBufferMirrorMode(mHwcContext, inParcel->readInt32());
            break;
        case IQService::GET_DISPLAY_VISIBLE_REGION:
            ret = getDisplayVisibleRegion(mHwcContext, inParcel->readInt32(),
                                    outParcel);
            break;
        case IQService::CHECK_EXTERNAL_STATUS:
            isExternalConnected(mHwcContext, outParcel);
            break;
        case IQService::GET_DISPLAY_ATTRIBUTES:
            getDisplayAttributes(mHwcContext, inParcel, outParcel);
            break;
        case IQService::SET_HSIC_DATA:
            setHSIC(inParcel);
            break;
        case IQService::SET_SECONDARY_DISPLAY_STATUS:
            setSecondaryDisplayStatus(mHwcContext, inParcel);
            break;
        case IQService::SET_VIEW_FRAME:
            setViewFrame(mHwcContext, inParcel);
            break;
        case IQService::DYNAMIC_DEBUG:
            toggleDynamicDebug(mHwcContext, inParcel);
            break;
        case IQService::SET_IDLE_TIMEOUT:
            setIdleTimeout(mHwcContext, inParcel);
            break;
        case IQService::SET_MAX_PIPES_PER_MIXER:
            setMaxPipesPerMixer(mHwcContext, inParcel);
            break;
        case IQService::SET_PARTIAL_UPDATE:
            ret = setPartialUpdatePref(mHwcContext, inParcel->readInt32());
            break;
        case IQService::TOGGLE_BWC:
            toggleBWC(mHwcContext, inParcel);
            break;
        case IQService::CONFIGURE_DYN_REFRESH_RATE:
            configureDynRefreshRate(mHwcContext, inParcel);
            break;
        case IQService::TOGGLE_SCREEN_UPDATE:
            toggleScreenUpdate(mHwcContext, inParcel->readInt32());
            break;
        case IQService::SET_S3D_MODE:
            setS3DMode(mHwcContext, inParcel->readInt32());
            break;
        case IQService::SET_ACTIVE_CONFIG:
            ret = setActiveConfig(mHwcContext, inParcel, outParcel);
            break;
        case IQService::GET_ACTIVE_CONFIG:
            ret = getActiveConfig(mHwcContext, inParcel, outParcel);
            break;
        case IQService::GET_CONFIG_COUNT:
            ret = getConfigCount(mHwcContext, inParcel, outParcel);
            break;
        case IQService::GET_DISPLAY_ATTRIBUTES_FOR_CONFIG:
            ret = getDisplayAttributesForConfig(mHwcContext, inParcel,
                    outParcel);
            break;
        default:
            ret = NO_ERROR;
    }
    return ret;
}

}
