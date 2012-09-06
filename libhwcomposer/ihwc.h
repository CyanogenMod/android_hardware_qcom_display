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

#ifndef ANDROID_IHWCOMPOSER_H
#define ANDROID_IHWCOMPOSER_H

#include <stdint.h>
#include <sys/types.h>

#include <utils/Errors.h>
#include <utils/RefBase.h>

#include <binder/IInterface.h>


namespace hwcService {
// ----------------------------------------------------------------------------
enum {
    SET_EXT_HPD_ENABLE = 0,
    SET_EXT_DISPLAY_RESOLUTION_MODE,
    SET_EXT_DISPLAY_ACTIONSAFE_DIMENSIONS,
    SET_OPEN_SECURE_START,
    SET_OPEN_SECURE_END,
    SET_CLOSE_SECURE_START,
    SET_CLOSE_SECURE_END,
    GET_EXT_DISPLAY_TYPE,
    GET_EXT_DISPLAY_RESOLUTION_MODES,
    GET_EXT_DISPLAY_RESOLUTION_MODE_COUNT,
};

class IHWComposer : public android::IInterface
{
public:
    DECLARE_META_INTERFACE(HWComposer);

    virtual android::status_t getResolutionModeCount(int *modeCount) = 0;
    virtual android::status_t getResolutionModes(int *EDIDModes,
                                                 int count = 1) = 0;
    virtual android::status_t getExternalDisplay(int *extDisp) = 0;

    virtual android::status_t setHPDStatus(int enable) = 0;
    virtual android::status_t setResolutionMode(int resMode) = 0;
    virtual android::status_t setActionSafeDimension(int w, int h) = 0;
    // Secure Intent Hooks
    virtual android::status_t setOpenSecureStart() = 0;
    virtual android::status_t setOpenSecureEnd() = 0;
    virtual android::status_t setCloseSecureStart() = 0;
    virtual android::status_t setCloseSecureEnd() = 0;
};

// ----------------------------------------------------------------------------

class BnHWComposer : public android::BnInterface<IHWComposer>
{
public:
    virtual android::status_t onTransact( uint32_t code,
                                          const android::Parcel& data,
                                          android::Parcel* reply,
                                          uint32_t flags = 0);
};

// ----------------------------------------------------------------------------
}; // namespace hwcService

#endif // ANDROID_IHWCOMPOSER_H
