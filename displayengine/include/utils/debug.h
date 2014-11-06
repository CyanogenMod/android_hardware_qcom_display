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

#ifndef SDE_LOG_TAG
#define SDE_LOG_TAG kLogTagNone
#endif

#ifndef SDE_MODULE_NAME
#define SDE_MODULE_NAME "SDE"
#endif

#define DLOG(method, format, ...) Debug::method(SDE_LOG_TAG, SDE_MODULE_NAME ": " format, \
                                                ##__VA_ARGS__)

// SDE_LOG_TAG and SDE_MODULE_NAME must be defined before #include this header file in
// respective module, else default definitions are used.
#define DLOGE(format, ...) DLOG(Error, format, ##__VA_ARGS__)
#define DLOGW(format, ...) DLOG(Warning, format, ##__VA_ARGS__)
#define DLOGI(format, ...) DLOG(Info, format, ##__VA_ARGS__)
#define DLOGV(format, ...) DLOG(Verbose, format, ##__VA_ARGS__)

namespace sde {

enum LogTag {
  kTagNone = 0,   //!< Log tag name is not specified.
  kTagCore,       //!< Log is tagged for display core.
  kTagStrategy,   //!< Log is tagged for composition strategy.
};

class Debug {
 public:
  // Log handlers
  static void Error(const LogTag &tag, const char *format, ...);
  static void Warning(const LogTag &tag, const char *format, ...);
  static void Info(const LogTag &tag, const char *format, ...);
  static void Verbose(const LogTag &tag, const char *format, ...);

  // Debug properties
  static bool IsVirtualDriver() { return debug_.virtual_driver_; }

 private:
  Debug();
  bool virtual_driver_;
  static Debug debug_;
};

}  // namespace sde

#endif  // __DEBUG_H__

