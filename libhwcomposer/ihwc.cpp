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

#include <stdint.h>
#include <sys/types.h>
#include <utils/Errors.h>

#include <binder/Parcel.h>
#include <binder/IBinder.h>
#include <binder/IInterface.h>
#include <ihwc.h>

using namespace android;

// ---------------------------------------------------------------------------

namespace hwcService {

class BpHWComposer : public BpInterface<IHWComposer>
{
public:
    BpHWComposer(const sp<IBinder>& impl)
        : BpInterface<IHWComposer>(impl)
    {
    }

    virtual status_t setHPDStatus(int hpdStatus) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(hpdStatus);
        status_t result = remote()->transact(SET_EXT_HPD_ENABLE,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t setResolutionMode(int resMode) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(resMode);
        status_t result = remote()->transact(SET_EXT_DISPLAY_RESOLUTION_MODE,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t setActionSafeDimension(int w, int h) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(w);
        data.writeInt32(h);
        status_t result =
            remote()->transact(SET_EXT_DISPLAY_ACTIONSAFE_DIMENSIONS,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }
    virtual status_t setOpenSecureStart() {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(SET_OPEN_SECURE_START,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t setOpenSecureEnd() {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(SET_OPEN_SECURE_END,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t setCloseSecureStart() {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(SET_CLOSE_SECURE_START,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }
    virtual status_t setCloseSecureEnd() {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(SET_CLOSE_SECURE_END,
                                             data, &reply);
        result = reply.readInt32();
        return result;
    }

    virtual status_t getExternalDisplay(int *extDispType) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(GET_EXT_DISPLAY_TYPE,
                                             data, &reply);
        *extDispType = reply.readInt32();
        result = reply.readInt32();
        return result;
    }

    virtual status_t getResolutionModes(int *resModes, int count) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        data.writeInt32(count);
        status_t result = remote()->transact(GET_EXT_DISPLAY_RESOLUTION_MODES,
                                             data, &reply);
        for(int i = 0;i < count;i++) {
            resModes[i] = reply.readInt32();
        }
        result = reply.readInt32();
        return result;
    }

    virtual status_t getResolutionModeCount(int *resModeCount) {
        Parcel data, reply;
        data.writeInterfaceToken(IHWComposer::getInterfaceDescriptor());
        status_t result = remote()->transact(
                          GET_EXT_DISPLAY_RESOLUTION_MODE_COUNT, data, &reply);
        *resModeCount = reply.readInt32();
        result = reply.readInt32();
        return result;
    }
};

IMPLEMENT_META_INTERFACE(HWComposer, "android.display.IHWComposer");

// ----------------------------------------------------------------------

status_t BnHWComposer::onTransact(
    uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
    // codes that don't require permission check
    switch(code) {
        case SET_EXT_HPD_ENABLE: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            int hpdStatus = data.readInt32();
            status_t res = setHPDStatus(hpdStatus);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_EXT_DISPLAY_RESOLUTION_MODE: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            int resMode = data.readInt32();
            status_t res = setResolutionMode(resMode);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_EXT_DISPLAY_ACTIONSAFE_DIMENSIONS: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            int w = data.readInt32();
            int h = data.readInt32();
            status_t res = setActionSafeDimension(w, h);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case SET_OPEN_SECURE_START: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            status_t res = setOpenSecureStart();
            reply->writeInt32(res);
            return NO_ERROR;
        }break;
        case SET_OPEN_SECURE_END: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            status_t res = setOpenSecureEnd();
            reply->writeInt32(res);
            return NO_ERROR;
        }break;
        case SET_CLOSE_SECURE_START: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            status_t res = setCloseSecureStart();
            reply->writeInt32(res);
            return NO_ERROR;
        }break;
        case SET_CLOSE_SECURE_END: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            status_t res = setCloseSecureEnd();
            reply->writeInt32(res);
            return NO_ERROR;
        }break;
        case GET_EXT_DISPLAY_TYPE: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            int extDispType;
            status_t res = getExternalDisplay(&extDispType);
            reply->writeInt32(extDispType);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case GET_EXT_DISPLAY_RESOLUTION_MODES: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            int count = data.readInt32();
            int resModes[64];
            status_t res = getResolutionModes(&resModes[0]);
            for(int i = 0;i < count;i++) {
                reply->writeInt32(resModes[i]);
            }
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        case GET_EXT_DISPLAY_RESOLUTION_MODE_COUNT: {
            CHECK_INTERFACE(IHWComposer, data, reply);
            int resModeCount;
            status_t res = getResolutionModeCount(&resModeCount);
            reply->writeInt32(resModeCount);
            reply->writeInt32(res);
            return NO_ERROR;
        } break;
        default:
            return BBinder::onTransact(code, data, reply, flags);
    }
}

}; // namespace hwcService
