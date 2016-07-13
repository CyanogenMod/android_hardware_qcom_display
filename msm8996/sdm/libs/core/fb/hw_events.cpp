/*
* Copyright (c) 2015 - 2016, The Linux Foundation. All rights reserved.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <utils/debug.h>
#include <utils/sys.h>
#include <pthread.h>
#include <algorithm>
#include <vector>
#include <map>
#include <utility>

#include "hw_events.h"

#define __CLASS__ "HWEvents"

namespace sdm {

DisplayError HWEventsInterface::Create(int fb_num, HWEventHandler *event_handler,
                                       std::vector<const char *> *event_list,
                                       HWEventsInterface **intf) {
  DisplayError error = kErrorNone;
  HWEvents *hw_events = NULL;

  hw_events = new HWEvents();
  error = hw_events->Init(fb_num, event_handler, event_list);
  if (error != kErrorNone) {
    delete hw_events;
  } else {
    *intf = hw_events;
  }

  return error;
}

DisplayError HWEventsInterface::Destroy(HWEventsInterface *intf) {
  HWEvents *hw_events = static_cast<HWEvents *>(intf);

  if (hw_events) {
    hw_events->Deinit();
    delete hw_events;
  }

  return kErrorNone;
}

pollfd HWEvents::InitializePollFd(HWEventData *event_data) {
  char node_path[kMaxStringLength] = {0};
  char data[kMaxStringLength] = {0};
  pollfd poll_fd;
  poll_fd.fd = -1;

  if (!strncmp(event_data->event_name, "thread_exit", strlen("thread_exit"))) {
    // Create an eventfd to be used to unblock the poll system call when
    // a thread is exiting.
    poll_fd.fd = Sys::eventfd_(0, 0);
    poll_fd.events |= POLLIN;
    exit_fd_ = poll_fd.fd;
  } else {
    snprintf(node_path, sizeof(node_path), "%s%d/%s", fb_path_, fb_num_, event_data->event_name);
    poll_fd.fd = Sys::open_(node_path, O_RDONLY);
    poll_fd.events |= POLLPRI | POLLERR;
  }

  if (poll_fd.fd < 0) {
    DLOGW("open failed for display=%d event=%s, error=%s", fb_num_, event_data->event_name,
          strerror(errno));
    return poll_fd;
  }

  // Read once on all fds to clear data on all fds.
  Sys::pread_(poll_fd.fd, data , kMaxStringLength, 0);

  return poll_fd;
}

DisplayError HWEvents::SetEventParser(const char *event_name, HWEventData *event_data) {
  DisplayError error = kErrorNone;

  if (!strncmp(event_name, "vsync_event", strlen("vsync_event"))) {
    event_data->event_parser = &HWEvents::HandleVSync;
  } else if (!strncmp(event_name, "show_blank_event", strlen("show_blank_event"))) {
    event_data->event_parser = &HWEvents::HandleBlank;
  } else if (!strncmp(event_name, "idle_notify", strlen("idle_notify"))) {
    event_data->event_parser = &HWEvents::HandleIdleTimeout;
  } else if (!strncmp(event_name, "msm_fb_thermal_level", strlen("msm_fb_thermal_level"))) {
    event_data->event_parser = &HWEvents::HandleThermal;
  } else if (!strncmp(event_name, "cec/rd_msg", strlen("cec/rd_msg"))) {
    event_data->event_parser = &HWEvents::HandleCECMessage;
  } else if (!strncmp(event_name, "thread_exit", strlen("thread_exit"))) {
    event_data->event_parser = &HWEvents::HandleThreadExit;
  } else {
    error = kErrorParameters;
  }

  return error;
}

void HWEvents::PopulateHWEventData() {
  for (uint32_t i = 0; i < event_list_->size(); i++) {
    const char *event_name = event_list_->at(i);
    HWEventData event_data;
    event_data.event_name = event_name;
    SetEventParser(event_name, &event_data);
    poll_fds_[i] = InitializePollFd(&event_data);
    event_data_list_.push_back(event_data);
  }
}

DisplayError HWEvents::Init(int fb_num, HWEventHandler *event_handler,
                            vector<const char *> *event_list) {
  if (!event_handler)
    return kErrorParameters;

  event_handler_ = event_handler;
  fb_num_ = fb_num;
  event_list_ = event_list;
  poll_fds_.resize(event_list_->size());
  event_thread_name_ += " - " + std::to_string(fb_num_);

  PopulateHWEventData();

  if (pthread_create(&event_thread_, NULL, &DisplayEventThread, this) < 0) {
    DLOGE("Failed to start %s, error = %s", event_thread_name_.c_str());
    return kErrorResources;
  }

  return kErrorNone;
}

DisplayError HWEvents::Deinit() {
  exit_threads_ = true;
  Sys::pthread_cancel_(event_thread_);

  uint64_t exit_value = 1;
  ssize_t write_size = Sys::write_(exit_fd_, &exit_value, sizeof(uint64_t));
  if (write_size != sizeof(uint64_t))
    DLOGW("Error triggering exit_fd_ (%d). write size = %d, error = %s", exit_fd_, write_size,
          strerror(errno));

  pthread_join(event_thread_, NULL);

  for (uint32_t i = 0; i < event_list_->size(); i++) {
    Sys::close_(poll_fds_[i].fd);
    poll_fds_[i].fd = -1;
  }

  return kErrorNone;
}

void* HWEvents::DisplayEventThread(void *context) {
  if (context) {
    return reinterpret_cast<HWEvents *>(context)->DisplayEventHandler();
  }

  return NULL;
}

void* HWEvents::DisplayEventHandler() {
  char data[kMaxStringLength] = {0};

  prctl(PR_SET_NAME, event_thread_name_.c_str(), 0, 0, 0);
  setpriority(PRIO_PROCESS, 0, kThreadPriorityUrgent);

  while (!exit_threads_) {
    int error = Sys::poll_(poll_fds_.data(), UINT32(event_list_->size()), -1);

    if (error <= 0) {
      DLOGW("poll failed. error = %s", strerror(errno));
      continue;
    }

    for (uint32_t event = 0; event < event_list_->size(); event++) {
      pollfd &poll_fd = poll_fds_[event];

      if (!strncmp(event_list_->at(event), "thread_exit", strlen("thread_exit"))) {
        if ((poll_fd.revents & POLLIN) && (Sys::read_(poll_fd.fd, data, kMaxStringLength) > 0)) {
          (this->*(event_data_list_[event]).event_parser)(data);
        }
      } else {
        if ((poll_fd.revents & POLLPRI) &&
                (Sys::pread_(poll_fd.fd, data, kMaxStringLength, 0) > 0)) {
          (this->*(event_data_list_[event]).event_parser)(data);
        }
      }
    }
  }

  pthread_exit(0);

  return NULL;
}

void HWEvents::HandleVSync(char *data) {
  int64_t timestamp = 0;
  if (!strncmp(data, "VSYNC=", strlen("VSYNC="))) {
    timestamp = strtoll(data + strlen("VSYNC="), NULL, 0);
  }

  event_handler_->VSync(timestamp);
}

void HWEvents::HandleIdleTimeout(char *data) {
  event_handler_->IdleTimeout();
}

void HWEvents::HandleThermal(char *data) {
  int64_t thermal_level = 0;
  if (!strncmp(data, "thermal_level=", strlen("thermal_level="))) {
    thermal_level = strtoll(data + strlen("thermal_level="), NULL, 0);
  }

  DLOGI("Received thermal notification with thermal level = %d", thermal_level);

  event_handler_->ThermalEvent(thermal_level);
}

void HWEvents::HandleCECMessage(char *data) {
  event_handler_->CECMessage(data);
}

}  // namespace sdm

