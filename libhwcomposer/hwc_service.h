/*
 *  Copyright (c) 2012, Code Aurora Forum. All rights reserved.
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
 *     * Neither the name of Code Aurora Forum, Inc. nor the names of its
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

#ifndef ANDROID_HWCOMPOSER_SERVICE_H
#define ANDROID_HWCOMPOSER_SERVICE_H

#include <utils/Errors.h>
#include <sys/types.h>
#include <cutils/log.h>
#include <binder/IServiceManager.h>
#include <ihwc.h>
#include <hwc_external.h>


namespace hwcService {
// ----------------------------------------------------------------------------

class HWComposerService : public BnHWComposer {
enum {
    MAX_ACTIONSAFE_WIDTH  = 10,
    MAX_ACTIONSAFE_HEIGHT = MAX_ACTIONSAFE_WIDTH,
};
private:
    HWComposerService();
public:
    ~HWComposerService();

    static HWComposerService* getInstance();
    virtual android::status_t getResolutionModeCount(int *modeCount);
    virtual android::status_t getResolutionModes(int *EDIDModes, int count = 1);
    virtual android::status_t getExternalDisplay(int *extDisp);

    virtual android::status_t setHPDStatus(int enable);
    virtual android::status_t setResolutionMode(int resMode);
    virtual android::status_t setActionSafeDimension(int w, int h);

    // Secure Intent Hooks
    virtual android::status_t setOpenSecureStart();
    virtual android::status_t setOpenSecureEnd();
    virtual android::status_t setCloseSecureStart();
    virtual android::status_t setCloseSecureEnd();
    void setHwcContext(hwc_context_t *hwcCtx);
private:
    static HWComposerService *sHwcService;
    hwc_context_t *mHwcContext;
};

}; // namespace hwcService
#endif // ANDROID_HWCOMPOSER_SERVICE_H
