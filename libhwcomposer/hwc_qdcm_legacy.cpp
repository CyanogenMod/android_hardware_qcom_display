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
#include <IQService.h>
#include <mdp_version.h>
#include "mode_manager.h"

using namespace android;
using namespace qService;
using namespace qhwc;

namespace qQdcm {
//----------------------------------------------------------------------------
void qdcmInitContext(hwc_context_t *ctx)
{
}

void qdcmCloseContext(hwc_context_t *ctx)
{
}

void qdcmApplyDefaultAfterBootAnimationDone(hwc_context_t *ctx)
{
    loadQdcmLibrary(ctx);
    if (ctx->mQdcmInfo.mQdcmMode)
        ctx->mQdcmInfo.mQdcmMode->applyDefaultMode(0);
    unloadQdcmLibrary(ctx);
}

//do nothing in case qdcm legacy implementation.
void qdcmCmdsHandler(hwc_context_t *ctx, const Parcel *in, Parcel *out)
{
}


} //namespace qQdcm

