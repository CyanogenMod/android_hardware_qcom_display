/*
* Copyright (c) 2014, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __HWC_LOGGER_H__
#define __HWC_LOGGER_H__

#include <cutils/log.h>

#ifndef HWC_MODULE_NAME
#define HWC_MODULE_NAME "HWComposer"
#endif

#define HWC_LOG(Macro, format, ...) Macro(HWC_MODULE_NAME ": " format, ##__VA_ARGS__)

// HWC_MODULE_NAME must be defined before #include this header file in respective
// module, else default definition is used.
#define DLOGE(format, ...) HWC_LOG(ALOGE, format, ##__VA_ARGS__)
#define DLOGW(format, ...) HWC_LOG(ALOGW, format, ##__VA_ARGS__)
#define DLOGI(format, ...) HWC_LOG(ALOGI, format, ##__VA_ARGS__)
#define DLOGV(format, ...) HWC_LOG(ALOGV, format, ##__VA_ARGS__)

#endif  // __HWC_LOGGER_H__

