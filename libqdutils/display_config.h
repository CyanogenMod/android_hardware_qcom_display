/*
 * Copyright (c) 2013 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name of The Linux Foundation. nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DISPLAY_CONFIG_H
#define _DISPLAY_CONFIG_H

#include <gralloc_priv.h>
#include <qdMetaData.h>
#include <hardware/hwcomposer.h>

// This header is for clients to use to set/get global display configuration.
// Only primary and external displays are supported here.

namespace qdutils {


/* TODO: Have all the common enums that need be exposed to clients and which
 * are also needed in hwc defined here. Remove such definitions we have in
 * hwc_utils.h
 */

// Use this enum to specify the dpy parameters where needed
enum {
    DISPLAY_PRIMARY = 0,
    DISPLAY_EXTERNAL,
    DISPLAY_TERTIARY,
    DISPLAY_VIRTUAL,
};

// External Display states - used in setSecondaryDisplayStatus()
// To be consistent with the same defined in hwc_utils.h
enum {
    EXTERNAL_OFFLINE = 0,
    EXTERNAL_ONLINE,
    EXTERNAL_PAUSE,
    EXTERNAL_RESUME,
};

enum {
    DISABLE_METADATA_DYN_REFRESH_RATE = 0,
    ENABLE_METADATA_DYN_REFRESH_RATE,
    SET_BINDER_DYN_REFRESH_RATE,
};

enum {
    DEFAULT_MODE = 0,
    VIDEO_MODE,
    COMMAND_MODE,
};

// Display Attributes that are available to clients of this library
// Not to be confused with a similar struct in hwc_utils (in the hwc namespace)
typedef struct DisplayAttributes {
    uint32_t vsync_period; //nanoseconds
    uint32_t xres;
    uint32_t yres;
    float xdpi;
    float ydpi;
    char panel_type;
    DisplayAttributes() : vsync_period(0), xres(0), yres(0), xdpi(0.0f),
            ydpi(0.0f), panel_type(0) {}
} DisplayAttributes_t;

//=============================================================================
// The functions below run in the client process and wherever necessary
// do a binder call to HWC to get/set data.

// Check if external display is connected. Useful to check before making
// calls for external displays
// Returns 1 if connected, 0 if disconnected, negative values on errors
int isExternalConnected(void);

// Get display vsync period which is in nanoseconds
// i.e vsync_period = 1000000000l / fps
// Returns 0 on success, negative values on errors
int getDisplayAttributes(int dpy, DisplayAttributes_t& dpyattr);

// Set HSIC data on a given display ID
// Returns 0 on success, negative values on errors
int setHSIC(int dpy, const HSICData_t& hsic_data);

// get the active visible region for the display
// Returns 0 on success, negative values on errors
int getDisplayVisibleRegion(int dpy, hwc_rect_t &rect);

// set the view frame information in hwc context from surfaceflinger
int setViewFrame(int dpy, int l, int t, int r, int b);

// Set the secondary display status(pause/resume/offline etc.,)
int setSecondaryDisplayStatus(int dpy, uint32_t status);

// Enable/Disable/Set refresh rate dynamically
int configureDynRefreshRate(uint32_t op, uint32_t refreshRate);

// Returns the number of configs supported for the display on success.
// Returns -1 on error.
// Only primary display supported for now, value of dpy ignored.
int getConfigCount(int dpy);

// Returns the index of config that is current set for the display on success.
// Returns -1 on error.
// Only primary display supported for now, value of dpy ignored.
int getActiveConfig(int dpy);

// Sets the config for the display on success and returns 0.
// Returns -1 on error.
// Only primary display supported for now, value of dpy ignored
int setActiveConfig(int configIndex, int dpy);

// Returns the attributes for the specified config for the display on success.
// Returns xres and yres as 0 on error.
// Only primary display supported for now, value of dpy ignored
DisplayAttributes getDisplayAttributes(int configIndex, int dpy);

// Set the primary display mode to command or video mode
int setDisplayMode(int mode);

// Sets the panel brightness of the primary display
int setPanelBrightness(int level);

// Retrieves the current panel brightness value
int getPanelBrightness();

}; //namespace

#endif
