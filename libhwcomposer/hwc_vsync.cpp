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

// WARNING : Excessive logging, if VSYNC_DEBUG enabled
#define VSYNC_DEBUG 0

#include <utils/Log.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/msm_mdp.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include "hwc_utils.h"
#include "string.h"
#include "external.h"


namespace qhwc {

static void *vsync_loop(void *param)
{
    const char* vsync_timestamp_fb0 = "/sys/class/graphics/fb0/vsync_event";
    const char* vsync_timestamp_fb1 = "/sys/class/graphics/fb1/vsync_event";
    int dpy = HWC_DISPLAY_PRIMARY;

    hwc_context_t * ctx = reinterpret_cast<hwc_context_t *>(param);

    char thread_name[64] = "hwcVsyncThread";
    prctl(PR_SET_NAME, (unsigned long) &thread_name, 0, 0, 0);
    setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY +
                android::PRIORITY_MORE_FAVORABLE);

    static char vdata[PAGE_SIZE];

    uint64_t cur_timestamp=0;
    ssize_t len = -1;
    int fd_timestamp = -1;
    int ret = 0;
    bool fb1_vsync = false;
    bool enabled = false;

    /* Currently read vsync timestamp from drivers
       e.g. VSYNC=41800875994
    */

    do {
        pthread_mutex_lock(&ctx->vstate.lock);
        while (ctx->vstate.enable == false) {
            if(enabled) {
                int e = 0;
                if(ioctl(ctx->dpyAttr[dpy].fd, MSMFB_OVERLAY_VSYNC_CTRL,
                         &e) < 0) {
                    ALOGE("%s: vsync control failed. Dpy=%d, enabled=%d : %s",
                          __FUNCTION__, dpy, enabled, strerror(errno));
                    ret = -errno;
                }
                enabled = false;
            }
            pthread_cond_wait(&ctx->vstate.cond, &ctx->vstate.lock);
        }
        pthread_mutex_unlock(&ctx->vstate.lock);

        if (!enabled) {
            int e = 1;
            if(ioctl(ctx->dpyAttr[dpy].fd, MSMFB_OVERLAY_VSYNC_CTRL,
                                          &e) < 0) {
                ALOGE("%s: vsync control failed. Dpy=%d, enabled=%d : %s",
                      __FUNCTION__, dpy, enabled, strerror(errno));
                ret = -errno;
            }
            enabled = true;
        }

        fd_timestamp = open(vsync_timestamp_fb0, O_RDONLY);
        if (fd_timestamp < 0) {
            ALOGE ("FATAL:%s:not able to open file:%s, %s",  __FUNCTION__,
                   (fb1_vsync) ? vsync_timestamp_fb1 : vsync_timestamp_fb0,
                                                     strerror(errno));
            return NULL;
       }
       // Open success - read now
       len = read(fd_timestamp, vdata, PAGE_SIZE);
       if (len < 0){
           ALOGE ("FATAL:%s:not able to read file:%s, %s", __FUNCTION__,
                  vsync_timestamp_fb0, strerror(errno));
           close (fd_timestamp);
           return NULL;
       }

       // extract timestamp
       const char *str = vdata;
       if (!strncmp(str, "VSYNC=", strlen("VSYNC="))) {
          cur_timestamp = strtoull(str + strlen("VSYNC="), NULL, 0);
       } else {
          ALOGE ("FATAL:%s:timestamp data not in correct format",
                                                 __FUNCTION__);
       }
       // send timestamp to HAL
       ALOGD_IF (VSYNC_DEBUG, "%s: timestamp %llu sent to HWC for %s",
            __FUNCTION__, cur_timestamp, "fb0");
       ctx->proc->vsync(ctx->proc, dpy, cur_timestamp);

       close (fd_timestamp);
    } while (true);

    return NULL;
}

void init_vsync_thread(hwc_context_t* ctx)
{
    pthread_t vsync_thread;
    ALOGI("Initializing VSYNC Thread");
    pthread_create(&vsync_thread, NULL, vsync_loop, (void*) ctx);
}

}; //namespace
