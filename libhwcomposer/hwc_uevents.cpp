/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-14, The Linux Foundation. All rights reserved.
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
#include "hdmi.h"
#include "hwc_virtual.h"
#include "mdp_version.h"
using namespace overlay;
namespace qhwc {
#define HWC_UEVENT_SWITCH_STR  "change@/devices/virtual/switch/"
#define HWC_UEVENT_THREAD_NAME "hwcUeventThread"

/* Parse uevent data for devices which we are interested */
static int getConnectedDisplay(hwc_context_t* ctx, const char* strUdata)
{
    int ret = -1;
    // Switch node for HDMI as PRIMARY/EXTERNAL
    if(strcasestr("change@/devices/virtual/switch/hdmi", strUdata)) {
        if (ctx->mHDMIDisplay->isHDMIPrimaryDisplay()) {
            ret = HWC_DISPLAY_PRIMARY;
        } else {
            ret = HWC_DISPLAY_EXTERNAL;
        }
    }
    return ret;
}

static bool getPanelResetStatus(hwc_context_t* ctx, const char* strUdata, int len)
{
    const char* iter_str = strUdata;
    if (strcasestr("change@/devices/virtual/graphics/fb0", strUdata)) {
        while(((iter_str - strUdata) <= len) && (*iter_str)) {
            char* pstr = strstr(iter_str, "PANEL_ALIVE=0");
            if (pstr != NULL) {
                ALOGI("%s: got change event in fb0 with PANEL_ALIVE=0",
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

    int dpy = getConnectedDisplay(ctx, udata);
    if(dpy < 0) {
        ALOGD_IF(UEVENT_DEBUG, "%s: Not disp Event ", __FUNCTION__);
        return;
    }

    int switch_state = getConnectedState(udata, len);

    ALOGE_IF(UEVENT_DEBUG,"%s: uevent received: %s switch state: %d",
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
            handle_offline(ctx, dpy);
            ctx->mDrawLock.unlock();

            /* We need to send hotplug to SF only when we are disconnecting
             * HDMI as an external display. */
            if(dpy == HWC_DISPLAY_EXTERNAL) {
                ALOGE_IF(UEVENT_DEBUG,"%s:Sending EXTERNAL OFFLINE hotplug"
                        "event", __FUNCTION__);
                ctx->proc->hotplug(ctx->proc, dpy, EXTERNAL_OFFLINE);
            }
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

            if (ctx->mHDMIDisplay->isHDMIPrimaryDisplay()) {
                ctx->mDrawLock.lock();
                handle_online(ctx, dpy);
                ctx->mDrawLock.unlock();

                ctx->proc->invalidate(ctx->proc);
                break;
            } else {
                ctx->mDrawLock.lock();
                //Force composition to give up resources like pipes and
                //close fb. For example if assertive display is going on,
                //fb2 could be open, thus connecting Layer Mixer#0 to
                //WriteBack module. If HDMI attempts to open fb1, the driver
                //will try to attach Layer Mixer#0 to HDMI INT, which will
                //fail, since Layer Mixer#0 is still connected to WriteBack.
                //This block will force composition to close fb2 in above
                //example.
                ctx->dpyAttr[dpy].isConfiguring = true;
                ctx->mDrawLock.unlock();

                ctx->proc->invalidate(ctx->proc);
            }
            //2 cycles for slower content
            usleep(ctx->dpyAttr[HWC_DISPLAY_PRIMARY].vsync_period
                   * 2 / 1000);

            if(isVDConnected(ctx)) {
                // Do not initiate WFD teardown if WFD architecture is based
                // on VDS mechanism.
                // WFD Stack listens to HDMI intent and initiates virtual
                // display teardown.
                // ToDo: Currently non-WFD Virtual display clients do not
                // involve HWC. If there is a change, we need to come up
                // with mechanism of how to address non-WFD Virtual display
                // clients + HDMI
                ctx->mWfdSyncLock.lock();
                ALOGD_IF(HWC_WFDDISPSYNC_LOG,
                        "%s: Waiting for wfd-teardown to be signalled",
                        __FUNCTION__);
                ctx->mWfdSyncLock.wait();
                ALOGD_IF(HWC_WFDDISPSYNC_LOG,
                        "%s: Teardown signalled. Completed waiting in"
                        "uevent thread", __FUNCTION__);
                ctx->mWfdSyncLock.unlock();
            }
            ctx->mHDMIDisplay->configure();
            ctx->mHDMIDisplay->activateDisplay();

            ctx->mDrawLock.lock();
            updateDisplayInfo(ctx, dpy);
            initCompositionResources(ctx, dpy);
            ctx->dpyAttr[dpy].isPause = false;
            ctx->dpyAttr[dpy].connected = true;
            ctx->dpyAttr[dpy].isConfiguring = true;
            ctx->mDrawLock.unlock();

            /* External display is HDMI */
            ALOGE_IF(UEVENT_DEBUG, "%s: Sending EXTERNAL ONLINE"
                    "hotplug event", __FUNCTION__);
            ctx->proc->hotplug(ctx->proc, dpy, EXTERNAL_ONLINE);
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
    androidSetThreadPriority(0, HAL_PRIORITY_URGENT_DISPLAY);
    if(!uevent_init()) {
        ALOGE("%s: failed to init uevent ",__FUNCTION__);
        return NULL;
    }

    while(1) {
        len = uevent_next_event(udata, (int)sizeof(udata) - 2);
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
