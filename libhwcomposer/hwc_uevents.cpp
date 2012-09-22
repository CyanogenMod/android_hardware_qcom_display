/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
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
#define DEBUG 0
#include <hardware_legacy/uevent.h>
#include <utils/Log.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <string.h>
#include <stdlib.h>
#include "hwc_utils.h"
#include "hwc_external.h"

#define PAGE_SIZE 4096

namespace qhwc {

static void handle_uevent(hwc_context_t* ctx, const char* udata, int len)
{
    int vsync = 0;
    int64_t timestamp = 0;
    const char *str = udata;

    if(!strcasestr(str, "@/devices/virtual/graphics/fb")) {
        ALOGD_IF(DEBUG, "%s: Not Ext Disp Event ", __FUNCTION__);
        return;
    }

    // parse HDMI events
    // The event will be of the form:
    // change@/devices/virtual/graphics/fb1 ACTION=change
    // DEVPATH=/devices/virtual/graphics/fb1
    // SUBSYSTEM=graphics HDCP_STATE=FAIL MAJOR=29
    // for now just parsing onlin/offline info is enough
    str = udata;
    if(!(strncmp(str,"online@",strlen("online@")))) {
        strncpy(ctx->mHDMIEvent,str,strlen(str));
        ctx->hdmi_pending = true;
        //Invalidate
        hwc_procs* proc = (hwc_procs*)ctx->device.reserved_proc[0];
        if(!proc) {
            ALOGE("%s: HWC proc not registered", __FUNCTION__);
        } else {
            proc->invalidate(proc);
        }
    } else if(!(strncmp(str,"offline@",strlen("offline@")))) {
        ctx->hdmi_pending = false;
        ctx->mExtDisplay->processUEventOffline(str);
    }
}

static void *uevent_loop(void *param)
{
    int len = 0;
    static char udata[PAGE_SIZE];
    hwc_context_t * ctx = reinterpret_cast<hwc_context_t *>(param);

    char thread_name[64] = "hwcUeventThread";
    prctl(PR_SET_NAME, (unsigned long) &thread_name, 0, 0, 0);
    setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);
    uevent_init();

    while(1) {
        len = uevent_next_event(udata, sizeof(udata) - 2);
        handle_uevent(ctx, udata, len);
    }

    return NULL;
}

void init_uevent_thread(hwc_context_t* ctx)
{
    pthread_t uevent_thread;
    ALOGI("Initializing UEvent Listener Thread");
    pthread_create(&uevent_thread, NULL, uevent_loop, (void*) ctx);
}

}; //namespace
