/*
* Copyright (c) 2014 - 2015, The Linux Foundation. All rights reserved.
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

#include <utils/constants.h>

#include "hwc_debugger.h"

namespace sde {

HWCDebugHandler HWCDebugHandler::debug_handler_;
uint32_t HWCDebugHandler::debug_flags_ = 0x1;

void HWCDebugHandler::DebugAll(bool enable) {
  if (enable) {
    debug_flags_ = 0xFFFFFFFF;
  } else {
    debug_flags_ = 0x1;   // kTagNone should always be printed.
  }
}

void HWCDebugHandler::DebugResources(bool enable) {
  if (enable) {
    SET_BIT(debug_flags_, kTagResources);
  } else {
    CLEAR_BIT(debug_flags_, kTagResources);
  }
}

void HWCDebugHandler::DebugStrategy(bool enable) {
  if (enable) {
    SET_BIT(debug_flags_, kTagStrategy);
  } else {
    CLEAR_BIT(debug_flags_, kTagStrategy);
  }
}

void HWCDebugHandler::DebugCompManager(bool enable) {
  if (enable) {
    SET_BIT(debug_flags_, kTagCompManager);
  } else {
    CLEAR_BIT(debug_flags_, kTagCompManager);
  }
}

void HWCDebugHandler::DebugDriverConfig(bool enable) {
  if (enable) {
    SET_BIT(debug_flags_, kTagDriverConfig);
  } else {
    CLEAR_BIT(debug_flags_, kTagDriverConfig);
  }
}

void HWCDebugHandler::DebugBufferManager(bool enable) {
  if (enable) {
    SET_BIT(debug_flags_, kTagBufferManager);
  } else {
    CLEAR_BIT(debug_flags_, kTagBufferManager);
  }
}

void HWCDebugHandler::DebugOfflineCtrl(bool enable) {
  if (enable) {
    SET_BIT(debug_flags_, kTagOfflineCtrl);
  } else {
    CLEAR_BIT(debug_flags_, kTagOfflineCtrl);
  }
}

void HWCDebugHandler::Error(DebugTag /*tag*/, const char *format, ...) {
  va_list list;
  va_start(list, format);
  __android_log_vprint(ANDROID_LOG_ERROR, LOG_TAG, format, list);
}

void HWCDebugHandler::Warning(DebugTag /*tag*/, const char *format, ...) {
  va_list list;
  va_start(list, format);
  __android_log_vprint(ANDROID_LOG_WARN, LOG_TAG, format, list);
}

void HWCDebugHandler::Info(DebugTag tag, const char *format, ...) {
  if (IS_BIT_SET(debug_flags_, tag)) {
    va_list list;
    va_start(list, format);
    __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, format, list);
  }
}

void HWCDebugHandler::Verbose(DebugTag tag, const char *format, ...) {
  if (IS_BIT_SET(debug_flags_, tag)) {
    va_list list;
    va_start(list, format);
    __android_log_vprint(ANDROID_LOG_VERBOSE, LOG_TAG, format, list);
  }
}

void HWCDebugHandler::BeginTrace(const char *class_name, const char *function_name,
                                 const char *custom_string) {
  char name[PATH_MAX] = {0};
  snprintf(name, sizeof(name), "%s::%s::%s", class_name, function_name, custom_string);
  atrace_begin(ATRACE_TAG, name);
}

void HWCDebugHandler::EndTrace() {
  atrace_end(ATRACE_TAG);
}

}  // namespace sde

