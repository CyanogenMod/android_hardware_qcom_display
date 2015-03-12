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

#include <core/sde_types.h>
#include <cutils/log.h>

#define DLOG(Macro, format, ...) Macro(__CLASS__ "::%s: " format, __FUNCTION__, ##__VA_ARGS__)

#define DLOGE(format, ...) DLOG(ALOGE, format, ##__VA_ARGS__)
#define DLOGW(format, ...) DLOG(ALOGW, format, ##__VA_ARGS__)
#define DLOGI(format, ...) DLOG(ALOGI, format, ##__VA_ARGS__)
#define DLOGV(format, ...) DLOG(ALOGV, format, ##__VA_ARGS__)

namespace sde {

class HWCLogHandler : public LogHandler {
 public:
  static inline LogHandler* Get() { return &log_handler_; }
  static void LogAll(bool enable);
  static void LogResources(bool enable);
  static void LogStrategy(bool enable);

  virtual void Error(LogTag tag, const char *format, ...);
  virtual void Warning(LogTag tag, const char *format, ...);
  virtual void Info(LogTag tag, const char *format, ...);
  virtual void Verbose(LogTag tag, const char *format, ...);

 private:
  static HWCLogHandler log_handler_;
  static uint32_t log_flags_;
};

}  // namespace sde

#endif  // __HWC_LOGGER_H__

