/*
* Copyright (c) 2013 The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

#ifndef HWC_VPU_H
#define HWC_VPU_H

#include <sys/types.h>

//Forward declarations
struct hwc_display_contents_1;
typedef struct hwc_display_contents_1 hwc_display_contents_1_t;
struct hwc_context_t;
namespace vpu {
class VPU;
};
namespace android {
class Parcel;
};

namespace qhwc {

class VPUClient {
public:
    VPUClient();

    ~VPUClient();

    int prepare(hwc_context_t *ctx, hwc_display_contents_1_t* list);

    int draw(hwc_context_t *ctx, hwc_display_contents_1_t* list);

    int processCommand(uint32_t command,
            const android::Parcel* inParcel, android::Parcel* outParcel);

private:
    vpu::VPU *mVPU;
    void* mVPULib;

}; // class VPU
}; // namespace qhwc
#endif /* end of include guard: HWC_VPU_H */
