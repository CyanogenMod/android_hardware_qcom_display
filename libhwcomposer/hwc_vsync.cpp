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
#include <sys/resource.h>
#include <sys/prctl.h>
#include "hwc_utils.h"
#include "hwc_external.h"
#include "string.h"

#define PAGE_SIZE 4096

namespace qhwc {

static void *vsync_loop(void *param)
{
    const char* vsync_timestamp_fb0 = "/sys/class/graphics/fb0/vsync_event";
    const char* vsync_timestamp_fb1 = "/sys/class/graphics/fb1/vsync_event";

    hwc_context_t * ctx = reinterpret_cast<hwc_context_t *>(param);

    char thread_name[64] = "hwcVsyncThread";
    prctl(PR_SET_NAME, (unsigned long) &thread_name, 0, 0, 0);
    setpriority(PRIO_PROCESS, 0, 
                HAL_PRIORITY_URGENT_DISPLAY + ANDROID_PRIORITY_MORE_FAVORABLE);

    static char vdata[PAGE_SIZE];

    uint64_t cur_timestamp=0;
    int32_t len = -1, fd_timestamp = -1;
    bool fb1_vsync = false;

    /* Currently read vsync timestamp from drivers
       e.g. VSYNC=41800875994
    */

    hwc_procs* proc = (hwc_procs*)ctx->device.reserved_proc[0];

    do {
        pthread_mutex_lock(&ctx->vstate.lock);
        if(ctx->vstate.enable == false) {
          pthread_cond_wait(&ctx->vstate.cond, &ctx->vstate.lock);
        }
        pthread_mutex_unlock(&ctx->vstate.lock);

       int hdmiconnected = ctx->mExtDisplay->getExternalDisplay();

       // vsync for primary OR HDMI ?
       if(ctx->mExtDisplay->isHDMIConfigured() &&
              (hdmiconnected == EXTERN_DISPLAY_FB1)){
          fb1_vsync = true;
       } else {
          fb1_vsync = false;
       }

       // try to open timestamp sysfs
       if (fb1_vsync){
          fd_timestamp = open(vsync_timestamp_fb1, O_RDONLY);
       } else {
          fd_timestamp = open(vsync_timestamp_fb0, O_RDONLY);
       }
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
                (fb1_vsync) ? vsync_timestamp_fb1 : vsync_timestamp_fb0,
                                                        strerror(errno));
           fd_timestamp = -1;
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
            __FUNCTION__, cur_timestamp, (fb1_vsync) ? "fb1" : "fb0");
      proc->vsync(proc, 0, cur_timestamp);

      // close open fds
      close (fd_timestamp);

      // reset fd
      fd_timestamp = -1;

      // repeat, whatever, you just did
    } while (true);
}

void init_vsync_thread(hwc_context_t* ctx)
{
    pthread_t vsync_thread;
    ALOGI("Initializing VSYNC Thread");
    pthread_create(&vsync_thread, NULL, vsync_loop, (void*) ctx);
}

}; //namespace
