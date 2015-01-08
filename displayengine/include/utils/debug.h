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

#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <stdint.h>
#include <core/sde_types.h>

#define DLOG(tag, method, format, ...) Debug::GetLogHandler()->method(tag, \
                                            __CLASS__ "::%s: " format, __FUNCTION__, ##__VA_ARGS__)

#define DLOGE_IF(tag, format, ...) DLOG(tag, Error, format, ##__VA_ARGS__)
#define DLOGW_IF(tag, format, ...) DLOG(tag, Warning, format, ##__VA_ARGS__)
#define DLOGI_IF(tag, format, ...) DLOG(tag, Info, format, ##__VA_ARGS__)
#define DLOGV_IF(tag, format, ...) DLOG(tag, Verbose, format, ##__VA_ARGS__)

#define DLOGE(format, ...) DLOGE_IF(kTagNone, format, ##__VA_ARGS__)
#define DLOGW(format, ...) DLOGW_IF(kTagNone, format, ##__VA_ARGS__)
#define DLOGI(format, ...) DLOGI_IF(kTagNone, format, ##__VA_ARGS__)
#define DLOGV(format, ...) DLOGV_IF(kTagNone, format, ##__VA_ARGS__)

namespace sde {

class Debug {
 public:
  static inline void SetLogHandler(LogHandler *log_handler) { debug_.log_handler_ = log_handler; }
  static inline LogHandler* GetLogHandler() { return debug_.log_handler_; }
  static inline bool IsVirtualDriver() { return debug_.virtual_driver_; }
  static uint32_t GetSimulationFlag();
  static uint32_t GetHDMIResolution();

 private:
  Debug();

  // By default, drop any log messages coming from Display Engine. It will be overriden by Display
  // Engine client when core is successfully initialized.
  class DefaultLogHandler : public LogHandler {
   public:
    virtual void Error(LogTag /*tag*/, const char */*format*/, ...) { }
    virtual void Warning(LogTag /*tag*/, const char */*format*/, ...) { }
    virtual void Info(LogTag /*tag*/, const char */*format*/, ...) { }
    virtual void Verbose(LogTag /*tag*/, const char */*format*/, ...) { }
  };

  DefaultLogHandler default_log_handler_;
  LogHandler *log_handler_;
  bool virtual_driver_;
  static Debug debug_;
};

}  // namespace sde

#endif  // __DEBUG_H__

