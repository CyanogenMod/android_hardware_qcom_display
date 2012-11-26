/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2012, Code Aurora Forum. All rights reserved.
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

#ifndef FB_PRIV_H
#define FB_PRIV_H
#include <linux/fb.h>

#define NUM_FRAMEBUFFERS_MIN  2
#define NUM_FRAMEBUFFERS_MAX  3

#define NO_SURFACEFLINGER_SWAPINTERVAL
#define COLOR_FORMAT(x) (x & 0xFFF) // Max range for colorFormats is 0 - FFF

struct private_handle_t;

namespace overlay {
class Overlay;
}

enum {
    // flag to indicate we'll post this buffer
    PRIV_USAGE_LOCKED_FOR_POST = 0x80000000,
    PRIV_MIN_SWAP_INTERVAL = 0,
    PRIV_MAX_SWAP_INTERVAL = 1,
};

struct private_module_t {
    gralloc_module_t base;
    struct private_handle_t* framebuffer;
    uint32_t fbFormat;
    uint32_t flags;
    uint32_t numBuffers;
    uint32_t bufferMask;
    pthread_mutex_t lock;
    private_handle_t *currentBuffer;
    struct fb_var_screeninfo info;
    struct fb_fix_screeninfo finfo;
    float xdpi;
    float ydpi;
    float fps;
    uint32_t swapInterval;
    uint32_t currentOffset;
    bool fbPostDone;
    overlay::Overlay *overlay;
    pthread_mutex_t fbPostLock;
    //Condition to inform HWC that fb_post called
    pthread_cond_t fbPostCond;
    bool fbPanDone;
    pthread_mutex_t fbPanLock;
    pthread_cond_t fbPanCond;
};



#endif /* FB_PRIV_H */
