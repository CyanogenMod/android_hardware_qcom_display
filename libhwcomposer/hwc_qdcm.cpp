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

#include <hwc_qdcm.h>
#include <hwc_utils.h>
#include <utils/String16.h>
#include <mdp_version.h>
#include "mode_manager.h"
#include "libmm-disp-apis.h"
#include "IQService.h"

using namespace android;
using namespace qService;
using namespace qhwc;
using namespace qmode;

namespace qQdcm {
//----------------------------------------------------------------------------
void qdcmInitContext(hwc_context_t *ctx)
{
    loadQdcmLibrary(ctx);
}

void qdcmCloseContext(hwc_context_t *ctx)
{
    if (ctx->mQdcmInfo.mQdcmMode) {
        unloadQdcmLibrary(ctx);
    }
}

void qdcmApplyDefaultAfterBootAnimationDone(hwc_context_t *ctx)
{
    if (ctx->mQdcmInfo.mQdcmMode)
        ctx->mQdcmInfo.mQdcmMode->applyDefaultMode(0);
}

static void qdcmSetActiveMode(hwc_context_t *ctx, const Parcel *in, Parcel *out)
{
    int ret = 0;

    if (ctx->mQdcmInfo.mQdcmMode && in && out) {

        struct SET_MODE_PROP_IN params =
                           { (disp_id_type)in->readInt32(), in->readInt32()};

        ret = ctx->mQdcmInfo.mQdcmMode->requestRoute((int)CMD_SET_ACTIVE_MODE,
                (void *)&params, (void *)NULL);

        out->writeInt32(ret);  //return operation status via binder.
    }
}

static void qdcmSetDefaultMode(hwc_context_t *ctx, const Parcel *in, Parcel *out)
{
    int ret = 0;

    if (ctx->mQdcmInfo.mQdcmMode && in && out) {

        struct SET_MODE_PROP_IN params =
                          { (disp_id_type)in->readInt32(), in->readInt32()};

        ret = ctx->mQdcmInfo.mQdcmMode->requestRoute((int)CMD_SET_DEFAULT_MODE,
                (void *)&params, (void *)NULL);

        out->writeInt32(ret);  //return operation status via binder.
    }
}

static void qdcmGetDefaultMode(hwc_context_t *ctx,
                                            const Parcel *in, Parcel *out)
{
    int ret = 0;

    if (ctx->mQdcmInfo.mQdcmMode && in && out) {

        int params = in->readInt32();
        int modeid = 0;

        ret = ctx->mQdcmInfo.mQdcmMode->requestRoute((int)CMD_GET_DEFAULT_MODE,
                (const void *)&params, (void *)&modeid);

        out->writeInt32(modeid);
        out->writeInt32(ret);  //return operation status via binder.
    }
}

static void qdcmGetColorBalanceRange(hwc_context_t *ctx __unused,
                const Parcel *in __unused, Parcel *out __unused)
{
}

static void qdcmGetColorBalance(hwc_context_t *ctx,
                                            const Parcel *in, Parcel *out)
{
    int ret = 0;

    if (ctx->mQdcmInfo.mQdcmMode && in && out) {

        int params = in->readInt32();
        int warmness = 0;

        ret = ctx->mQdcmInfo.mQdcmMode->requestRoute((int)CMD_GET_CB,
                (const void *)&params, (void *)&warmness);

        out->writeInt32(warmness);
        out->writeInt32(ret);  //return operation status via binder.
    }
}

static void qdcmSetColorBalance(hwc_context_t *ctx,
                                            const Parcel *in, Parcel *out)
{
    int ret = 0;

    if (ctx->mQdcmInfo.mQdcmMode && in && out) {

        struct SET_CB_IN params =
                           { (disp_id_type)in->readInt32(), in->readInt32() };

        ALOGD_IF(QDCM_DEBUG, "%s dispID = %d, warmness = %d\n",
                __FUNCTION__, params.id, params.warmness);

        ret = ctx->mQdcmInfo.mQdcmMode->requestRoute((int)CMD_SET_CB,
                (const void *)&params, NULL);

        out->writeInt32(ret);  //return operation status via binder.
    }
}

static void qdcmSaveModeV2(hwc_context_t *ctx, const Parcel *in, Parcel *out)
{
    int ret = 0;

    if (ctx->mQdcmInfo.mQdcmMode && in && out) {

        struct SAVE_DISPLAY_MODE_V2_IN params =
                     { (disp_id_type)in->readInt32(),
                                     in->readCString(),
                           (uint32_t)in->readInt32(),
                                     in->readInt32()
                     };
        int value = 0;

        ret = ctx->mQdcmInfo.mQdcmMode->requestRoute((int)CMD_SAVE_MODE_V2,
                (const void *)&params, (void *)&value);

        out->writeInt32(value);
        out->writeInt32(ret);  //return operation status via binder.
    }
}

static void qdcmSetPaConfig(hwc_context_t *ctx, const Parcel *in, Parcel *out)
{
    int ret = 0;

    if (ctx->mQdcmInfo.mQdcmMode && in && out) {

        struct SET_PA_CONFIG_IN params;

        params.id = (disp_id_type)in->readInt32();
        params.pa.ops = in->readInt32();
        params.pa.data.hue = in->readInt32();
        params.pa.data.saturation = in->readInt32();
        params.pa.data.value = in->readInt32();
        params.pa.data.contrast = in->readInt32();
        params.pa.data.sat_thresh = in->readInt32();

        ret = ctx->mQdcmInfo.mQdcmMode->requestRoute((int)CMD_SET_PA_CONFIG,
                (const void *)&params, NULL);

        out->writeInt32(ret);  //return operation status via binder.
    }
}

static void qdcmGetPaConfig(hwc_context_t *ctx, const Parcel *in, Parcel *out)
{
    int ret = 0;

    if (ctx->mQdcmInfo.mQdcmMode && in && out) {

        int params = in->readInt32();
        struct disp_pa_config value;

        ret = ctx->mQdcmInfo.mQdcmMode->requestRoute((int)CMD_GET_PA_CONFIG,
                (const void *)&params, (void *)&value);

        out->writeInt32(value.ops);
        out->writeInt32(value.data.hue);
        out->writeInt32(value.data.saturation);
        out->writeInt32(value.data.value);
        out->writeInt32(value.data.contrast);
        out->writeInt32(value.data.sat_thresh);

        out->writeInt32(ret);  //return operation status via binder.
    }
}

static void qdcmGetPaRange(hwc_context_t *ctx, const Parcel *in, Parcel *out)
{
    int ret = 0;

    if (ctx->mQdcmInfo.mQdcmMode && in && out) {

        int params = in->readInt32();
        struct disp_pa_range value;

        ret = ctx->mQdcmInfo.mQdcmMode->requestRoute((int)CMD_GET_PA_RANGE,
                (const void *)&params, (void *)&value);

        out->writeInt32(value.max.hue);
        out->writeInt32(value.max.saturation);
        out->writeInt32(value.max.value);
        out->writeInt32(value.max.contrast);
        out->writeInt32(value.max.sat_thresh);
        out->writeInt32(value.min.hue);
        out->writeInt32(value.min.saturation);
        out->writeInt32(value.min.value);
        out->writeInt32(value.min.contrast);
        out->writeInt32(value.min.sat_thresh);

        out->writeInt32(ret);  //return operation status via binder.
    }
}

void qdcmCmdsHandler(hwc_context_t *ctx, const Parcel *in, Parcel *out)
{
    int subcmd = in->readInt32();

    ALOGD_IF(QDCM_DEBUG, "%s enter subcmd = %d\n", __FUNCTION__, subcmd);
    switch (subcmd) {
        case CMD_SET_ACTIVE_MODE:
            qdcmSetActiveMode(ctx, in, out);
            break;
        case CMD_SET_DEFAULT_MODE:
            qdcmSetDefaultMode(ctx, in, out);
            break;
        case CMD_GET_DEFAULT_MODE:
            qdcmGetDefaultMode(ctx, in, out);
            break;
        case CMD_GET_CB_RANGE:
            qdcmGetColorBalanceRange(ctx, in, out);
            break;
        case CMD_GET_CB:
            qdcmGetColorBalance(ctx, in, out);
            break;
        case CMD_SET_CB:
            qdcmSetColorBalance(ctx, in, out);
            break;
        case CMD_SAVE_MODE_V2:
            qdcmSaveModeV2(ctx, in, out);
            break;
        case CMD_SET_PA_CONFIG:
            qdcmSetPaConfig(ctx, in, out);
            break;
        case CMD_GET_PA_CONFIG:
            qdcmGetPaConfig(ctx, in, out);
            break;
        case CMD_GET_PA_RANGE:
            qdcmGetPaRange(ctx, in, out);
            break;
    }
}


} //namespace qQdcm

