/*
* Copyright (c) 2014 The Linux Foundation. All rights reserved.
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

#define DEBUG 0
#include <QServiceUtils.h>
#include "QHDMIClient.h"

using namespace android;
using namespace qhdmicec;
using namespace qService;

namespace qClient {

void QHDMIClient::binderDied(const wp<IBinder>& who __unused)
{
    ALOGW("%s: Display QService died", __FUNCTION__);
}

void QHDMIClient::onHdmiHotplug(int connected)
{
    ALOGD("%s: HDMI connected event connected: %d", __FUNCTION__, connected);
    cec_hdmi_hotplug(mCtx, connected);
}

void QHDMIClient::onCECMessageRecieved(char *msg, ssize_t len)
{
    ALOGD_IF(DEBUG, "%s: CEC message received len: %zd", __FUNCTION__, len);
    cec_receive_message(mCtx, msg, len);
}

void QHDMIClient::registerClient(sp<QHDMIClient>& client)
{
    sp<IServiceManager> sm = defaultServiceManager();
    sp<IBinder> binder = sm->getService(String16("display.qservice"));
    binder->linkToDeath(client);
    mQService = interface_cast<IQService>(binder);
    mQService->connect(interface_cast<IQHDMIClient>(client));
}

};
