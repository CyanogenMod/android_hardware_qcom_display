/*
* Copyright (c) 2015 - 2016, The Linux Foundation. All rights reserved.
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

#ifndef __HW_EVENTS_H__
#define __HW_EVENTS_H__

#include <sys/poll.h>
#include <string>
#include <vector>
#include <map>
#include <utility>

#include "hw_interface.h"
#include "hw_events_interface.h"

namespace sdm {

class HWEvents : public HWEventsInterface {
 public:
  DisplayError Init(int fb_num, HWEventHandler *event_handler,
                    std::vector<const char *> *event_list);
  DisplayError Deinit();

 private:
  static const int kMaxStringLength = 1024;

  typedef void (HWEvents::*EventParser)(char *);

  struct HWEventData {
    const char* event_name = NULL;
    EventParser event_parser = NULL;
  };

  static void* DisplayEventThread(void *context);
  void* DisplayEventHandler();
  void HandleVSync(char *data);
  void HandleBlank(char *data) { }
  void HandleIdleTimeout(char *data);
  void HandleThermal(char *data);
  void HandleCECMessage(char *data);
  void HandleThreadExit(char *data) { }
  void PopulateHWEventData();
  DisplayError SetEventParser(const char *event_name, HWEventData *event_data);
  pollfd InitializePollFd(HWEventData *event_data);

  HWEventHandler *event_handler_ = NULL;
  std::vector<const char *> *event_list_ = NULL;
  std::vector<HWEventData> event_data_list_ = {};
  pollfd *poll_fds_ = NULL;
  pthread_t event_thread_;
  std::string event_thread_name_ = "SDM_EventThread";
  bool exit_threads_ = false;
  const char* fb_path_ = "/sys/devices/virtual/graphics/fb";
  int fb_num_ = -1;
  int exit_fd_ = -1;
};

}  // namespace sdm

#endif  // __HW_EVENTS_H__

