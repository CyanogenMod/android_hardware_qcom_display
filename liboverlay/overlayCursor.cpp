/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
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

//#include "overlay.h"
#include "overlayCursor.h"
#include "mdpWrapper.h"

namespace overlay {

HWCursor* HWCursor::sHwCursor = 0;

//=========== class HWCursor =================================================
HWCursor* HWCursor::getInstance() {
    if (sHwCursor == NULL) {
        sHwCursor = new HWCursor();
    }
    return sHwCursor;
}

bool HWCursor::config(const int fd, void* base, PipeArgs& pargs,
                    Dim& crop, Dim& dest) {
    bool ret = true;
    fb_cursor *cursor = &mfbCursor;
    fb_image cursorImage;

    cursor->set = FB_CUR_SETIMAGE | FB_CUR_SETPOS;
    cursor->enable = (uint16_t)1;
    cursor->rop = 0,
    cursor->mask = NULL;
    cursor->hot.x = (uint16_t)crop.x;
    cursor->hot.y = (uint16_t)crop.y;

    cursorImage.dx = dest.x;
    cursorImage.dy = dest.y;
    cursorImage.width = pargs.whf.w;
    cursorImage.height = pargs.whf.h;
    cursorImage.fg_color = pargs.planeAlpha; // Hint for PMA
    cursorImage.bg_color = 0xffffff00;  // RGBA
    cursorImage.depth = 32;
    cursorImage.data = (char*)base;

    cursor->image = cursorImage;

    if (!setCursor(fd)) {
        ALOGE("%s: Failed to setup HW cursor", __FUNCTION__);
        ret = false;
        memset(cursor, 0, sizeof(fb_cursor));
    }
    return ret;
}

bool HWCursor::setPositionAsync(const int fd, int x, int y) {
    bool ret = true;
    if (isCursorSet()) {
        fb_cursor *cursor = &mfbCursor;
        cursor->set = FB_CUR_SETPOS;
        cursor->image.dx = x;
        cursor->image.dy = y;
        if (!setCursor(fd)) {
            ALOGE("%s: Failed to set position x = %d y = %d", __FUNCTION__, x, y);
            ret = false;
        }
    }
    return ret;
}

bool HWCursor::free(const int fd) {
    fb_cursor *cursor = &mfbCursor;
    fb_image cursorImage;
    bool ret = true;

    if(!cursor->enable) {
        return ret;
    }

    cursor->enable = (uint16_t)0;

    if (!setCursor(fd)) {
        ALOGE("%s: Failed to free cursor hw", __FUNCTION__);
        ret = false;
    }
    memset(cursor, 0, sizeof(fb_cursor));
    return ret;
}

bool HWCursor::setCursor(const int fd) {
    bool ret = true;
    ATRACE_CALL();
    fb_cursor *cursor = &mfbCursor;

    if(fd <= 0) {
        ALOGE("%s: Invalid fd", fd);
        return false;
    }

    if (ioctl(fd, MSMFB_CURSOR, cursor) < 0) {
        ALOGE("%s: Failed to call ioctl MSMFB_CURSOR err=%s\n", __FUNCTION__,
              strerror(errno));
        ret = false;
    }
    return ret;
}

void HWCursor::getDump(char* buf, size_t len) {
      char cursordump[len];
      fb_cursor* cursor = &mfbCursor;
      if (cursor->enable) {
          snprintf(cursordump, sizeof(cursordump),
              "HWCursor on Primary: src w=%d h=%d\n"
              "\tsrc_rect x=%d y=%d w=%d h=%d\n"
              "\tdst_rect x=%d y=%d w=%d h=%d\n\n", cursor->image.width,
              cursor->image.height, cursor->hot.x, cursor->hot.y,
              cursor->image.width, cursor->image.height,
              cursor->image.dx, cursor->image.dy, cursor->image.width,
              cursor->image.height);
          strlcat(buf, cursordump, len);
      }

}

} //namespace overlay
