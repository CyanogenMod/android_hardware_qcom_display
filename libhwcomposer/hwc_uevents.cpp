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
#ifndef HWC_OBSERVER_H
#define HWC_OBSERVER_H
#include <hardware_legacy/uevent.h>
#include <utils/Log.h>
#include <sys/resource.h>
#include <string.h>
#include <stdlib.h>
#include "hwc_utils.h"
#include "hwc_external.h"

namespace qhwc {

const char* MSMFB_DEVICE_CHANGE = "change@/devices/virtual/graphics/fb0";
const char* MSMFB_HDMI_NODE = "fb1";

static void handle_uevent(hwc_context_t* ctx, const char* udata, int len)
{
    int vsync;
    char* hdmi;
    int64_t timestamp = 0;
    const char *str = udata;
    hwc_procs* proc = reinterpret_cast<hwc_procs*>(ctx->device.reserved_proc[0]);

    vsync = !strncmp(str, MSMFB_DEVICE_CHANGE, strlen(MSMFB_DEVICE_CHANGE));
    hdmi = strcasestr(str, MSMFB_HDMI_NODE);
    if(!vsync && !hdmi)
        return;
    if(vsync) {
        str += strlen(str) + 1;
        while(*str) {
            if (!strncmp(str, "VSYNC=", strlen("VSYNC="))) {
                timestamp = strtoull(str + strlen("VSYNC="), NULL, 0);
                proc->vsync(proc, 0, timestamp);
            }
            str += strlen(str) + 1;
            if(str - udata >= len)
                break;
        }
    }

    if(hdmi) {
        // parse HDMI events
        // The event will be of the form:
        // change@/devices/virtual/graphics/fb1 ACTION=change
        // DEVPATH=/devices/virtual/graphics/fb1
        // SUBSYSTEM=graphics HDCP_STATE=FAIL MAJOR=29
        // for now just parsing onlin/offline info is enough
        str = udata;
        int connected = 0;
        if(!(strncmp(str,"online@",strlen("online@")))) {
            connected = 1;
        } else if(!(strncmp(str,"offline@",strlen("offline@")))) {
            connected = 0;
        }
        ctx->mExtDisplay->setExternalDisplayStatus(connected);
    }

}

static void *uevent_loop(void *param)
{
    int len = 0;
    static char udata[4096];
    memset(udata, 0, sizeof(udata));
    hwc_context_t * ctx = reinterpret_cast<hwc_context_t *>(param);

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
    pthread_create(&uevent_thread, NULL, uevent_loop, (void*) ctx);
}

}; //namespace
#endif //HWC_OBSERVER_H
