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

#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__

#include <stdlib.h>

#define LIKELY(exp) __builtin_expect((exp) != 0, true)
#define UNLIKELY(exp) __builtin_expect((exp) != 0, false)

#define INT(exp) static_cast<int>(exp)
#define FLOAT(exp) static_cast<float>(exp)
#define UINT8(exp) static_cast<uint8_t>(exp)
#define UINT32(exp) static_cast<uint32_t>(exp)
#define INT32(exp) static_cast<int32_t>(exp)

#define STRUCT_VAR(struct_name, var_name) \
          struct struct_name var_name; \
          memset(&var_name, 0, sizeof(var_name));

#define STRUCT_VAR_ARRAY(struct_name, var_name, num_var) \
          struct struct_name var_name[num_var]; \
          memset(&var_name[0], 0, sizeof(var_name));

#define ROUND_UP(number, step) ((((number) + ((step) - 1)) / (step)) * (step))

#define SET_BIT(value, bit) (value |= (1 << (bit)))
#define CLEAR_BIT(value, bit) (value &= (~(1 << (bit))))
#define IS_BIT_SET(value, bit) (value & (1 << (bit)))

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

namespace sde {

  const int kThreadPriorityUrgent = -9;
  const int kMaxRotatePerLayer = 2;

  typedef void * Handle;

}  // namespace sde

#endif  // __CONSTANTS_H__

