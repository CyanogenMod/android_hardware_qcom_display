/*
* Copyright (c) 2014 - 2015, The Linux Foundation. All rights reserved.
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

#ifndef __CORE_IMPL_H__
#define __CORE_IMPL_H__

#include <core/core_interface.h>
#include <private/strategy_interface.h>
#include <utils/locker.h>

#include "hw_interface.h"
#include "comp_manager.h"
#include "offline_ctrl.h"

#define SET_REVISION(major, minor) ((major << 8) | minor)

namespace sde {

class CoreImpl : public CoreInterface {
 public:
  // This class implements display core interface revision 1.0.
  static const uint16_t kRevision = SET_REVISION(1, 0);

  CoreImpl(CoreEventHandler *event_handler, BufferAllocator *buffer_allocator,
           BufferSyncHandler *buffer_sync_handler);
  virtual ~CoreImpl() { }

  // This method returns the interface revision for the current display core object.
  // Future revisions will override this method and return the appropriate revision upon query.
  virtual uint16_t GetRevision() { return kRevision; }
  virtual DisplayError Init();
  virtual DisplayError Deinit();

  // Methods from core interface
  virtual DisplayError CreateDisplay(DisplayType type, DisplayEventHandler *event_handler,
                                     DisplayInterface **intf);
  virtual DisplayError DestroyDisplay(DisplayInterface *intf);

 protected:
  Locker locker_;
  CoreEventHandler *event_handler_;
  BufferAllocator *buffer_allocator_;
  BufferSyncHandler *buffer_sync_handler_;
  HWInterface *hw_intf_;
  CompManager comp_mgr_;
  OfflineCtrl offline_ctrl_;
};

}  // namespace sde

#endif  // __CORE_IMPL_H__

