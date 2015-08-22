/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2014, The Linux Foundation. All rights reserved.
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

#ifndef ANDROID_IQSERVICE_H
#define ANDROID_IQSERVICE_H

#include <stdint.h>
#include <sys/types.h>
#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <binder/IInterface.h>
#include <binder/IBinder.h>
#include <IQClient.h>


namespace qService {
// ----------------------------------------------------------------------------

class IQService : public android::IInterface
{
public:
    DECLARE_META_INTERFACE(QService);
    enum {
        COMMAND_LIST_START = android::IBinder::FIRST_CALL_TRANSACTION,
        SECURING = 2,           // Hardware securing start/end notification
        UNSECURING = 3,         // Hardware unsecuring start/end notification
        CONNECT = 4,            // Connect to qservice
        SCREEN_REFRESH = 5,     // Refresh screen through SF invalidate
        EXTERNAL_ORIENTATION = 6,// Set external orientation
        BUFFER_MIRRORMODE = 7,  // Buffer mirrormode
        CHECK_EXTERNAL_STATUS = 8,// Check status of external display
        GET_DISPLAY_ATTRIBUTES = 9,// Get display attributes
        SET_HSIC_DATA = 10,     // Set HSIC on dspp
        GET_DISPLAY_VISIBLE_REGION = 11,// Get the visibleRegion for dpy
        SET_SECONDARY_DISPLAY_STATUS = 12,// Sets secondary display status
        SET_MAX_PIPES_PER_MIXER = 13,// Set max pipes per mixer for MDPComp
        SET_VIEW_FRAME = 14,    // Set view frame of display
        DYNAMIC_DEBUG = 15,     // Enable more logging on the fly
        SET_IDLE_TIMEOUT = 16,  // Set idle timeout for GPU fallback
        TOGGLE_BWC = 17,           // Toggle BWC On/Off on targets that support
        /* Enable/Disable/Set refresh rate dynamically */
        CONFIGURE_DYN_REFRESH_RATE = 18,
        SET_PARTIAL_UPDATE = 19,   // Preference on partial update feature
        TOGGLE_SCREEN_UPDATE = 20, // Provides ability to disable screen updates
        APPLY_MODE_BY_ID = 40, //Apply display mode by ID
        COMMAND_LIST_END = 400,
    };

    enum {
        END = 0,
        START,
    };

    enum {
        DEBUG_ALL,
        DEBUG_MDPCOMP,
        DEBUG_VSYNC,
        DEBUG_VD,
        DEBUG_PIPE_LIFECYCLE,
    };

    // Register a client that can be notified
    virtual void connect(const android::sp<qClient::IQClient>& client) = 0;
    // Generic function to dispatch binder commands
    // The type of command decides how the data is parceled
    virtual android::status_t dispatch(uint32_t command,
            const android::Parcel* inParcel,
            android::Parcel* outParcel) = 0;
};

// ----------------------------------------------------------------------------

class BnQService : public android::BnInterface<IQService>
{
public:
    virtual android::status_t onTransact( uint32_t code,
                                          const android::Parcel& data,
                                          android::Parcel* reply,
                                          uint32_t flags = 0);
};

// ----------------------------------------------------------------------------
}; // namespace qService

#endif // ANDROID_IQSERVICE_H
