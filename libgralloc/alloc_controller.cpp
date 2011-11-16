/*
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#include <cutils/log.h>
#include <utils/RefBase.h>
#include "gralloc_priv.h"
#include "alloc_controller.h"
#include "memalloc.h"
#include "ionalloc.h"

using namespace gralloc;
using android::sp;

sp<IAllocController> IAllocController::sController = NULL;
sp<IAllocController> IAllocController::getInstance(void)
{
    if(sController == NULL) {
#ifdef USE_ION
        sController = new IonController();
#else
        // XXX: Return pmem/ashmem controller when completed
#endif
    }
    return sController;
}

IonController::IonController()
{
    mIonAlloc = new IonAlloc();
}

static bool canFallback(int compositionType, int usage, int flags)
{
    // Fallback to system heap when alloc fails unless
    // 1. Composition type is MDP
    // 2. Earlier alloc attempt was from system heap
    // 3. Contiguous heap requsted explicitly

    if(compositionType == MDP_COMPOSITION)
        return false;
    if(flags & ION_HEAP_SYSTEM_ID)
        return false;
    if(usage &(GRALLOC_USAGE_PRIVATE_ADSP_HEAP|
               GRALLOC_USAGE_PRIVATE_EBI_HEAP |
               GRALLOC_USAGE_PRIVATE_SMI_HEAP))
        return false;
    //Return true by default
    return true;
}

int IonController::allocate(alloc_data& data, int usage,
        int compositionType)
{
    int ionFlags = 0;
    int ret;

    //System heap cannot be uncached
    if (usage & GRALLOC_USAGE_PRIVATE_UNCACHED &&
            !(usage & GRALLOC_USAGE_PRIVATE_SYSTEM_HEAP))
        data.uncached = true;
    else
        data.uncached = false;

    if(usage & GRALLOC_USAGE_PRIVATE_ADSP_HEAP)
        ionFlags |= 1 << ION_HEAP_ADSP_ID;

    if(usage & GRALLOC_USAGE_PRIVATE_SMI_HEAP)
        ionFlags |= 1 << ION_HEAP_SMI_ID;

    if(usage & GRALLOC_USAGE_PRIVATE_EBI_HEAP)
        ionFlags |= 1 << ION_HEAP_EBI_ID;

    if(usage & GRALLOC_USAGE_PRIVATE_SYSTEM_HEAP)
        ionFlags |= 1 << ION_HEAP_SYSTEM_ID;

    // if no flags are set, default to
    // EBI heap, so that bypass can work
    // we can fall back to system heap if
    // we run out.
    if(!ionFlags)
        ionFlags = 1 << ION_HEAP_EBI_ID;

    data.flags = ionFlags;
    ret = mIonAlloc->alloc_buffer(data);

    // Fallback
    if(ret < 0 && canFallback(compositionType, usage, ionFlags)) {
        LOGW("Falling back to system heap");
        data.flags = 1 << ION_HEAP_SYSTEM_ID;
        ret = mIonAlloc->alloc_buffer(data);
    }

    if(ret >= 0 )
        data.allocType = private_handle_t::PRIV_FLAGS_USES_ION;

    return ret;
}

sp<IMemAlloc> IonController::getAllocator(int flags)
{
    sp<IMemAlloc> memalloc;
    if (flags & private_handle_t::PRIV_FLAGS_USES_ION) {
        memalloc = mIonAlloc;
    } else {
        LOGE("%s: Invalid flags passed: 0x%x", __FUNCTION__, flags);
    }

    return memalloc;
}

int PmemAshmemController::allocate(alloc_data& data, int usage,
        int compositionType)
{
    //XXX PMEM with ashmem fallback strategy
    return 0;
}

sp<IMemAlloc> PmemAshmemController::getAllocator(int flags)
{
    sp<IMemAlloc> memalloc;
    if (flags & private_handle_t::PRIV_FLAGS_USES_PMEM) {
        // XXX Return right allocator based on flags
        memalloc = NULL;
    } else {
        LOGE("%s: Invalid flags passed: 0x%x", __FUNCTION__, flags);
        memalloc = NULL;
    }

    return memalloc;
}


