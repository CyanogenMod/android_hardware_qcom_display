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

/*! @file strategy_interface.h
    @brief Interface file for strategy manager which will be used by display core to select a
    composition strategy for a frame to be displayed on target.
*/
#ifndef __STRATEGY_INTERFACE_H__
#define __STRATEGY_INTERFACE_H__

#include <core/display_types.h>

namespace sde {

/*! @brief Strategy library name

    @details This macro defines name for the composition strategy library. This macro shall be used
    to load library using dlopen().

    @sa GetStrategyInterface
*/
#define STRATEGY_LIBRARY_NAME "libsdestrategy.so"

/*! @brief Function name to get composer strategy interface

    @details This macro defines function name for GetStrategyInterface() which will be implemented
    in the composition strategy library. This macro shall be used to specify name of the function
    in dlsym().

    @sa GetStrategyInterface
*/
#define GET_STRATEGY_INTERFACE_NAME "GetStrategyInterface"

class StrategyInterface;

/*! @brief Function to get composer strategy interface.

    @details This function is used to get StrategyInterface object which resides in the composer
    strategy library loaded at runtime.

    @param[out] interface \link StrategyInterface \endlink

    @return \link DisplayError \endlink
*/
typedef DisplayError (*GetStrategyInterface)(StrategyInterface **interface);

/*! @brief Maximum number of layers that can be handled by hardware in a given layer stack.
*/
const int kMaxSDELayers = 16;

/*! @brief This structure defines constraints and display properties that shall be considered for
    deciding a composition strategy.

    @sa GetNextStrategy
*/
struct StrategyConstraints {
  bool safe_mode;         //!< In this mode, strategy manager chooses the composition strategy
                          //!< that requires minimum number of pipe for the current frame. i.e.,
                          //!< video only composition, secure only composition or GPU composition
  uint32_t max_layers;    //!< Maximum number of layers that shall be programmed on hardware for the
                          //!< given layer stack.

  StrategyConstraints() : safe_mode(false), max_layers(kMaxSDELayers) { }
};

/*! @brief Flag to denote that GPU composition is performed for the given layer stack.
*/
const uint32_t kFlagGPU = 0x1;

/*! @brief This structure encapsulates information about the input layer stack and the layers which
    shall be programmed on hardware.

    @sa GetNextStrategy
*/
struct HWLayersInfo {
  LayerStack *stack;        //!< Input layer stack. Set by the caller.

  uint32_t index[kMaxSDELayers];
                            //!< Indexes of the layers from the layer stack which need to be
                            //!< programmed on hardware.
  uint32_t count;           //!< Total number of layers which need to be set on hardware.
  uint32_t flags;           //!< Strategy flags. There is one flag set for each of the strategy
                            //!< that has been selected for this layer stack. This flag is preserved
                            //!< between multiple GetNextStrategy() calls. Composition manager
                            //!< relies on the total flag count to check the number of strategies
                            //!< that are attempted for this layer stack.

  HWLayersInfo() {
    Reset();
  }

  void Reset() {
    stack = NULL;
    count = 0;
    flags = 0;
  }
};

class StrategyInterface {
 public:
  /*! @brief Method to get strategy for a layer stack. Caller can loop through this method to try
    get all applicable strategies.

    @param[in] constraints \link StrategyConstraints \endlink
    @param[inout] layers_info \link HWLayersInfo \endlink

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetNextStrategy(StrategyConstraints *constraints,
                                       HWLayersInfo *hw_layers_info) = 0;

 protected:
  virtual ~StrategyInterface() { }
};

}  // namespace sde

#endif  // __STRATEGY_INTERFACE_H__

