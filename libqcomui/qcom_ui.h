/*
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#ifndef INCLUDE_LIBQCOM_UI
#define INCLUDE_LIBQCOM_UI

#include <cutils/native_handle.h>

/*
 * Qcom specific Native Window perform operations
 */
enum {
    NATIVE_WINDOW_SET_BUFFERS_SIZE = 0x10000000,
};

/*
 * Function to check if the allocated buffer is of the correct size.
 * Reallocate the buffer with the correct size, if the size doesn't
 * match
 *
 * @param: handle of the allocated buffer
 * @param: requested size for the buffer
 * @param: usage flags
 *
 * return 0 on success
 */
int checkBuffer(native_handle_t *buffer_handle, int size, int usage);

/*
 * Checks if the format is supported by the GPU.
 *
 * @param: format to check
 *
 * @return true if the format is supported by the GPU.
 */
bool isGPUSupportedFormat(int format);

/*
 * Gets the number of arguments required for this operation.
 *
 * @param: operation whose argument count is required.
 *
 * @return -EINVAL if the operation is invalid.
 */
int getNumberOfArgsForOperation(int operation);
#endif // INCLUDE_LIBQCOM_UI
