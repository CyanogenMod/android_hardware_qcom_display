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

/*! @file dump_interface.h
  @brief Interface file for dump options provided by display manager.

*/
#ifndef __DUMP_INTERFACE_H__
#define __DUMP_INTERFACE_H__

#include <stdint.h>

#include "sdm_types.h"

namespace sdm {

/*! @brief Display dump interface.

  @details This class defines dump methods provided by display manager.

*/
class DumpInterface {
 public:
  /*! @brief Method to get dump information in form of a string.

    @details Client shall use this method to get current snapshot of display manager context as a
    formatted string for logging or dumping purposes.

    @param[inout] buffer String buffer allocated by the client. Filled with null terminated dump
    information upon return.
    @param[in] length Length of the string buffer. Length shall be offset adjusted if any.

    @return \link DisplayError \endlink

    @warning Client shall ensure that this interface is not used while a display is being either
    created or destroyed through display core.
  */
  static DisplayError GetDump(char *buffer, uint32_t length);

 protected:
  virtual ~DumpInterface() { }
};

}  // namespace sdm

#endif  // __DUMP_INTERFACE_H__

