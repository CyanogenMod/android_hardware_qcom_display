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

/*! @file sde_types.h
  @brief This file contains miscellaneous data types used across display interfaces.
*/
#ifndef __SDE_TYPES_H__
#define __SDE_TYPES_H__

namespace sde {

/*! @brief This enum represents different error codes that display interfaces may return.
*/
enum DisplayError {
  kErrorNone,             //!< Call executed successfully.
  kErrorUndefined,        //!< An unspecified error has occured.
  kErrorNotSupported,     //!< Requested operation is not supported.
  kErrorVersion,          //!< Client is using advanced version of interfaces and calling into an
                          //!< older version of display library.
  kErrorDataAlignment,    //!< Client data structures are not aligned on naturual boundaries.
  kErrorInstructionSet,   //!< 32-bit client is calling into 64-bit library or vice versa.
  kErrorParameters,       //!< Invalid parameters passed to a method.
  kErrorFileDescriptor,   //!< Invalid file descriptor.
  kErrorMemory,           //!< System is running low on memory.
  kErrorResources,        //!< Not enough hardware resources available to execute call.
  kErrorHardware,         //!< A hardware error has occured.
  kErrorTimeOut,          //!< The operation has timed out to prevent client from waiting forever.
};

/*! @brief This enum represents different modules/logical unit tags that a log message may be
  associated with. Client may use this to filter messages for dynamic logging.

  @sa DisplayLogHandler
*/
enum LogTag {
  kTagNone,             //!< Log is not tagged. This type of logs should always be printed.
  kTagResources,        //!< Log is tagged for resource management.
  kTagStrategy,         //!< Log is tagged for strategy decisions.
};

/*! @brief Display log handler class.

  @details This class defines display log handler. The handle contains methods which client should
  implement to get different levels of logging from display engine. Display engine will call into
  these methods at appropriate times to send logging information.

  @sa CoreInterface::CreateCore
*/
class LogHandler {
 public:
  /*! @brief Method to handle error messages.

    @param[in] tag \link LogTag \endlink
    @param[in] format \link message format with variable argument list \endlink
  */
  virtual void Error(LogTag tag, const char *format, ...) = 0;

  /*! @brief Method to handle warning messages.

    @param[in] tag \link LogTag \endlink
    @param[in] format \link message format with variable argument list \endlink
  */
  virtual void Warning(LogTag tag, const char *format, ...) = 0;

  /*! @brief Method to handle informative messages.

    @param[in] tag \link LogTag \endlink
    @param[in] format \link message format with variable argument list \endlink
  */
  virtual void Info(LogTag tag, const char *format, ...) = 0;

  /*! @brief Method to handle verbose messages.

    @param[in] tag \link LogTag \endlink
    @param[in] format \link message format with variable argument list \endlink
  */
  virtual void Verbose(LogTag tag, const char *format, ...) = 0;

 protected:
  virtual ~LogHandler() { }
};

/*! @brief This structure is defined for client and library compatibility check purpose only. This
  structure is used in SDE_VERSION_TAG definition only. Client should not refer it directly for
  any purpose.
*/
struct SDECompatibility {
  char c1;
  int i1;
  char c2;
  int i2;
};

}  // namespace sde

#endif  // __SDE_TYPES_H__

