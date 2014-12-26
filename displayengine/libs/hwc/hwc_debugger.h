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

#ifndef __HWC_DEBUGGER_H__
#define __HWC_DEBUGGER_H__

#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#include <core/sde_types.h>
#include <core/debug_interface.h>
#include <cutils/log.h>
#include <utils/Trace.h>

#define DLOG(Macro, format, ...) Macro(__CLASS__ "::%s: " format, __FUNCTION__, ##__VA_ARGS__)

#define DLOGE(format, ...) DLOG(ALOGE, format, ##__VA_ARGS__)
#define DLOGW(format, ...) DLOG(ALOGW, format, ##__VA_ARGS__)
#define DLOGI(format, ...) DLOG(ALOGI, format, ##__VA_ARGS__)
#define DLOGV(format, ...) DLOG(ALOGV, format, ##__VA_ARGS__)

#define DTRACE_BEGIN(custom_string) HWCDebugHandler::Get()->BeginTrace(__CLASS__, __FUNCTION__, \
                                                                       custom_string)
#define DTRACE_END() HWCDebugHandler::Get()->EndTrace()
#define DTRACE_SCOPED() ScopeTracer<HWCDebugHandler> scope_tracer(__CLASS__, __FUNCTION__)

namespace sde {

class HWCDebugHandler : public DebugHandler {
 public:
  static inline DebugHandler* Get() { return &debug_handler_; }
  static void DebugAll(bool enable);
  static void DebugResources(bool enable);
  static void DebugStrategy(bool enable);
  static void DebugCompManager(bool enable);
  static void DebugDriverConfig(bool enable);
  static void DebugBufferManager(bool enable);
  static void DebugOfflineCtrl(bool enable);

  virtual void Error(DebugTag tag, const char *format, ...);
  virtual void Warning(DebugTag tag, const char *format, ...);
  virtual void Info(DebugTag tag, const char *format, ...);
  virtual void Verbose(DebugTag tag, const char *format, ...);
  virtual void BeginTrace(const char *class_name, const char *function_name,
                          const char *custom_string);
  virtual void EndTrace();

 private:
  static HWCDebugHandler debug_handler_;
  static uint32_t debug_flags_;
};

}  // namespace sde

#endif  // __HWC_DEBUGGER_H__

