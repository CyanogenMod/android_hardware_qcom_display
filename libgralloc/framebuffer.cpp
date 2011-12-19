/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010-2011 Code Aurora Forum. All rights reserved.
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

#include <dlfcn.h>

#include <cutils/ashmem.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <utils/Timers.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <utils/Timers.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#include <linux/fb.h>
#include <linux/msm_mdp.h>

#include <GLES/gl.h>

#include "gralloc_priv.h"
#include "gr.h"
#ifdef NO_SURFACEFLINGER_SWAPINTERVAL
#include <cutils/properties.h>
#endif

#define FB_DEBUG 0

#if defined(HDMI_DUAL_DISPLAY)
#define AS_1080_RATIO_H (4.25/100)  // Default Action Safe vertical limit for 1080p
#define AS_1080_RATIO_W (4.25/100)  // Default Action Safe horizontal limit for 1080p
#define AS_720_RATIO_H (6.0/100)  // Default Action Safe vertical limit for 720p
#define AS_720_RATIO_W (4.25/100)  // Default Action Safe horizontal limit for 720p
#define AS_480_RATIO_H (8.0/100)  // Default Action Safe vertical limit for 480p
#define AS_480_RATIO_W (5.0/100)  // Default Action Safe horizontal limit for 480p
#define HEIGHT_1080P 1080
#define HEIGHT_720P 720
#define HEIGHT_480P 480
#define EVEN_OUT(x) if (x & 0x0001) {x--;}
using overlay::Overlay;
/** min of int a, b */
static inline int min(int a, int b) {
    return (a<b) ? a : b;
}
/** max of int a, b */
static inline int max(int a, int b) {
    return (a>b) ? a : b;
}
/** align */
static inline size_t ALIGN(size_t x, size_t align) {
    return (x + align-1) & ~(align-1);
}
#endif

char framebufferStateName[] = {'S', 'R', 'A'};

#ifdef DEBUG_CALC_FPS

#define MAX_FPS_CALC_PERIOD_IN_FRAMES 128
#define MAX_FRAMARRIVAL_STEPS          50
#define MAX_DEBUG_FPS_LEVEL             2

struct debug_fps_metadata_t {
    /*fps calculation based on time or number of frames*/
    enum DfmType {
      DFM_FRAMES = 0,
      DFM_TIME   = 1,
    };

    DfmType type;

    /* indicates how much time do we wait till we calculate FPS */
    unsigned long time_period;

    /*indicates how much time elapsed since we report fps*/
    float time_elapsed;

    /* indicates how many frames do we wait till we calculate FPS */
    unsigned int period;
    /* current frame, will go upto period, and then reset */
    unsigned int curr_frame;
    /* frame will arrive at a multiple of 16666 us at the display.
       This indicates how many steps to consider for our calculations.
       For example, if framearrival_steps = 10, then the frame that arrived
       after 166660 us or more will be ignored.
    */
    unsigned int framearrival_steps;
    /* ignorethresh_us = framearrival_steps * 16666 */
    nsecs_t      ignorethresh_us;
    /* used to calculate the actual frame arrival step, the times might not be
       accurate
    */
    unsigned int margin_us;
    /* actual data storage */
    nsecs_t      framearrivals[MAX_FPS_CALC_PERIOD_IN_FRAMES];
    nsecs_t      accum_framearrivals[MAX_FRAMARRIVAL_STEPS];
};

#endif

/*****************************************************************************/

enum {
    MDDI_PANEL = '1',
    EBI2_PANEL = '2',
    LCDC_PANEL = '3',
    EXT_MDDI_PANEL = '4',
    TV_PANEL = '5'
};

enum {
    PAGE_FLIP = 0x00000001,
    LOCKED = 0x00000002
};

struct fb_context_t {
    framebuffer_device_t  device;
};

static int neworientation;

#ifdef DEBUG_CALC_FPS
static debug_fps_metadata_t debug_fps_metadata;
static unsigned int debug_fps_level = 0;
#endif

/*****************************************************************************/

static void
msm_copy_buffer(buffer_handle_t handle, int fd,
                int width, int height, int format,
                int x, int y, int w, int h);

static int fb_setSwapInterval(struct framebuffer_device_t* dev,
            int interval)
{
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

#ifdef DEBUG_CALC_FPS

static void populate_debug_fps_metadata(void)
{
    char prop[PROPERTY_VALUE_MAX];

    /*defaults calculation of fps to based on number of frames*/
    property_get("debug.gr.calcfps.type", prop, "0");
    debug_fps_metadata.type = (debug_fps_metadata_t::DfmType) atoi(prop);

    /*defaults to 1000ms*/
    property_get("debug.gr.calcfps.timeperiod", prop, "1000");
    debug_fps_metadata.time_period = atoi(prop);

    property_get("debug.gr.calcfps.period", prop, "10");
    debug_fps_metadata.period = atoi(prop);

    if (debug_fps_metadata.period > MAX_FPS_CALC_PERIOD_IN_FRAMES) {
        debug_fps_metadata.period = MAX_FPS_CALC_PERIOD_IN_FRAMES;
    }

    /* default ignorethresh_us: 500 milli seconds */
    property_get("debug.gr.calcfps.ignorethresh_us", prop, "500000");
    debug_fps_metadata.ignorethresh_us = atoi(prop);

    debug_fps_metadata.framearrival_steps =
                       (debug_fps_metadata.ignorethresh_us / 16666);

    if (debug_fps_metadata.framearrival_steps > MAX_FRAMARRIVAL_STEPS) {
        debug_fps_metadata.framearrival_steps = MAX_FRAMARRIVAL_STEPS;
        debug_fps_metadata.ignorethresh_us =
                        debug_fps_metadata.framearrival_steps * 16666;
    }

    /* 2ms margin of error for the gettimeofday */
    debug_fps_metadata.margin_us = 2000;

    for (int i = 0; i < MAX_FRAMARRIVAL_STEPS; i++)
        debug_fps_metadata.accum_framearrivals[i] = 0;

    LOGE("period: %d", debug_fps_metadata.period);
    LOGE("ignorethresh_us: %lld", debug_fps_metadata.ignorethresh_us);
}

static void print_fps(float fps)
{
    if (debug_fps_metadata_t::DFM_FRAMES == debug_fps_metadata.type)
        LOGE("FPS for last %d frames: %3.2f", debug_fps_metadata.period, fps);
    else
        LOGE("FPS for last (%f ms, %d frames): %3.2f",
             debug_fps_metadata.time_elapsed,
             debug_fps_metadata.curr_frame, fps);

    debug_fps_metadata.curr_frame = 0;
    debug_fps_metadata.time_elapsed = 0.0;

    if (debug_fps_level > 1) {
        LOGE("Frame Arrival Distribution:");
        for (unsigned int i = 0;
             i < ((debug_fps_metadata.framearrival_steps / 6) + 1);
             i++) {
            LOGE("%lld %lld %lld %lld %lld %lld",
                 debug_fps_metadata.accum_framearrivals[i*6],
                 debug_fps_metadata.accum_framearrivals[i*6+1],
                 debug_fps_metadata.accum_framearrivals[i*6+2],
                 debug_fps_metadata.accum_framearrivals[i*6+3],
                 debug_fps_metadata.accum_framearrivals[i*6+4],
                 debug_fps_metadata.accum_framearrivals[i*6+5]);
        }

        /* We are done with displaying, now clear the stats */
        for (unsigned int i = 0;
             i < debug_fps_metadata.framearrival_steps;
             i++)
            debug_fps_metadata.accum_framearrivals[i] = 0;
    }
    return;
}

static void calc_fps(nsecs_t currtime_us)
{
    static nsecs_t oldtime_us = 0;

    nsecs_t diff = currtime_us - oldtime_us;

    oldtime_us = currtime_us;

    if (debug_fps_metadata_t::DFM_FRAMES == debug_fps_metadata.type &&
        diff > debug_fps_metadata.ignorethresh_us) {
        return;
    }

    if (debug_fps_metadata.curr_frame < MAX_FPS_CALC_PERIOD_IN_FRAMES) {
        debug_fps_metadata.framearrivals[debug_fps_metadata.curr_frame++] = diff;
    }

    if (debug_fps_level > 1) {
        unsigned int currstep = (diff + debug_fps_metadata.margin_us) / 16666;

        if (currstep < debug_fps_metadata.framearrival_steps) {
            debug_fps_metadata.accum_framearrivals[currstep-1]++;
        }
    }

    if (debug_fps_metadata_t::DFM_FRAMES == debug_fps_metadata.type) {
        if (debug_fps_metadata.curr_frame == debug_fps_metadata.period) {
            /* time to calculate and display FPS */
            nsecs_t sum = 0;
            for (unsigned int i = 0; i < debug_fps_metadata.period; i++)
                sum += debug_fps_metadata.framearrivals[i];
            print_fps((debug_fps_metadata.period * float(1000000))/float(sum));
        }
    }
    else if (debug_fps_metadata_t::DFM_TIME == debug_fps_metadata.type) {
        debug_fps_metadata.time_elapsed += ((float)diff/1000.0);
        if (debug_fps_metadata.time_elapsed >= debug_fps_metadata.time_period) {
            float fps = (1000.0 * debug_fps_metadata.curr_frame)/
                        (float)debug_fps_metadata.time_elapsed;
            print_fps(fps);
        }
    }
    return;
}

#endif // DEBUG_CALC_FPS

static void *disp_loop(void *ptr)
{
    struct qbuf_t nxtBuf;
    static int cur_buf=-1;
    private_module_t *m = reinterpret_cast<private_module_t*>(ptr);

    while (1) {
        pthread_mutex_lock(&(m->qlock));

        // wait (sleep) while display queue is empty;
        while (m->disp.isEmpty()) {
            pthread_cond_wait(&(m->qpost),&(m->qlock));
        }

        // dequeue next buff to display and lock it
        nxtBuf = m->disp.getHeadValue();
        m->disp.pop();
        pthread_mutex_unlock(&(m->qlock));

        // post buf out to display synchronously
        private_handle_t const* hnd = reinterpret_cast<private_handle_t const*>
                                                (nxtBuf.buf);
        const size_t offset = hnd->base - m->framebuffer->base;
        m->info.activate = FB_ACTIVATE_VBL;
        m->info.yoffset = offset / m->finfo.line_length;

#if defined(HDMI_DUAL_DISPLAY)
        pthread_mutex_lock(&m->overlayLock);
        m->orientation = neworientation;
        m->currentOffset = offset;
        m->hdmiStateChanged = true;
        pthread_cond_signal(&(m->overlayPost));
        pthread_mutex_unlock(&m->overlayLock);
#endif
        if (ioctl(m->framebuffer->fd, FBIOPUT_VSCREENINFO, &m->info) == -1) {
            LOGE("ERROR FBIOPUT_VSCREENINFO failed; frame not displayed");
        }

#if defined COMPOSITION_BYPASS
        //Signal so that we can close channels if we need to
        pthread_mutex_lock(&m->bufferPostLock);
        m->bufferPostDone = true;
        pthread_cond_signal(&m->bufferPostCond);
        pthread_mutex_unlock(&m->bufferPostLock);
#endif

#ifdef DEBUG_CALC_FPS
        if (debug_fps_level > 0) calc_fps(ns2us(systemTime()));
#endif

        if (cur_buf == -1) {
            int nxtAvail = ((nxtBuf.idx + 1) % m->numBuffers);
            pthread_mutex_lock(&(m->avail[nxtBuf.idx].lock));
            m->avail[nxtBuf.idx].is_avail = true;
            m->avail[nxtBuf.idx].state = REF;
            pthread_cond_broadcast(&(m->avail[nxtBuf.idx].cond));
            pthread_mutex_unlock(&(m->avail[nxtBuf.idx].lock));
        } else {
            pthread_mutex_lock(&(m->avail[nxtBuf.idx].lock));
            if (m->avail[nxtBuf.idx].state != SUB) {
                LOGE_IF(m->swapInterval != 0, "[%d] state %c, expected %c", nxtBuf.idx,
                    framebufferStateName[m->avail[nxtBuf.idx].state],
                    framebufferStateName[SUB]);
            }
            m->avail[nxtBuf.idx].state = REF;
            pthread_mutex_unlock(&(m->avail[nxtBuf.idx].lock));

            pthread_mutex_lock(&(m->avail[cur_buf].lock));
            m->avail[cur_buf].is_avail = true;
            if (m->avail[cur_buf].state != REF) {
                LOGE_IF(m->swapInterval != 0, "[%d] state %c, expected %c", cur_buf,
                    framebufferStateName[m->avail[cur_buf].state],
                    framebufferStateName[REF]);
            }
            m->avail[cur_buf].state = AVL;
            pthread_cond_broadcast(&(m->avail[cur_buf].cond));
            pthread_mutex_unlock(&(m->avail[cur_buf].lock));
        }
        cur_buf = nxtBuf.idx;
    }
    return NULL;
}

#if defined(HDMI_DUAL_DISPLAY)
static void *hdmi_ui_loop(void *ptr)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(
            ptr);
    while (1) {
        pthread_mutex_lock(&m->overlayLock);
        while(!(m->hdmiStateChanged))
            pthread_cond_wait(&(m->overlayPost), &(m->overlayLock));
        m->hdmiStateChanged = false;
        if (m->exitHDMIUILoop) {
            pthread_mutex_unlock(&m->overlayLock);
            return NULL;
        }
        float asWidthRatio = m->actionsafeWidthRatio/100.0f;
        float asHeightRatio = m->actionsafeHeightRatio/100.0f;

        if (m->pobjOverlay) {
            Overlay* pTemp = m->pobjOverlay;
            if (!m->enableHDMIOutput)
                pTemp->closeChannel();
            else if (m->enableHDMIOutput && !m->videoOverlay) {
                if (!pTemp->isChannelUP()) {
                   int alignedW = ALIGN(m->info.xres, 32);

                   private_handle_t const* hnd =
                      reinterpret_cast<private_handle_t const*>(m->framebuffer);
                   overlay_buffer_info info;
                   info.width = alignedW;
                   info.height = hnd->height;
                   info.format = hnd->format;
                   info.size = hnd->size;

                   if (pTemp->startChannel(info, 1,
                                           false, true, 0, VG0_PIPE, true)) {
                        pTemp->setFd(m->framebuffer->fd);
                        pTemp->setCrop(0, 0, m->info.xres, m->info.yres);
                   } else
                       pTemp->closeChannel();
                }

                if (pTemp->isChannelUP()) {
                    int width = pTemp->getFBWidth();
                    int height = pTemp->getFBHeight();
                    int aswidth = width, asheight = height;
                    int asX = 0, asY = 0; // Action safe x, y co-ordinates
                    int fbwidth = m->info.xres, fbheight = m->info.yres;
                    float defaultASWidthRatio = 0.0f, defaultASHeightRatio = 0.0f;
                    if(HEIGHT_1080P == height) {
                        defaultASHeightRatio = AS_1080_RATIO_H;
                        defaultASWidthRatio = AS_1080_RATIO_W;
                    } else if(HEIGHT_720P == height) {
                        defaultASHeightRatio = AS_720_RATIO_H;
                        defaultASWidthRatio = AS_720_RATIO_W;
                    } else if(HEIGHT_480P == height) {
                        defaultASHeightRatio = AS_480_RATIO_H;
                        defaultASWidthRatio = AS_480_RATIO_W;
                    }
                    if(asWidthRatio <= 0.0f)
                        asWidthRatio = defaultASWidthRatio;
                    if(asHeightRatio <= 0.0f)
                        asHeightRatio = defaultASHeightRatio;

                    aswidth = (int)((float)width  - (float)(width * asWidthRatio));
                    asheight = (int)((float)height  - (float)(height * asHeightRatio));
                    asX = (width - aswidth) / 2;
                    asY = (height - asheight) / 2;
                    int rot = m->orientation;
                    if (fbwidth < fbheight) {
                         switch(rot) {
                         // ROT_0
                         case 0:
                         // ROT_180
                         case HAL_TRANSFORM_ROT_180: {
                                aswidth = (asheight * fbwidth) / fbheight;
                                asX = (width - aswidth) / 2;
                                if(rot ==  HAL_TRANSFORM_ROT_180)
                                  rot = OVERLAY_TRANSFORM_ROT_180;
                                else
                                  rot = 0;
                            }
                            break;
                         // ROT_90
                         case HAL_TRANSFORM_ROT_90:
                            rot = OVERLAY_TRANSFORM_ROT_270;
                            break;
                         // ROT_270
                         case HAL_TRANSFORM_ROT_270:
                            rot = OVERLAY_TRANSFORM_ROT_90;
                            break;
                        }
                    }
                    else if (fbwidth > fbheight) {
                         switch(rot) {
                         // ROT_0
                         case 0:
                            rot = 0;
                            break;
                         // ROT_180
                         case HAL_TRANSFORM_ROT_180:
                            rot = OVERLAY_TRANSFORM_ROT_180;
                            break;
                         // ROT_90
                         case HAL_TRANSFORM_ROT_90:
                         // ROT_270
                         case HAL_TRANSFORM_ROT_270: {
                                //Swap width and height
                                int t = fbwidth;
                                fbwidth = fbheight;
                                fbheight = t;
                                aswidth = (asheight * fbwidth) / fbheight;
                                asX = (width - aswidth) / 2;
                                if(rot == HAL_TRANSFORM_ROT_90)
                                    rot = OVERLAY_TRANSFORM_ROT_270;
                                else
                                    rot = OVERLAY_TRANSFORM_ROT_90;
                            }
                            break;
                        }
                    }
                    int currOrientation = 0;
                    pTemp->getOrientation(currOrientation);
                    if(rot != currOrientation) {
                        pTemp->setTransform(rot);
                    }
                    EVEN_OUT(asX);
                    EVEN_OUT(asY);
                    EVEN_OUT(aswidth);
                    EVEN_OUT(asheight);
                    int currentX = 0, currentY = 0;
                    uint32_t currentW = width, currentH = height;
                    if (pTemp->getPosition(currentX, currentY, currentW, currentH)) {
                        if ((currentX != asX) || (currentY != asY) || (currentW != aswidth)
                            || (currentH != asheight)) {
                            pTemp->setPosition(asX, asY, aswidth, asheight);
                        }
                    }
                    pTemp->queueBuffer(m->currentOffset);
                }
            }
            else
                pTemp->closeChannel();
        }
        pthread_mutex_unlock(&m->overlayLock);
    }
    return NULL;
}

static int fb_videoOverlayStarted(struct framebuffer_device_t* dev, int started)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);
    pthread_mutex_lock(&m->overlayLock);
    Overlay* pTemp = m->pobjOverlay;
    if(started != m->videoOverlay) {
        m->hdmiStateChanged = true;
        if (started && pTemp) {
            pTemp->closeChannel();
            m->videoOverlay = true;
            pthread_cond_signal(&(m->overlayPost));
        }
        else {
           m->videoOverlay = false;
           pthread_cond_signal(&(m->overlayPost));
        }
    }
    pthread_mutex_unlock(&m->overlayLock);
    return 0;
}

static int fb_enableHDMIOutput(struct framebuffer_device_t* dev, int enable)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);
    pthread_mutex_lock(&m->overlayLock);
    Overlay* pTemp = m->pobjOverlay;
    if (!enable && pTemp)
        pTemp->closeChannel();
    m->enableHDMIOutput = enable;
    m->hdmiStateChanged = true;
    pthread_cond_signal(&(m->overlayPost));
    pthread_mutex_unlock(&m->overlayLock);
    return 0;
}

static int fb_setActionSafeWidthRatio(struct framebuffer_device_t* dev, float asWidthRatio)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);
    pthread_mutex_lock(&m->overlayLock);
    m->actionsafeWidthRatio = asWidthRatio;
    pthread_mutex_unlock(&m->overlayLock);
    return 0;
}

static int fb_setActionSafeHeightRatio(struct framebuffer_device_t* dev, float asHeightRatio)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);
    pthread_mutex_lock(&m->overlayLock);
    m->actionsafeHeightRatio = asHeightRatio;
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
#endif

//Wait until framebuffer content is displayed.
//This is called in the context of threadLoop.
//Display loop wakes this up after display.
static int fb_waitForBufferPost(struct framebuffer_device_t* dev)
{
#if defined COMPOSITION_BYPASS
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);
    pthread_mutex_lock(&m->bufferPostLock);
    while(m->bufferPostDone == false) {
        pthread_cond_wait(&(m->bufferPostCond), &(m->bufferPostLock));
    }
    pthread_mutex_unlock(&m->bufferPostLock);
#endif
    return 0;
}

static int fb_resetBufferPostStatus(struct framebuffer_device_t* dev)
{
#if defined COMPOSITION_BYPASS
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);
    pthread_mutex_lock(&m->bufferPostLock);
    m->bufferPostDone = false;
    pthread_mutex_unlock(&m->bufferPostLock);
#endif
    return 0;
}
static int fb_post(struct framebuffer_device_t* dev, buffer_handle_t buffer)
{
    if (private_handle_t::validate(buffer) < 0)
        return -EINVAL;

    int nxtIdx, futureIdx = -1;
    bool reuse;
    struct qbuf_t qb;
    fb_context_t* ctx = (fb_context_t*)dev;

    private_handle_t const* hnd = reinterpret_cast<private_handle_t const*>(buffer);
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);

    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {

        reuse = false;
        nxtIdx = (m->currentIdx + 1) % m->numBuffers;
        futureIdx = (nxtIdx + 1) % m->numBuffers;

        if (m->swapInterval == 0) {
            // if SwapInterval = 0 and no buffers available then reuse
            // current buf for next rendering so don't post new buffer
            if (pthread_mutex_trylock(&(m->avail[nxtIdx].lock))) {
                reuse = true;
            } else {
                if (! m->avail[nxtIdx].is_avail)
                    reuse = true;
                pthread_mutex_unlock(&(m->avail[nxtIdx].lock));
            }
        }

        if(!reuse){
            // unlock previous ("current") Buffer and lock the new buffer
            m->base.lock(&m->base, buffer,
                    private_module_t::PRIV_USAGE_LOCKED_FOR_POST,
                    0,0, m->info.xres, m->info.yres, NULL);

            // post/queue the new buffer
            pthread_mutex_lock(&(m->avail[nxtIdx].lock));
            if (m->avail[nxtIdx].is_avail != true) {
                LOGE_IF(m->swapInterval != 0, "Found %d buf to be not avail", nxtIdx);
            }

            m->avail[nxtIdx].is_avail = false;

            if (m->avail[nxtIdx].state != AVL) {
                LOGD("[%d] state %c, expected %c", nxtIdx,
                    framebufferStateName[m->avail[nxtIdx].state],
                    framebufferStateName[AVL]);
            }

            m->avail[nxtIdx].state = SUB;
            pthread_mutex_unlock(&(m->avail[nxtIdx].lock));

            qb.idx = nxtIdx;
            qb.buf = buffer;
            pthread_mutex_lock(&(m->qlock));
            m->disp.push(qb);
            pthread_cond_signal(&(m->qpost));
            pthread_mutex_unlock(&(m->qlock));

            if (m->currentBuffer)
                m->base.unlock(&m->base, m->currentBuffer);

            m->currentBuffer = buffer;
            m->currentIdx = nxtIdx;
        } else {
            if (m->currentBuffer)
                m->base.unlock(&m->base, m->currentBuffer);
            m->base.lock(&m->base, buffer,
                         private_module_t::PRIV_USAGE_LOCKED_FOR_POST,
                         0,0, m->info.xres, m->info.yres, NULL);
            m->currentBuffer = buffer;
        }

    } else {
        void* fb_vaddr;
        void* buffer_vaddr;
        m->base.lock(&m->base, m->framebuffer,
                GRALLOC_USAGE_SW_WRITE_RARELY,
                0, 0, m->info.xres, m->info.yres,
                &fb_vaddr);

        m->base.lock(&m->base, buffer,
                GRALLOC_USAGE_SW_READ_RARELY,
                0, 0, m->info.xres, m->info.yres,
                &buffer_vaddr);

        //memcpy(fb_vaddr, buffer_vaddr, m->finfo.line_length * m->info.yres);

        msm_copy_buffer(
                m->framebuffer, m->framebuffer->fd,
                m->info.xres, m->info.yres, m->fbFormat,
                m->info.xoffset, m->info.yoffset,
                m->info.width, m->info.height);

        m->base.unlock(&m->base, buffer);
        m->base.unlock(&m->base, m->framebuffer);
    }

    LOGD_IF(FB_DEBUG, "Framebuffer state: [0] = %c [1] = %c [2] = %c",
        framebufferStateName[m->avail[0].state],
        framebufferStateName[m->avail[1].state],
        framebufferStateName[m->avail[2].state]);
    return 0;
}

static int fb_compositionComplete(struct framebuffer_device_t* dev)
{
    // TODO: Properly implement composition complete callback
    glFinish();

    return 0;
}

static int fb_lockBuffer(struct framebuffer_device_t* dev, int index)
{
    private_module_t* m = reinterpret_cast<private_module_t*>(
            dev->common.module);

    // Return immediately if the buffer is available
    if ((m->avail[index].state == AVL) || (m->swapInterval == 0))
        return 0;

    pthread_mutex_lock(&(m->avail[index].lock));
    while (m->avail[index].state != AVL) {
        pthread_cond_wait(&(m->avail[index].cond),
                         &(m->avail[index].lock));
    }
    pthread_mutex_unlock(&(m->avail[index].lock));

    return 0;
}

/*****************************************************************************/

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
        else if(property_get("debug.composition.type", property, NULL) > 0 && (strncmp(property, "mdp", 3) == 0))
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
    /*
     * Request NUM_BUFFERS screens (at lest 2 for page flipping)
     */
    int numberOfBuffers = (int)(finfo.smem_len/(info.yres * info.xres * (info.bits_per_pixel/8)));
    LOGV("num supported framebuffers in kernel = %d", numberOfBuffers);

    if (property_get("debug.gr.numframebuffers", property, NULL) > 0) {
        int num = atoi(property);
        if ((num >= NUM_FRAMEBUFFERS_MIN) && (num <= NUM_FRAMEBUFFERS_MAX)) {
            numberOfBuffers = num;
        }
    }
    if (numberOfBuffers > NUM_FRAMEBUFFERS_MAX)
        numberOfBuffers = NUM_FRAMEBUFFERS_MAX;

    LOGE("We support %d buffers", numberOfBuffers);

    info.yres_virtual = info.yres * numberOfBuffers;

    uint32_t flags = PAGE_FLIP;
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &info) == -1) {
        info.yres_virtual = info.yres;
        flags &= ~PAGE_FLIP;
        LOGW("FBIOPUT_VSCREENINFO failed, page flipping not supported");
    }

    if (info.yres_virtual < info.yres * 2) {
        // we need at least 2 for page-flipping
        info.yres_virtual = info.yres;
        flags &= ~PAGE_FLIP;
        LOGW("page flipping not supported (yres_virtual=%d, requested=%d)",
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
    //The reserved[4] field is used to store FPS by the driver.
    float fps  = info.reserved[4];

    LOGI(   "using (fd=%d)\n"
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

    LOGI(   "width        = %d mm (%f dpi)\n"
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

#ifdef NO_SURFACEFLINGER_SWAPINTERVAL
    char pval[PROPERTY_VALUE_MAX];
    property_get("debug.gr.swapinterval", pval, "1");
    module->swapInterval = atoi(pval);
    if (module->swapInterval < private_module_t::PRIV_MIN_SWAP_INTERVAL ||
        module->swapInterval > private_module_t::PRIV_MAX_SWAP_INTERVAL) {
        module->swapInterval = 1;
        LOGW("Out of range (%d to %d) value for debug.gr.swapinterval, using 1",
             private_module_t::PRIV_MIN_SWAP_INTERVAL,
             private_module_t::PRIV_MAX_SWAP_INTERVAL);
    }

#else
    /* when surfaceflinger supports swapInterval then can just do this */
    module->swapInterval = 1;
#endif

#ifdef DEBUG_CALC_FPS
    char prop[PROPERTY_VALUE_MAX];
    property_get("debug.gr.calcfps", prop, "0");
    debug_fps_level = atoi(prop);
    if (debug_fps_level > MAX_DEBUG_FPS_LEVEL) {
        LOGW("out of range value for debug.gr.calcfps, using 0");
        debug_fps_level = 0;
    }

    LOGE("DEBUG_CALC_FPS: %d", debug_fps_level);
    populate_debug_fps_metadata();
#endif

    module->currentIdx = -1;
    pthread_cond_init(&(module->qpost), NULL);
    pthread_mutex_init(&(module->qlock), NULL);
    for (i = 0; i < NUM_FRAMEBUFFERS_MAX; i++) {
        pthread_mutex_init(&(module->avail[i].lock), NULL);
        pthread_cond_init(&(module->avail[i].cond), NULL);
        module->avail[i].is_avail = true;
        module->avail[i].state = AVL;
    }

    /* create display update thread */
    pthread_t thread1;
    if (pthread_create(&thread1, NULL, &disp_loop, (void *) module)) {
         return -errno;
    }

    /*
     * map the framebuffer
     */

    int err;
    size_t fbSize = roundUpToPageSize(finfo.line_length * info.yres_virtual);
    module->framebuffer = new private_handle_t(dup(fd), fbSize,
            private_handle_t::PRIV_FLAGS_USES_PMEM, BUFFER_TYPE_UI, module->fbFormat, info.xres, info.yres);

    module->numBuffers = info.yres_virtual / info.yres;
    module->bufferMask = 0;

    void* vaddr = mmap(0, fbSize, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (vaddr == MAP_FAILED) {
        LOGE("Error mapping the framebuffer (%s)", strerror(errno));
        return -errno;
    }
    module->framebuffer->base = intptr_t(vaddr);
    memset(vaddr, 0, fbSize);

#if defined(HDMI_DUAL_DISPLAY)
    /* Overlay for HDMI*/
    pthread_mutex_init(&(module->overlayLock), NULL);
    pthread_cond_init(&(module->overlayPost), NULL);
    module->pobjOverlay = new Overlay();
    module->currentOffset = 0;
    module->exitHDMIUILoop = false;
    module->hdmiStateChanged = false;
    pthread_t hdmiUIThread;
    pthread_create(&hdmiUIThread, NULL, &hdmi_ui_loop, (void *) module);
#endif
#if defined COMPOSITION_BYPASS
    pthread_mutex_init(&(module->bufferPostLock), NULL);
    pthread_cond_init(&(module->bufferPostCond), NULL);
    module->bufferPostDone = false;
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
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = fb_close;
        dev->device.setSwapInterval = fb_setSwapInterval;
        dev->device.post            = fb_post;
        dev->device.setUpdateRect = 0;
        dev->device.compositionComplete = fb_compositionComplete;
        dev->device.lockBuffer = fb_lockBuffer;
#if defined(HDMI_DUAL_DISPLAY)
        dev->device.orientationChanged = fb_orientationChanged;
        dev->device.videoOverlayStarted = fb_videoOverlayStarted;
        dev->device.enableHDMIOutput = fb_enableHDMIOutput;
        dev->device.setActionSafeWidthRatio = fb_setActionSafeWidthRatio;
        dev->device.setActionSafeHeightRatio = fb_setActionSafeHeightRatio;
#endif

#if defined COMPOSITION_BYPASS
        dev->device.waitForBufferPost = fb_waitForBufferPost;
        dev->device.resetBufferPostStatus = fb_resetBufferPostStatus;
#endif

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
            const_cast<int&>(dev->device.minSwapInterval) = private_module_t::PRIV_MIN_SWAP_INTERVAL;
            const_cast<int&>(dev->device.maxSwapInterval) = private_module_t::PRIV_MAX_SWAP_INTERVAL;
            const_cast<int&>(dev->device.numFramebuffers) = m->numBuffers;
            if (m->finfo.reserved[0] == 0x5444 &&
                    m->finfo.reserved[1] == 0x5055) {
                dev->device.setUpdateRect = fb_setUpdateRect;
                LOGD("UPDATE_ON_DEMAND supported");
            }

            *device = &dev->device.common;
        }

        // Close the gralloc module
        gralloc_close(gralloc_device);
    }
    return status;
}

/* Copy a pmem buffer to the framebuffer */

static void
msm_copy_buffer(buffer_handle_t handle, int fd,
                int width, int height, int format,
                int x, int y, int w, int h)
{
    struct {
        unsigned int count;
        mdp_blit_req req;
    } blit;
    private_handle_t *priv = (private_handle_t*) handle;

    memset(&blit, 0, sizeof(blit));
    blit.count = 1;

    blit.req.flags = 0;
    blit.req.alpha = 0xff;
    blit.req.transp_mask = 0xffffffff;

    blit.req.src.width = width;
    blit.req.src.height = height;
    blit.req.src.offset = 0;
    blit.req.src.memory_id = priv->fd;

    blit.req.dst.width = width;
    blit.req.dst.height = height;
    blit.req.dst.offset = 0;
    blit.req.dst.memory_id = fd;
    blit.req.dst.format = format;

    blit.req.src_rect.x = blit.req.dst_rect.x = x;
    blit.req.src_rect.y = blit.req.dst_rect.y = y;
    blit.req.src_rect.w = blit.req.dst_rect.w = w;
    blit.req.src_rect.h = blit.req.dst_rect.h = h;

    if (ioctl(fd, MSMFB_BLIT, &blit))
        LOGE("MSMFB_BLIT failed = %d", -errno);
}
