/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010-2012 Code Aurora Forum. All rights reserved.
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

#include <sys/mman.h>

#include <cutils/log.h>
#include <cutils/properties.h>
#include <dlfcn.h>

#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <cutils/atomic.h>

#include <linux/fb.h>
#include <linux/msm_mdp.h>

#include <GLES/gl.h>

#include "gralloc_priv.h"
#include "fb_priv.h"
#include "gr.h"
#include <genlock.h>
#include <cutils/properties.h>
#include <profiler.h>

#include "overlay.h"
namespace ovutils = overlay::utils;

#define EVEN_OUT(x) if (x & 0x0001) {x--;}
/** min of int a, b */
static inline int min(int a, int b) {
    return (a<b) ? a : b;
}
/** max of int a, b */
static inline int max(int a, int b) {
    return (a>b) ? a : b;
}

enum {
    PAGE_FLIP = 0x00000001,
    LOCKED    = 0x00000002
};

struct fb_context_t {
    framebuffer_device_t  device;
};


static int fb_setSwapInterval(struct framebuffer_device_t* dev,
                              int interval)
{
    //XXX: Get the value here and implement along with
    //single vsync in HWC
    char pval[PROPERTY_VALUE_MAX];
    property_get("debug.egl.swapinterval", pval, "-1");
    int property_interval = atoi(pval);
    if (property_interval >= 0)
        interval = property_interval;

    fb_context_t* ctx = (fb_context_t*)dev;
    private_module_t* m = reinterpret_cast<private_module_t*>(
        dev->common.module);
    if (interval < dev->minSwapInterval || interval > dev->maxSwapInterval)
        return -EINVAL;

    m->swapInterval = interval;
    return 0;
}

static int fb_setUpdateRect(struct framebuffer_device_t* dev,
                            int l, int t, int w, int h)
{
    if (((w|h) <= 0) || ((l|t)<0))
        return -EINVAL;
    fb_context_t* ctx = (fb_context_t*)dev;
    private_module_t* m = reinterpret_cast<private_module_t*>(
        dev->common.module);
    m->info.reserved[0] = 0x54445055; // "UPDT";
    m->info.reserved[1] = (uint16_t)l | ((uint32_t)t << 16);
    m->info.reserved[2] = (uint16_t)(l+w) | ((uint32_t)(t+h) << 16);
    return 0;
}

#if defined(HDMI_DUAL_DISPLAY)
static int closeHDMIChannel(private_module_t* m)
{
    // XXX - when enabling HDMI
#if 0
    Overlay* pTemp = m->pobjOverlay;
    if(pTemp != NULL)
        pTemp->closeChannel();
#endif
    return 0;
}

// XXX - Complete when enabling HDMI
#if 0
static void getSecondaryDisplayDestinationInfo(private_module_t* m, overlay_rect&
                                               rect, int& orientation)
{
    Overlay* pTemp = m->pobjOverlay;
    int width = pTemp->getFBWidth();
    int height = pTemp->getFBHeight();
    int fbwidth = m->info.xres, fbheight = m->info.yres;
    rect.x = 0; rect.y = 0;
    rect.w = width; rect.h = height;
    int rot = m->orientation;
    switch(rot) {
        // ROT_0
        case 0:
            // ROT_180
        case HAL_TRANSFORM_ROT_180:
            pTemp->getAspectRatioPosition(fbwidth, fbheight,
                                          &rect);
            if(rot ==  HAL_TRANSFORM_ROT_180)
                orientation = HAL_TRANSFORM_ROT_180;
            else
                orientation  = 0;
            break;
            // ROT_90
        case HAL_TRANSFORM_ROT_90:
            // ROT_270
        case HAL_TRANSFORM_ROT_270:
            //Calculate the Aspectratio for the UI
            //in the landscape mode
            //Width and height will be swapped as there
            //is rotation
            pTemp->getAspectRatioPosition(fbheight, fbwidth,
                                          &rect);

            if(rot == HAL_TRANSFORM_ROT_90)
                orientation = HAL_TRANSFORM_ROT_270;
            else if(rot == HAL_TRANSFORM_ROT_270)
                orientation = HAL_TRANSFORM_ROT_90;
            break;
    }
    return;
}
#endif

/* Determine overlay state based on whether hardware supports true UI
   mirroring and whether video is playing or not */
static ovutils::eOverlayState getOverlayState(struct private_module_t* module)
{
    overlay2::Overlay& ov = *(Overlay::getInstance());

    // Default to existing state
    ovutils::eOverlayState state = ov.getState();

    // Sanity check
    if (!module) {
        ALOGE("%s: NULL module", __FUNCTION__);
        return state;
    }

    // Check if video is playing or not
    if (module->videoOverlay) {
        // Video is playing, check if hardware supports true UI mirroring
        if (module->trueMirrorSupport) {
            // True UI mirroring is supported by hardware
            if (ov.getState() == ovutils::OV_2D_VIDEO_ON_PANEL) {
                // Currently playing 2D video
                state = ovutils::OV_2D_TRUE_UI_MIRROR;
            } else if (ov.getState() == ovutils::OV_3D_VIDEO_ON_2D_PANEL) {
                // Currently playing M3D video
                // FIXME: Support M3D true UI mirroring
                state = ovutils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV;
            }
        } else {
            // True UI mirroring is not supported by hardware
            if (ov.getState() == ovutils::OV_2D_VIDEO_ON_PANEL) {
                // Currently playing 2D video
                state = ovutils::OV_2D_VIDEO_ON_PANEL_TV;
            } else if (ov.getState() == ovutils::OV_3D_VIDEO_ON_2D_PANEL) {
                // Currently playing M3D video
                state = ovutils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV;
            }
        }
    } else {
        // Video is not playing, true UI mirroring support is irrelevant
        state = ovutils::OV_UI_MIRROR;
    }

    return state;
}

/* Set overlay state */
static void setOverlayState(ovutils::eOverlayState state)
{
    overlay2::Overlay& ov = *(Overlay::getInstance());
    ov.setState(state);
}

static void *hdmi_ui_loop(void *ptr)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(ptr);
    while (1) {
        pthread_mutex_lock(&m->overlayLock);
        while(!(m->hdmiStateChanged))
            pthread_cond_wait(&(m->overlayPost), &(m->overlayLock));

        m->hdmiStateChanged = false;
        if (m->exitHDMIUILoop) {
            pthread_mutex_unlock(&m->overlayLock);
            return NULL;
        }

        // No need to mirror UI if HDMI is not on
        if (!m->enableHDMIOutput) {
            ALOGE_IF(FB_DEBUG, "%s: hdmi not ON", __FUNCTION__);
            pthread_mutex_unlock(&m->overlayLock);
            continue;
        }

        overlay2::OverlayMgr* ovMgr =
            overlay2::OverlayMgrSingleton::getOverlayMgr();
        overlay2::Overlay& ov = ovMgr->ov();

        // Set overlay state
        ovutils::eOverlayState state = getOverlayState(m);
        setOverlayState(state);

        // Determine the RGB pipe for UI depending on the state
        ovutils::eDest dest = ovutils::OV_PIPE_ALL;
        if (state == ovutils::OV_2D_TRUE_UI_MIRROR) {
            // True UI mirroring state: external RGB pipe is OV_PIPE2
            dest = ovutils::OV_PIPE2;
        } else if (state == ovutils::OV_UI_MIRROR) {
            // UI-only mirroring state: external RGB pipe is OV_PIPE0
            dest = ovutils::OV_PIPE0;
        } else {
            // No UI in this case
            pthread_mutex_unlock(&m->overlayLock);
            continue;
        }

        if (m->hdmiMirroringState == HDMI_UI_MIRRORING) {
            int alignedW = ALIGN(m->info.xres, 32);

            private_handle_t const* hnd =
                reinterpret_cast<private_handle_t const*>(m->framebuffer);
            unsigned int width = alignedW;
            unsigned int height = hnd->height;
            unsigned int format = hnd->format;
            unsigned int size = hnd->size/m->numBuffers;

            ovutils::eMdpFlags mdpFlags = ovutils::OV_MDP_FLAGS_NONE;
            // External display connected during secure video playback
            // Open secure UI session
            // NOTE: when external display is already connected and then secure
            // playback is started, we dont have to do anything
            if (m->secureVideoOverlay) {
                ovutils::setMdpFlags(mdpFlags, ovutils::OV_MDP_SECURE_OVERLAY_SESSION);
            }

            ovutils::Whf whf(width, height, format, size);
            ovutils::PipeArgs parg(mdpFlags,
                                   ovutils::OVERLAY_TRANSFORM_0,
                                   whf,
                                   ovutils::WAIT,
                                   ovutils::ZORDER_0,
                                   ovutils::IS_FG_OFF,
                                   ovutils::ROT_FLAG_ENABLED);
            ovutils::PipeArgs pargs[ovutils::MAX_PIPES] = { parg, parg, parg };
            bool ret = ov.setSource(pargs, dest);
            if (!ret) {
                ALOGE("%s setSource failed", __FUNCTION__);
            }

            // we need to communicate m->orientation that will get some
            // modifications within setParameter func.
            // FIXME that is ugly.
            const ovutils::Params prms (ovutils::OVERLAY_TRANSFORM_UI,
                                        m->orientation);
            ov.setParameter(prms, dest);
            if (!ret) {
                ALOGE("%s setParameter failed transform", __FUNCTION__);
            }

            // x,y,w,h
            ovutils::Dim dcrop(0, 0, m->info.xres, m->info.yres);
            ov.setMemoryId(m->framebuffer->fd, dest);
            ret = ov.setCrop(dcrop, dest);
            if (!ret) {
                ALOGE("%s setCrop failed", __FUNCTION__);
            }

            ovutils::Dim pdim (m->info.xres,
                               m->info.yres,
                               0,
                               0,
                               m->orientation);
            ret = ov.setPosition(pdim, dest);
            if (!ret) {
                ALOGE("%s setPosition failed", __FUNCTION__);
            }

            if (!ov.commit(dest)) {
                ALOGE("%s commit fails", __FUNCTION__);
            }

            ret = ov.queueBuffer(m->currentOffset, dest);
            if (!ret) {
                ALOGE("%s queueBuffer failed", __FUNCTION__);
            }
        } else {
            setOverlayState(ovutils::OV_CLOSED);
        }
        pthread_mutex_unlock(&m->overlayLock);
    }
    return NULL;
}

static int fb_videoOverlayStarted(struct framebuffer_device_t* dev, int started)
{
    ALOGE_IF(FB_DEBUG, "%s started=%d", __FUNCTION__, started);
    private_module_t* m = reinterpret_cast<private_module_t*>(
        dev->common.module);
    pthread_mutex_lock(&m->overlayLock);
    if(started != m->videoOverlay) {
        m->videoOverlay = started;
        m->hdmiStateChanged = true;
        if (!m->trueMirrorSupport) {
            if (started) {
                m->hdmiMirroringState = HDMI_NO_MIRRORING;
                ovutils::eOverlayState state = getOverlayState(m);
                setOverlayState(state);
            } else if (m->enableHDMIOutput)
                m->hdmiMirroringState = HDMI_UI_MIRRORING;
        } else {
            if (m->videoOverlay == VIDEO_3D_OVERLAY_STARTED) {
                ALOGE_IF(FB_DEBUG, "3D Video Started, stop mirroring!");
                m->hdmiMirroringState = HDMI_NO_MIRRORING;
                ovutils::eOverlayState state = getOverlayState(m);
                setOverlayState(state);
            }
            else if (m->enableHDMIOutput) {
                m->hdmiMirroringState = HDMI_UI_MIRRORING;
            }
        }
    }
    pthread_mutex_unlock(&m->overlayLock);
    return 0;
}

static int fb_enableHDMIOutput(struct framebuffer_device_t* dev, int externaltype)
{
    ALOGE_IF(FB_DEBUG, "%s externaltype=%d", __FUNCTION__, externaltype);
    private_module_t* m = reinterpret_cast<private_module_t*>(
        dev->common.module);
    pthread_mutex_lock(&m->overlayLock);
    //Check if true mirroring can be supported
    m->trueMirrorSupport = ovutils::FrameBufferInfo::getInstance()->supportTrueMirroring();
    m->enableHDMIOutput = externaltype;
    if(externaltype) {
        if (m->trueMirrorSupport) {
            m->hdmiMirroringState = HDMI_UI_MIRRORING;
        } else {
            if(!m->videoOverlay)
                m->hdmiMirroringState = HDMI_UI_MIRRORING;
        }
    } else if (!externaltype) {
        // Either HDMI is disconnected or suspend occurred
        m->hdmiMirroringState = HDMI_NO_MIRRORING;
        ovutils::eOverlayState state = getOverlayState(m);
        setOverlayState(state);
    }
    m->hdmiStateChanged = true;
    pthread_cond_signal(&(m->overlayPost));
    pthread_mutex_unlock(&m->overlayLock);
    return 0;
}

static int fb_orientationChanged(struct framebuffer_device_t* dev, int orientation)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(
        dev->common.module);
    pthread_mutex_lock(&m->overlayLock);
    neworientation = orientation;
    pthread_mutex_unlock(&m->overlayLock);
    return 0;
}

static int handle_open_secure_start(private_module_t* m) {
    pthread_mutex_lock(&m->overlayLock);
    m->hdmiMirroringState = HDMI_NO_MIRRORING;
    m->secureVideoOverlay = true;
    pthread_mutex_unlock(&m->overlayLock);
    return 0;
}

static int handle_open_secure_end(private_module_t* m) {
    pthread_mutex_lock(&m->overlayLock);
    if (m->enableHDMIOutput) {
        if (m->trueMirrorSupport) {
            m->hdmiMirroringState = HDMI_UI_MIRRORING;
        } else if(!m->videoOverlay) {
            m->hdmiMirroringState = HDMI_UI_MIRRORING;
        }
        m->hdmiStateChanged = true;
        pthread_cond_signal(&(m->overlayPost));
    }
    pthread_mutex_unlock(&m->overlayLock);
    return 0;
}

static int handle_close_secure_start(private_module_t* m) {
    pthread_mutex_lock(&m->overlayLock);
    m->hdmiMirroringState = HDMI_NO_MIRRORING;
    m->secureVideoOverlay = false;
    pthread_mutex_unlock(&m->overlayLock);
    return 0;
}

static int handle_close_secure_end(private_module_t* m) {
    pthread_mutex_lock(&m->overlayLock);
    if (m->enableHDMIOutput) {
        if (m->trueMirrorSupport) {
            m->hdmiMirroringState = HDMI_UI_MIRRORING;
        } else if(!m->videoOverlay) {
            m->hdmiMirroringState = HDMI_UI_MIRRORING;
        }
        m->hdmiStateChanged = true;
        pthread_cond_signal(&(m->overlayPost));
    }
    pthread_mutex_unlock(&m->overlayLock);
    return 0;
}
#endif



/* fb_perform - used to add custom event and handle them in fb HAL
 * Used for external display related functions as of now
 */
static int fb_perform(struct framebuffer_device_t* dev, int event, int value)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(
        dev->common.module);
    switch(event) {
#if defined(HDMI_DUAL_DISPLAY)
        case EVENT_EXTERNAL_DISPLAY:
            fb_enableHDMIOutput(dev, value);
            break;
        case EVENT_VIDEO_OVERLAY:
            fb_videoOverlayStarted(dev, value);
            break;
        case EVENT_ORIENTATION_CHANGE:
            fb_orientationChanged(dev, value);
            break;
        case EVENT_OVERLAY_STATE_CHANGE:
            if (value == OVERLAY_STATE_CHANGE_START) {
                // When state change starts, get a lock on overlay
                pthread_mutex_lock(&m->overlayLock);
            } else if (value == OVERLAY_STATE_CHANGE_END) {
                // When state change is complete, unlock overlay
                pthread_mutex_unlock(&m->overlayLock);
            }
            break;
        case EVENT_OPEN_SECURE_START:
            handle_open_secure_start(m);
            break;
        case EVENT_OPEN_SECURE_END:
            handle_open_secure_end(m);
            break;
        case EVENT_CLOSE_SECURE_START:
            handle_close_secure_start(m);
            break;
        case EVENT_CLOSE_SECURE_END:
            handle_close_secure_end(m);
            break;
#endif
        default:
            ALOGE("In %s: UNKNOWN Event = %d!!!", __FUNCTION__, event);
            break;
    }
    return 0;
}


static int fb_post(struct framebuffer_device_t* dev, buffer_handle_t buffer)
{
    if (private_handle_t::validate(buffer) < 0)
        return -EINVAL;

    fb_context_t* ctx = (fb_context_t*) dev;

    private_handle_t *hnd = static_cast<private_handle_t*>
                            (const_cast<native_handle_t*>(buffer));

    private_module_t* m =
        reinterpret_cast<private_module_t*>(dev->common.module);

    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
        genlock_lock_buffer(hnd, GENLOCK_READ_LOCK, GENLOCK_MAX_TIMEOUT);

        if (m->currentBuffer) {
            genlock_unlock_buffer(m->currentBuffer);
            m->currentBuffer = 0;
        }

        const size_t offset = hnd->base - m->framebuffer->base;
        m->info.activate = FB_ACTIVATE_VBL;
        m->info.yoffset = offset / m->finfo.line_length;
        if (ioctl(m->framebuffer->fd, FBIOPUT_VSCREENINFO, &m->info) == -1) {
            ALOGE("FBIOPUT_VSCREENINFO failed");
            genlock_unlock_buffer(hnd);
            return -errno;
        }
        CALC_FPS();
        m->currentBuffer = hnd;
    }
    return 0;
}

static int fb_compositionComplete(struct framebuffer_device_t* dev)
{
    // TODO: Properly implement composition complete callback
    glFinish();

    return 0;
}

int mapFrameBufferLocked(struct private_module_t* module)
{
    // already initialized...
    if (module->framebuffer) {
        return 0;
    }
    char const * const device_template[] = {
        "/dev/graphics/fb%u",
        "/dev/fb%u",
        0 };

    int fd = -1;
    int i=0;
    char name[64];
    char property[PROPERTY_VALUE_MAX];

    while ((fd==-1) && device_template[i]) {
        snprintf(name, 64, device_template[i], 0);
        fd = open(name, O_RDWR, 0);
        i++;
    }
    if (fd < 0)
        return -errno;

    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1)
        return -errno;

    struct fb_var_screeninfo info;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1)
        return -errno;

    info.reserved[0] = 0;
    info.reserved[1] = 0;
    info.reserved[2] = 0;
    info.xoffset = 0;
    info.yoffset = 0;
    info.activate = FB_ACTIVATE_NOW;

    /* Interpretation of offset for color fields: All offsets are from the right,
     * inside a "pixel" value, which is exactly 'bits_per_pixel' wide (means: you
     * can use the offset as right argument to <<). A pixel afterwards is a bit
     * stream and is written to video memory as that unmodified. This implies
     * big-endian byte order if bits_per_pixel is greater than 8.
     */

    if(info.bits_per_pixel == 32) {
        /*
         * Explicitly request RGBA_8888
         */
        info.bits_per_pixel = 32;
        info.red.offset     = 24;
        info.red.length     = 8;
        info.green.offset   = 16;
        info.green.length   = 8;
        info.blue.offset    = 8;
        info.blue.length    = 8;
        info.transp.offset  = 0;
        info.transp.length  = 8;

        /* Note: the GL driver does not have a r=8 g=8 b=8 a=0 config, so if we do
         * not use the MDP for composition (i.e. hw composition == 0), ask for
         * RGBA instead of RGBX. */
        if (property_get("debug.sf.hw", property, NULL) > 0 && atoi(property) == 0)
            module->fbFormat = HAL_PIXEL_FORMAT_RGBX_8888;
        else if(property_get("debug.composition.type", property, NULL) > 0 &&
                (strncmp(property, "mdp", 3) == 0))
            module->fbFormat = HAL_PIXEL_FORMAT_RGBX_8888;
        else
            module->fbFormat = HAL_PIXEL_FORMAT_RGBA_8888;
    } else {
        /*
         * Explicitly request 5/6/5
         */
        info.bits_per_pixel = 16;
        info.red.offset     = 11;
        info.red.length     = 5;
        info.green.offset   = 5;
        info.green.length   = 6;
        info.blue.offset    = 0;
        info.blue.length    = 5;
        info.transp.offset  = 0;
        info.transp.length  = 0;
        module->fbFormat = HAL_PIXEL_FORMAT_RGB_565;
    }

    //adreno needs 4k aligned offsets. Max hole size is 4096-1
    int  size = roundUpToPageSize(info.yres * info.xres * (info.bits_per_pixel/8));

    /*
     * Request NUM_BUFFERS screens (at least 2 for page flipping)
     */
    int numberOfBuffers = (int)(finfo.smem_len/size);
    ALOGV("num supported framebuffers in kernel = %d", numberOfBuffers);

    if (property_get("debug.gr.numframebuffers", property, NULL) > 0) {
        int num = atoi(property);
        if ((num >= NUM_FRAMEBUFFERS_MIN) && (num <= NUM_FRAMEBUFFERS_MAX)) {
            numberOfBuffers = num;
        }
    }
    if (numberOfBuffers > NUM_FRAMEBUFFERS_MAX)
        numberOfBuffers = NUM_FRAMEBUFFERS_MAX;

    ALOGV("We support %d buffers", numberOfBuffers);

    //consider the included hole by 4k alignment
    uint32_t line_length = (info.xres * info.bits_per_pixel / 8);
    info.yres_virtual = (size * numberOfBuffers) / line_length;

    uint32_t flags = PAGE_FLIP;
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &info) == -1) {
        info.yres_virtual = size / line_length;
        flags &= ~PAGE_FLIP;
        ALOGW("FBIOPUT_VSCREENINFO failed, page flipping not supported");
    }

    if (info.yres_virtual < ((size * 2) / line_length) ) {
        // we need at least 2 for page-flipping
        info.yres_virtual = size / line_length;
        flags &= ~PAGE_FLIP;
        ALOGW("page flipping not supported (yres_virtual=%d, requested=%d)",
              info.yres_virtual, info.yres*2);
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1)
        return -errno;

    if (int(info.width) <= 0 || int(info.height) <= 0) {
        // the driver doesn't return that information
        // default to 160 dpi
        info.width  = ((info.xres * 25.4f)/160.0f + 0.5f);
        info.height = ((info.yres * 25.4f)/160.0f + 0.5f);
    }

    float xdpi = (info.xres * 25.4f) / info.width;
    float ydpi = (info.yres * 25.4f) / info.height;
    //The reserved[3] field is used to store FPS by the driver.
    float fps  = info.reserved[3];

    ALOGI("using (fd=%d)\n"
          "id           = %s\n"
          "xres         = %d px\n"
          "yres         = %d px\n"
          "xres_virtual = %d px\n"
          "yres_virtual = %d px\n"
          "bpp          = %d\n"
          "r            = %2u:%u\n"
          "g            = %2u:%u\n"
          "b            = %2u:%u\n",
          fd,
          finfo.id,
          info.xres,
          info.yres,
          info.xres_virtual,
          info.yres_virtual,
          info.bits_per_pixel,
          info.red.offset, info.red.length,
          info.green.offset, info.green.length,
          info.blue.offset, info.blue.length
         );

    ALOGI("width        = %d mm (%f dpi)\n"
          "height       = %d mm (%f dpi)\n"
          "refresh rate = %.2f Hz\n",
          info.width,  xdpi,
          info.height, ydpi,
          fps
         );


    if (ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == -1)
        return -errno;

    if (finfo.smem_len <= 0)
        return -errno;

    module->flags = flags;
    module->info = info;
    module->finfo = finfo;
    module->xdpi = xdpi;
    module->ydpi = ydpi;
    module->fps = fps;
    module->swapInterval = 1;

    CALC_INIT();

    /*
     * map the framebuffer
     */

    int err;
    module->numBuffers = info.yres_virtual / info.yres;
    module->bufferMask = 0;
    //adreno needs page aligned offsets. Align the fbsize to pagesize.
    size_t fbSize = roundUpToPageSize(finfo.line_length * info.yres)*
                    module->numBuffers;
    module->framebuffer = new private_handle_t(fd, fbSize,
                                               private_handle_t::PRIV_FLAGS_USES_PMEM,
                                               BUFFER_TYPE_UI,
                                               module->fbFormat, info.xres, info.yres);
    void* vaddr = mmap(0, fbSize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (vaddr == MAP_FAILED) {
        ALOGE("Error mapping the framebuffer (%s)", strerror(errno));
        return -errno;
    }
    module->framebuffer->base = intptr_t(vaddr);
    memset(vaddr, 0, fbSize);

#if defined(HDMI_DUAL_DISPLAY)
    /* Overlay for HDMI*/
    pthread_mutex_init(&(module->overlayLock), NULL);
    pthread_cond_init(&(module->overlayPost), NULL);
    module->currentOffset = 0;
    module->exitHDMIUILoop = false;
    module->hdmiStateChanged = false;
    pthread_t hdmiUIThread;
    pthread_create(&hdmiUIThread, NULL, &hdmi_ui_loop, (void *) module);
    module->hdmiMirroringState = HDMI_NO_MIRRORING;
    module->trueMirrorSupport = false;
#endif

    return 0;
}

static int mapFrameBuffer(struct private_module_t* module)
{
    pthread_mutex_lock(&module->lock);
    int err = mapFrameBufferLocked(module);
    pthread_mutex_unlock(&module->lock);
    return err;
}

/*****************************************************************************/

static int fb_close(struct hw_device_t *dev)
{
    fb_context_t* ctx = (fb_context_t*)dev;
#if defined(HDMI_DUAL_DISPLAY)
    private_module_t* m = reinterpret_cast<private_module_t*>(
        ctx->device.common.module);
    pthread_mutex_lock(&m->overlayLock);
    m->exitHDMIUILoop = true;
    pthread_cond_signal(&(m->overlayPost));
    pthread_mutex_unlock(&m->overlayLock);
#endif
    if (ctx) {
        free(ctx);
    }
    return 0;
}

int fb_device_open(hw_module_t const* module, const char* name,
                   hw_device_t** device)
{
    int status = -EINVAL;
    if (!strcmp(name, GRALLOC_HARDWARE_FB0)) {
        alloc_device_t* gralloc_device;
        status = gralloc_open(module, &gralloc_device);
        if (status < 0)
            return status;

        /* initialize our state here */
        fb_context_t *dev = (fb_context_t*)malloc(sizeof(*dev));
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag      = HARDWARE_DEVICE_TAG;
        dev->device.common.version  = 0;
        dev->device.common.module   = const_cast<hw_module_t*>(module);
        dev->device.common.close    = fb_close;
        dev->device.setSwapInterval = fb_setSwapInterval;
        dev->device.post            = fb_post;
        dev->device.setUpdateRect   = 0;
        dev->device.compositionComplete = fb_compositionComplete;

        private_module_t* m = (private_module_t*)module;
        status = mapFrameBuffer(m);
        if (status >= 0) {
            int stride = m->finfo.line_length / (m->info.bits_per_pixel >> 3);
            const_cast<uint32_t&>(dev->device.flags) = 0;
            const_cast<uint32_t&>(dev->device.width) = m->info.xres;
            const_cast<uint32_t&>(dev->device.height) = m->info.yres;
            const_cast<int&>(dev->device.stride) = stride;
            const_cast<int&>(dev->device.format) = m->fbFormat;
            const_cast<float&>(dev->device.xdpi) = m->xdpi;
            const_cast<float&>(dev->device.ydpi) = m->ydpi;
            const_cast<float&>(dev->device.fps) = m->fps;
            const_cast<int&>(dev->device.minSwapInterval) = PRIV_MIN_SWAP_INTERVAL;
            const_cast<int&>(dev->device.maxSwapInterval) = PRIV_MAX_SWAP_INTERVAL;
            const_cast<int&>(dev->device.numFramebuffers) = m->numBuffers;
            if (m->finfo.reserved[0] == 0x5444 &&
                m->finfo.reserved[1] == 0x5055) {
                dev->device.setUpdateRect = fb_setUpdateRect;
                ALOGD("UPDATE_ON_DEMAND supported");
            }

            *device = &dev->device.common;
        }

        // Close the gralloc module
        gralloc_close(gralloc_device);
    }
    return status;
}
