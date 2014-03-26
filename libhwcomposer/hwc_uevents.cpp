
/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are
 * retained for attribution purposes only.

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
#define UEVENT_DEBUG 0
#include <hardware_legacy/uevent.h>
#include <utils/Log.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <string.h>
#include <stdlib.h>
#include "hwc_utils.h"
#include "hwc_fbupdate.h"
#include "hwc_mdpcomp.h"
#include "hwc_copybit.h"
#include "comptype.h"
#include "external.h"
#include "virtual.h"
#include "mdp_version.h"
using namespace overlay;
namespace qhwc {
#define HWC_UEVENT_SWITCH_STR  "change@/devices/virtual/switch/"
#define HWC_UEVENT_THREAD_NAME "hwcUeventThread"

/* External Display states */
enum {
    EXTERNAL_OFFLINE = 0,
    EXTERNAL_ONLINE,
    EXTERNAL_PAUSE,
    EXTERNAL_RESUME
};

static void setup(hwc_context_t* ctx, int dpy)
{
    ctx->mFBUpdate[dpy] =
        IFBUpdate::getObject(ctx, ctx->dpyAttr[dpy].xres, dpy);
    ctx->mMDPComp[dpy] =
        MDPComp::getObject(ctx->dpyAttr[dpy].xres, dpy);
    int compositionType =
                qdutils::QCCompositionType::getInstance().getCompositionType();
    if (compositionType & (qdutils::COMPOSITION_TYPE_DYN |
                           qdutils::COMPOSITION_TYPE_MDP |
                           qdutils::COMPOSITION_TYPE_C2D)) {
        ctx->mCopyBit[dpy] = new CopyBit(ctx, dpy);
    }
}

static void clear(hwc_context_t* ctx, int dpy)
{
    if(ctx->mFBUpdate[dpy]) {
        delete ctx->mFBUpdate[dpy];
        ctx->mFBUpdate[dpy] = NULL;
    }
    if(ctx->mCopyBit[dpy]){
        delete ctx->mCopyBit[dpy];
        ctx->mCopyBit[dpy] = NULL;
    }
    if(ctx->mMDPComp[dpy]) {
        delete ctx->mMDPComp[dpy];
        ctx->mMDPComp[dpy] = NULL;
    }
}

/* Parse uevent data for devices which we are interested */
static int getConnectedDisplay(const char* strUdata)
{
    if(strcasestr("change@/devices/virtual/switch/hdmi", strUdata))
        return HWC_DISPLAY_EXTERNAL;
    if(strcasestr("change@/devices/virtual/switch/wfd", strUdata))
        return HWC_DISPLAY_VIRTUAL;
    return -1;
}

static bool getPanelResetStatus(hwc_context_t* ctx, const char* strUdata, int len)
{
    const char* iter_str = strUdata;
    if (strcasestr("change@/devices/virtual/graphics/fb0", strUdata)) {
        while(((iter_str - strUdata) <= len) && (*iter_str)) {
            char* pstr = strstr(iter_str, "PANEL_ALIVE=0");
            if (pstr != NULL) {
                ALOGE("%s: got change event in fb0 with PANEL_ALIVE=0",
                                                           __FUNCTION__);
                ctx->mPanelResetStatus = true;
                return true;
            }
            iter_str += strlen(iter_str)+1;
        }
    }
    return false;
}

/* Parse uevent data for action requested for the display */
static int getConnectedState(const char* strUdata, int len)
{
    const char* iter_str = strUdata;
    while(((iter_str - strUdata) <= len) && (*iter_str)) {
        char* pstr = strstr(iter_str, "SWITCH_STATE=");
        if (pstr != NULL) {
            return (atoi(pstr + strlen("SWITCH_STATE=")));
        }
        iter_str += strlen(iter_str)+1;
    }
    return -1;
}

static void handle_uevent(hwc_context_t* ctx, const char* udata, int len)
{
    bool bpanelReset = getPanelResetStatus(ctx, udata, len);
    if (bpanelReset) {
        ctx->proc->invalidate(ctx->proc);
        return;
    }

    int dpy = getConnectedDisplay(udata);
    if(dpy < 0) {
        ALOGD_IF(UEVENT_DEBUG, "%s: Not disp Event ", __FUNCTION__);
        return;
    }

    int switch_state = getConnectedState(udata, len);

    ALOGE_IF(UEVENT_DEBUG,"%s: uevent recieved: %s switch state: %d",
             __FUNCTION__,udata, switch_state);

    switch(switch_state) {
    case EXTERNAL_OFFLINE:
        {
            /* Display not connected */
            if(!ctx->dpyAttr[dpy].connected){
                ALOGE_IF(UEVENT_DEBUG,"%s: Ignoring EXTERNAL_OFFLINE event"
                         "for display: %d", __FUNCTION__, dpy);
                break;
            }

            ctx->mDrawLock.lock();
            clear(ctx, dpy);
            ctx->dpyAttr[dpy].connected = false;
            ctx->dpyAttr[dpy].isActive = false;

            /* We need to send hotplug to SF only when we are disconnecting
             * (1) HDMI OR (2) proprietary WFD session */
            if(dpy == HWC_DISPLAY_EXTERNAL ||
                    ctx->mVirtualonExtActive) {
                ALOGE_IF(UEVENT_DEBUG,"%s:Sending EXTERNAL OFFLINE hotplug"
                        "event", __FUNCTION__);
                ctx->proc->hotplug(ctx->proc, HWC_DISPLAY_EXTERNAL,
                        EXTERNAL_OFFLINE);
                ctx->mVirtualonExtActive = false;
            }
            ctx->proc->invalidate(ctx->proc);
            ctx->mDrawLock.wait();
            // At this point all the pipes used by External have been
            // marked as UNSET.

            // Perform commit to unstage the pipes.
            if (!Overlay::displayCommit(ctx->dpyAttr[dpy].fd)) {
                ALOGE("%s: display commit fail! for %d dpy",
                        __FUNCTION__, dpy);
            }

            if(dpy == HWC_DISPLAY_EXTERNAL) {
                ctx->mExtDisplay->teardown();
            } else {
                ctx->mVirtualDisplay->teardown();
            }
            ctx->mDrawLock.unlock();

            break;
        }
    case EXTERNAL_ONLINE:
        {
            /* Display already connected */
            if(ctx->dpyAttr[dpy].connected) {
                ALOGE_IF(UEVENT_DEBUG,"%s: Ignoring EXTERNAL_ONLINE event"
                         "for display: %d", __FUNCTION__, dpy);
                break;
            }

            //Force composition to give up resources like pipes and
            //close fb. For example if assertive display is going on,
            //fb2 could be open, thus connecting Layer Mixer#0 to
            //WriteBack module. If HDMI attempts to open fb1, the driver
            //will try to attach Layer Mixer#0 to HDMI INT, which will
            //fail, since Layer Mixer#0 is still connected to WriteBack.
            //This block will force composition to close fb2 in above
            //example.
            ctx->mDrawLock.lock();
            ctx->dpyAttr[dpy].isConfiguring = true;
            ctx->proc->invalidate(ctx->proc);

            ctx->mDrawLock.wait();
            ctx->mDrawLock.unlock();
            if(dpy == HWC_DISPLAY_EXTERNAL) {
                if(ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].connected) {
                    ALOGD_IF(UEVENT_DEBUG,"Received HDMI connection request"
                             "when WFD is active");

                    ctx->mDrawLock.lock();
                    clear(ctx, HWC_DISPLAY_VIRTUAL);
                    ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].connected = false;
                    ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].isActive = false;

                    /* Need to send hotplug only when connected WFD in
                     * proprietary path */
                    if(ctx->mVirtualonExtActive) {
                        ALOGE_IF(UEVENT_DEBUG,"%s: Sending EXTERNAL OFFLINE"
                                "hotplug event", __FUNCTION__);
                        ctx->proc->hotplug(ctx->proc, HWC_DISPLAY_EXTERNAL,
                                EXTERNAL_OFFLINE);
                        ctx->mVirtualonExtActive = false;
                    }
                    ctx->proc->invalidate(ctx->proc);

                    ctx->mDrawLock.wait();
                    // At this point all the pipes used by WFD(Virtual) have been
                    // marked as UNSET.
                    // Perform commit to unstage the pipes.
                    if (!Overlay::displayCommit(ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].fd)) {
                        ALOGE("%s: display commit fail! for %d dpy",
                                __FUNCTION__, HWC_DISPLAY_VIRTUAL);
                    }
                    ctx->mDrawLock.unlock();

                    ctx->mVirtualDisplay->teardown();
                }
                ctx->mExtDisplay->configure();
            } else {
                {
                    Locker::Autolock _l(ctx->mDrawLock);
                    /* TRUE only when we are on proprietary WFD session */
                    ctx->mVirtualonExtActive = true;
                    char property[PROPERTY_VALUE_MAX];
                    if((property_get("persist.sys.wfd.virtual",
                                                  property, NULL) > 0) &&
                       (!strncmp(property, "1", PROPERTY_VALUE_MAX ) ||
                       (!strncasecmp(property,"true", PROPERTY_VALUE_MAX )))) {
                        // This means we are on Google's WFD session
                        ctx->mVirtualonExtActive = false;
                    }
                }
                ctx->mVirtualDisplay->configure();
            }

            Locker::Autolock _l(ctx->mDrawLock);
            setup(ctx, dpy);
            ctx->dpyAttr[dpy].isPause = false;
            ctx->dpyAttr[dpy].connected = true;
            ctx->dpyAttr[dpy].isConfiguring = true;

            if(dpy == HWC_DISPLAY_EXTERNAL ||
               ctx->mVirtualonExtActive) {
                /* External display is HDMI or non-hybrid WFD solution */
                ALOGE_IF(UEVENT_DEBUG, "%s: Sending EXTERNAL_OFFLINE ONLINE"
                         "hotplug event", __FUNCTION__);
                ctx->proc->hotplug(ctx->proc,HWC_DISPLAY_EXTERNAL,
                                   EXTERNAL_ONLINE);
            } else {
                /* We wont be getting unblank for VIRTUAL DISPLAY and its
                 * always guaranteed from WFD stack that CONNECT uevent for
                 * VIRTUAL DISPLAY will be triggered before creating
                 * surface for the same. */
                ctx->dpyAttr[HWC_DISPLAY_VIRTUAL].isActive = true;
            }
            break;
        }
        case EXTERNAL_PAUSE:
            {   // pause case

                 ALOGD("%s Received Pause event",__FUNCTION__);
                 /* Display already in pause */
                 if(ctx->dpyAttr[dpy].isPause) {
                    ALOGE_IF(UEVENT_DEBUG,"%s: Ignoring EXTERNAL_PAUSE event"
                             "for display: %d", __FUNCTION__, dpy);
                    break;
                 }

                 ctx->mDrawLock.lock();
                 ctx->dpyAttr[dpy].isActive = true;
                 ctx->dpyAttr[dpy].isPause = true;
                 ctx->proc->invalidate(ctx->proc);

                 ctx->mDrawLock.wait();
                 // At this point all the pipes used by External have been
                 // marked as UNSET.
                 // Perform commit to unstage the pipes.
                 if (!Overlay::displayCommit(ctx->dpyAttr[dpy].fd)) {
                     ALOGE("%s: display commit fail! for %d dpy",
                             __FUNCTION__, dpy);
                 }
                 ctx->mDrawLock.unlock();

                 break;
            }
        case EXTERNAL_RESUME:
            {  // resume case

                ALOGD("%s Received resume event",__FUNCTION__);

                /* Display already is resumed */
                if(not ctx->dpyAttr[dpy].isPause) {
                    ALOGE_IF(UEVENT_DEBUG,"%s: Ignoring EXTERNAL_RESUME event"
                             "for display: %d", __FUNCTION__, dpy);
                    break;
                }

                //Treat Resume as Online event
                //Since external didnt have any pipes, force primary to give up
                //its pipes; we don't allow inter-mixer pipe transfers.

                ctx->mDrawLock.lock();
                ctx->dpyAttr[dpy].isConfiguring = true;
                ctx->dpyAttr[dpy].isActive = true;
                ctx->proc->invalidate(ctx->proc);

                ctx->mDrawLock.wait();
                //At this point external has all the pipes it would need.
                ctx->dpyAttr[dpy].isPause = false;
                ctx->proc->invalidate(ctx->proc);
                ctx->mDrawLock.unlock();

                break;
            }
    default:
        {
            ALOGE("%s: Invalid state to swtich:%d", __FUNCTION__, switch_state);
            break;
        }
    }
}

static void *uevent_loop(void *param)
{
    int len = 0;
    static char udata[PAGE_SIZE];
    hwc_context_t * ctx = reinterpret_cast<hwc_context_t *>(param);
    char thread_name[64] = HWC_UEVENT_THREAD_NAME;
    prctl(PR_SET_NAME, (unsigned long) &thread_name, 0, 0, 0);
    setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);
    if(!uevent_init()) {
        ALOGE("%s: failed to init uevent ",__FUNCTION__);
        return NULL;
    }

    while(1) {
        len = uevent_next_event(udata, sizeof(udata) - 2);
        handle_uevent(ctx, udata, len);
    }

    return NULL;
}

void init_uevent_thread(hwc_context_t* ctx)
{
    pthread_t uevent_thread;
    int ret;

    ALOGI("Initializing UEVENT Thread");
    ret = pthread_create(&uevent_thread, NULL, uevent_loop, (void*) ctx);
    if (ret) {
        ALOGE("%s: failed to create %s: %s", __FUNCTION__,
            HWC_UEVENT_THREAD_NAME, strerror(ret));
    }
}

}; //namespace
