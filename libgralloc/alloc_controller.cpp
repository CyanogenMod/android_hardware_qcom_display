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
#include <fcntl.h>
#include "gralloc_priv.h"
#include "alloc_controller.h"
#include "memalloc.h"
#include "ionalloc.h"
#include "pmemalloc.h"
#include "ashmemalloc.h"

using namespace gralloc;
using android::sp;

const int GRALLOC_HEAP_MASK = GRALLOC_USAGE_PRIVATE_ADSP_HEAP      |
                              GRALLOC_USAGE_PRIVATE_UI_CONTIG_HEAP |
                              GRALLOC_USAGE_PRIVATE_SMI_HEAP       |
                              GRALLOC_USAGE_PRIVATE_SYSTEM_HEAP    |
                              GRALLOC_USAGE_PRIVATE_IOMMU_HEAP     |
                              GRALLOC_USAGE_PRIVATE_MM_HEAP        |
                              GRALLOC_USAGE_PRIVATE_WRITEBACK_HEAP |
                              GRALLOC_USAGE_PRIVATE_CAMERA_HEAP;


//Common functions
static bool canFallback(int compositionType, int usage, bool triedSystem)
{
    // Fallback to system heap when alloc fails unless
    // 1. Composition type is MDP
    // 2. Alloc from system heap was already tried
    // 3. The heap type is requsted explicitly
    // 4. The heap type is protected

    if(compositionType == MDP_COMPOSITION)
        return false;
    if(triedSystem)
        return false;
    if(usage & (GRALLOC_HEAP_MASK | GRALLOC_USAGE_PROTECTED))
        return false;
    //Return true by default
    return true;
}

static bool useUncached(int usage)
{
    // System heaps cannot be uncached
    if(usage & GRALLOC_USAGE_PRIVATE_SYSTEM_HEAP)
        return false;
    if (usage & GRALLOC_USAGE_PRIVATE_UNCACHED)
        return true;
    return false;
}

sp<IAllocController> IAllocController::sController = NULL;
sp<IAllocController> IAllocController::getInstance(bool useMasterHeap)
{
    if(sController == NULL) {
#ifdef USE_ION
        sController = new IonController();
#else
        if(useMasterHeap)
            sController = new PmemAshmemController();
        else
            sController = new PmemKernelController();
#endif
    }
    return sController;
}

#ifdef USE_ION

//-------------- IonController-----------------------//
IonController::IonController()
{
    mIonAlloc = new IonAlloc();
}

int IonController::allocate(alloc_data& data, int usage,
        int compositionType)
{
    int ionFlags = 0;
    int ret;
    bool noncontig = false;

    data.uncached = useUncached(usage);

    if(usage & GRALLOC_USAGE_PRIVATE_UI_CONTIG_HEAP)
        ionFlags |= ION_HEAP(ION_SF_HEAP_ID);

    if(usage & GRALLOC_USAGE_PRIVATE_SYSTEM_HEAP) {
        ionFlags |= ION_HEAP(ION_SYSTEM_HEAP_ID);
        noncontig = true;
    }

    if(usage & GRALLOC_USAGE_PRIVATE_IOMMU_HEAP)
        ionFlags |= ION_HEAP(ION_IOMMU_HEAP_ID);

    if(usage & GRALLOC_USAGE_PRIVATE_MM_HEAP)
        ionFlags |= ION_HEAP(ION_CP_MM_HEAP_ID);

    if(usage & GRALLOC_USAGE_PRIVATE_WRITEBACK_HEAP)
        ionFlags |= ION_HEAP(ION_CP_WB_HEAP_ID);

    if(usage & GRALLOC_USAGE_PRIVATE_CAMERA_HEAP)
        ionFlags |= ION_HEAP(ION_CAMERA_HEAP_ID);

    if(usage & GRALLOC_USAGE_PROTECTED)
        ionFlags |= ION_SECURE;

    // if no flags are set, default to
    // EBI heap, so that bypass can work
    // we can fall back to system heap if
    // we run out.
    if(!ionFlags)
        ionFlags = ION_HEAP(ION_SF_HEAP_ID);

    data.flags = ionFlags;
    ret = mIonAlloc->alloc_buffer(data);
    // Fallback
    if(ret < 0 && canFallback(compositionType,
                              usage,
                              (ionFlags & ION_SYSTEM_HEAP_ID)))
    {
        LOGW("Falling back to system heap");
        data.flags = ION_HEAP(ION_SYSTEM_HEAP_ID);
        noncontig = true;
        ret = mIonAlloc->alloc_buffer(data);
    }

    if(ret >= 0 ) {
        data.allocType = private_handle_t::PRIV_FLAGS_USES_ION;
        if(noncontig)
            data.allocType |= private_handle_t::PRIV_FLAGS_NONCONTIGUOUS_MEM;
        if(ionFlags & ION_SECURE)
            data.allocType |= private_handle_t::PRIV_FLAGS_SECURE_BUFFER;
    }


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
#endif

//-------------- PmemKernelController-----------------------//

PmemKernelController::PmemKernelController()
{
     mPmemAdspAlloc = new PmemKernelAlloc(DEVICE_PMEM_ADSP);
     // XXX: Right now, there is no need to maintain an instance
     // of the SMI allocator as we need it only in a few cases
}

PmemKernelController::~PmemKernelController()
{
}

int PmemKernelController::allocate(alloc_data& data, int usage,
        int compositionType)
{
    int ret = 0;
    bool adspFallback = false;
    if (!(usage & GRALLOC_USAGE_PRIVATE_SMI_HEAP))
        adspFallback = true;

    // Try SMI first
    if ((usage & GRALLOC_USAGE_PRIVATE_SMI_HEAP) ||
        (usage & GRALLOC_USAGE_EXTERNAL_DISP)    ||
        (usage & GRALLOC_USAGE_PROTECTED))
    {
        int tempFd = open(DEVICE_PMEM_SMIPOOL, O_RDWR, 0);
        if(tempFd > 0) {
            close(tempFd);
            sp<IMemAlloc> memalloc;
            memalloc = new PmemKernelAlloc(DEVICE_PMEM_SMIPOOL);
            ret = memalloc->alloc_buffer(data);
            if(ret >= 0)
                return ret;
            else {
                if(adspFallback)
                    LOGW("Allocation from SMI failed, trying ADSP");
            }
        }
    }

    if ((usage & GRALLOC_USAGE_PRIVATE_ADSP_HEAP) || adspFallback) {
        ret = mPmemAdspAlloc->alloc_buffer(data);
    }
    return ret;
}

sp<IMemAlloc> PmemKernelController::getAllocator(int flags)
{
    sp<IMemAlloc> memalloc;
    if (flags & private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP)
        memalloc = mPmemAdspAlloc;
    else {
        LOGE("%s: Invalid flags passed: 0x%x", __FUNCTION__, flags);
        memalloc = NULL;
    }

    return memalloc;
}

//-------------- PmemAshmmemController-----------------------//

PmemAshmemController::PmemAshmemController()
{
    mPmemUserspaceAlloc = new PmemUserspaceAlloc();
    mAshmemAlloc = new AshmemAlloc();
    mPmemKernelCtrl = new PmemKernelController();
}

PmemAshmemController::~PmemAshmemController()
{
}

int PmemAshmemController::allocate(alloc_data& data, int usage,
        int compositionType)
{
    int ret = 0;

    // Make buffers cacheable by default
        data.uncached = false;

    // Override if we explicitly need uncached buffers
    if (usage & GRALLOC_USAGE_PRIVATE_UNCACHED)
        data.uncached = true;

    // If ADSP or SMI is requested use the kernel controller
    if(usage & (GRALLOC_USAGE_PRIVATE_ADSP_HEAP|
                GRALLOC_USAGE_PRIVATE_SMI_HEAP)) {
        ret = mPmemKernelCtrl->allocate(data, usage, compositionType);
        if(ret < 0)
            LOGE("%s: Failed to allocate ADSP/SMI memory", __func__);
        else
            data.allocType = private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP;
        return ret;
    }

    if(usage & GRALLOC_USAGE_PRIVATE_SYSTEM_HEAP) {
        ret = mAshmemAlloc->alloc_buffer(data);
        if(ret >= 0) {
            data.allocType = private_handle_t::PRIV_FLAGS_USES_ASHMEM;
            data.allocType |= private_handle_t::PRIV_FLAGS_NONCONTIGUOUS_MEM;
        }
        return ret;
    }

    // if no memory specific flags are set,
    // default to EBI heap, so that bypass
    // can work. We can fall back to system
    // heap if we run out.
    ret = mPmemUserspaceAlloc->alloc_buffer(data);

    // Fallback
    if(ret >= 0 ) {
        data.allocType = private_handle_t::PRIV_FLAGS_USES_PMEM;
    } else if(ret < 0 && canFallback(compositionType, usage, false)) {
        LOGW("Falling back to ashmem");
        ret = mAshmemAlloc->alloc_buffer(data);
        if(ret >= 0) {
            data.allocType = private_handle_t::PRIV_FLAGS_USES_ASHMEM;
            data.allocType |= private_handle_t::PRIV_FLAGS_NONCONTIGUOUS_MEM;
        }
    }

    return ret;
}

sp<IMemAlloc> PmemAshmemController::getAllocator(int flags)
{
    sp<IMemAlloc> memalloc;
    if (flags & private_handle_t::PRIV_FLAGS_USES_PMEM)
        memalloc = mPmemUserspaceAlloc;
    else if (flags & private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP)
        memalloc = mPmemKernelCtrl->getAllocator(flags);
    else if (flags & private_handle_t::PRIV_FLAGS_USES_ASHMEM)
        memalloc = mAshmemAlloc;
    else {
        LOGE("%s: Invalid flags passed: 0x%x", __FUNCTION__, flags);
        memalloc = NULL;
    }

    return memalloc;
}


