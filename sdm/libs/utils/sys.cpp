/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

#include <utils/sys.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#define __CLASS__ "Sys"

namespace sdm {

#ifndef SDM_VIRTUAL_DRIVER

int PthreadCancel(pthread_t /* thread */) {
  return 0;
}

// Pointer to actual driver interfaces.
Sys::ioctl Sys::ioctl_ = ::ioctl;
Sys::open Sys::open_ = ::open;
Sys::close Sys::close_ = ::close;
Sys::poll Sys::poll_ = ::poll;
Sys::pread Sys::pread_ = ::pread;
Sys::pwrite Sys::pwrite_ = ::pwrite;
Sys::fopen Sys::fopen_ = ::fopen;
Sys::fclose Sys::fclose_ = ::fclose;
Sys::getline Sys::getline_ = ::getline;
Sys::pthread_cancel Sys::pthread_cancel_ = PthreadCancel;
Sys::dup Sys::dup_ = ::dup;

#else

// Point to virtual driver interfaces.
extern int virtual_ioctl(int fd, int cmd, ...);
extern int virtual_open(const char *file_name, int access, ...);
extern int virtual_close(int fd);
extern int virtual_poll(struct pollfd *fds,  nfds_t num, int timeout);
extern ssize_t virtual_pread(int fd, void *data, size_t count, off_t offset);
extern ssize_t virtual_pwrite(int fd, const void *data, size_t count, off_t offset);
extern FILE* virtual_fopen(const char *fname, const char *mode);
extern int virtual_fclose(FILE* fileptr);
extern ssize_t virtual_getline(char **lineptr, size_t *linelen, FILE *stream);
extern int virtual_dup(int fd);

Sys::ioctl Sys::ioctl_ = virtual_ioctl;
Sys::open Sys::open_ = virtual_open;
Sys::close Sys::close_ = virtual_close;
Sys::poll Sys::poll_ = virtual_poll;
Sys::pread Sys::pread_ = virtual_pread;
Sys::pwrite Sys::pwrite_ = virtual_pwrite;
Sys::fopen Sys::fopen_ = virtual_fopen;
Sys::fclose Sys::fclose_ = virtual_fclose;
Sys::getline Sys::getline_ = virtual_getline;
Sys::pthread_cancel Sys::pthread_cancel_ = ::pthread_cancel;
Sys::dup Sys::dup_ = virtual_dup;

#endif  // SDM_VIRTUAL_DRIVER

}  // namespace sdm

