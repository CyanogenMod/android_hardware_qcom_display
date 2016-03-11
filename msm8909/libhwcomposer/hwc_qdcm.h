/*
 *  Copyright (c) 2014, The Linux Foundation. All rights reserved.
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

#ifndef ANDROID_QDCM_H
#define ANDROID_QDCM_H

#include <utils/Errors.h>
#include <sys/types.h>
#include <cutils/log.h>
#include <hwc_utils.h>
#include <dlfcn.h>
#include <binder/Parcel.h>
#include <cutils/properties.h>

#define QDCM_DEBUG 0

namespace qmode {
class ModeManager;
}

using namespace android;

namespace qQdcm {
// ----------------------------------------------------------------------------

//function prototypes used for QDCM library and service
static inline void loadQdcmLibrary(hwc_context_t *ctx)
{
    ctx->mQdcmInfo.mQdcmLib = dlopen("libmm-qdcm.so", RTLD_NOW);
    qmode::ModeManager* (*factory)() = NULL;

    if (ctx->mQdcmInfo.mQdcmLib)
        *(void **)&factory = dlsym(ctx->mQdcmInfo.mQdcmLib, "getObject");

    if (factory) {
        ctx->mQdcmInfo.mQdcmMode = factory();
    } else {
        ctx->mQdcmInfo.mQdcmMode = NULL;
        ALOGE("QDCM LIbrary load failing!");
    }

    ALOGD_IF(QDCM_DEBUG, "QDCM LIbrary loaded successfully!");
}

static inline void unloadQdcmLibrary(hwc_context_t *ctx)
{
    void (*destroy)(qmode::ModeManager*) = NULL;

    if (ctx->mQdcmInfo.mQdcmLib) {
        *(void **)&destroy = dlsym(ctx->mQdcmInfo.mQdcmLib, "deleteObject");

        if (destroy) {
            destroy(ctx->mQdcmInfo.mQdcmMode);
            ctx->mQdcmInfo.mQdcmMode = NULL;
        }

        dlclose(ctx->mQdcmInfo.mQdcmLib);
        ctx->mQdcmInfo.mQdcmLib = NULL;
    }
}

void qdcmInitContext(hwc_context_t *);
void qdcmCloseContext(hwc_context_t *);
void qdcmApplyDefaultAfterBootAnimationDone(hwc_context_t *);
void qdcmCmdsHandler(hwc_context_t*, const Parcel*, Parcel*);

}; // namespace qQdcm
#endif // ANDROID_QDCM_H
