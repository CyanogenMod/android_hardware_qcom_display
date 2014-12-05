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

#include <utils/constants.h>

#include "hwc_logger.h"

namespace sde {

HWCLogHandler HWCLogHandler::log_handler_;
uint32_t HWCLogHandler::log_flags_ = 0x1;

void HWCLogHandler::LogAll(bool enable) {
  if (enable) {
    log_flags_ = 0xFFFFFFFF;
  } else {
    log_flags_ = 0x1;   // kTagNone should always be printed.
  }
}

void HWCLogHandler::LogResources(bool enable) {
  if (enable) {
    log_flags_ = SET_BIT(log_flags_, kTagResources);
  } else {
    log_flags_ = CLEAR_BIT(log_flags_, kTagResources);
  }
}

void HWCLogHandler::LogStrategy(bool enable) {
  if (enable) {
    log_flags_ = SET_BIT(log_flags_, kTagStrategy);
  } else {
    log_flags_ = CLEAR_BIT(log_flags_, kTagStrategy);
  }
}

void HWCLogHandler::Error(LogTag /*tag*/, const char *format, ...) {
  va_list list;
  va_start(list, format);
  __android_log_vprint(ANDROID_LOG_ERROR, LOG_TAG, format, list);
}

void HWCLogHandler::Warning(LogTag /*tag*/, const char *format, ...) {
  va_list list;
  va_start(list, format);
  __android_log_vprint(ANDROID_LOG_WARN, LOG_TAG, format, list);
}

void HWCLogHandler::Info(LogTag tag, const char *format, ...) {
  if (IS_BIT(log_flags_, tag)) {
    va_list list;
    va_start(list, format);
    __android_log_vprint(ANDROID_LOG_INFO, LOG_TAG, format, list);
  }
}

void HWCLogHandler::Verbose(LogTag tag, const char *format, ...) {
  if (IS_BIT(log_flags_, tag)) {
    va_list list;
    va_start(list, format);
    __android_log_vprint(ANDROID_LOG_VERBOSE, LOG_TAG, format, list);
  }
}

}  // namespace sde

