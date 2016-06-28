/*
* Copyright (c) 2015, The Linux Foundation. All rights reserved.
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

#ifndef __SYS_H__
#define __SYS_H__

#include <sys/eventfd.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <pthread.h>

namespace sdm {

class Sys {
 public:
  // Pointers to system calls which are either mapped to actual system call or virtual driver.
  typedef int (*ioctl)(int, int, ...);
  typedef int (*open)(const char *, int, ...);
  typedef int (*close)(int);
  typedef int (*poll)(struct pollfd *, nfds_t, int);
  typedef ssize_t (*pread)(int, void *, size_t, off_t);
  typedef ssize_t (*pwrite)(int, const void *, size_t, off_t);
  typedef FILE* (*fopen)( const char *fname, const char *mode);
  typedef int (*fclose)(FILE* fileptr);
  typedef ssize_t (*getline)(char **lineptr, size_t *linelen, FILE *stream);
  typedef int (*pthread_cancel)(pthread_t thread);
  typedef int (*dup)(int fd);
  typedef ssize_t (*read)(int, void *, size_t);
  typedef ssize_t (*write)(int, const void *, size_t);
  typedef int (*eventfd)(unsigned int, int);

  static ioctl ioctl_;
  static open open_;
  static close close_;
  static poll poll_;
  static pread pread_;
  static pwrite pwrite_;
  static fopen fopen_;
  static fclose fclose_;
  static getline getline_;
  static pthread_cancel pthread_cancel_;
  static dup dup_;
  static read read_;
  static write write_;
  static eventfd eventfd_;
};

}  // namespace sdm

#endif  // __SYS_H__
