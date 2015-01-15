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

#include <core/sde_types.h>
#include <core/display_interface.h>

namespace sde {

/*! @brief Strategy library name

  @details This macro defines name for the composition strategy library. This macro shall be used
  to load library using dlopen().

  @sa CreateStrategyInterface
  @sa DestoryStrategyInterface
*/
#define STRATEGY_LIBRARY_NAME "libsdestrategy.so"

/*! @brief Function name to create composer strategy interface

  @details This macro defines function name for CreateStrategyInterface() which is implemented in
  the composition strategy library. This macro shall be used to specify name of the function in
  dlsym().

  @sa CreateStrategyInterface
*/
#define CREATE_STRATEGY_INTERFACE_NAME "CreateStrategyInterface"

/*! @brief Function name to destroy composer strategy interface

  @details This macro defines function name for DestroyStrategyInterface() which is implemented in
  the composition strategy library. This macro shall be used to specify name of the function in
  dlsym().

  @sa DestroyStrategyInterface
*/
#define DESTROY_STRATEGY_INTERFACE_NAME "DestroyStrategyInterface"

/*! @brief Strategy interface version.

  @details Strategy interface is version tagged to maintain backward compatibility. This version is
  supplied as a default argument during strategy library initialization.

  Client may use an older version of interfaces and link to a higher version of strategy library,
  but vice versa is not allowed.

  @sa CreateStrategyInterface
*/
#define STRATEGY_REVISION_MAJOR (1)
#define STRATEGY_REVISION_MINOR (0)

#define STRATEGY_VERSION_TAG ((uint16_t) ((STRATEGY_REVISION_MAJOR << 8) | STRATEGY_REVISION_MINOR))

class StrategyInterface;

/*! @brief Function to create composer strategy interface.

  @details This function is used to create StrategyInterface object which resides in the composer
  strategy library loaded at runtime.

  @param[in] version \link STRATEGY_VERSION_TAG \endlink
  @param[in] type \link DisplayType \endlink
  @param[out] interface \link StrategyInterface \endlink

  @return \link DisplayError \endlink
*/
typedef DisplayError (*CreateStrategyInterface)(uint16_t version, DisplayType type,
                      StrategyInterface **interface);

/*! @brief Function to destroy composer strategy interface.

  @details This function is used to destroy StrategyInterface object.

  @param[in] interface \link StrategyInterface \endlink

  @return \link DisplayError \endlink
*/
typedef DisplayError (*DestroyStrategyInterface)(StrategyInterface *interface);

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

/*! @brief This structure encapsulates information about the input layer stack and the layers which
  shall be programmed on hardware.

  @sa Start
*/
struct HWLayersInfo {
  LayerStack *stack;        //!< Input layer stack. Set by the caller.

  uint32_t index[kMaxSDELayers];
                            //!< Indexes of the layers from the layer stack which need to be
                            //!< programmed on hardware.

  uint32_t count;           //!< Total number of layers which need to be set on hardware.

  HWLayersInfo() : stack(NULL), count(0) { }
};

/*! @brief Strategy interface.

  @details This class defines Strategy interface. It contains methods which client shall use to
  determine which strategy to be used for layer composition. This interface is created during
  display device creation and remains valid until destroyed.
*/

class StrategyInterface {
 public:
  /*! @brief Method to indicate start of a new strategy selection iteration for a layer stack.

    @details Client shall call this method at beginning of each draw cycle before iterating
    through strategy selection. Strategy interface implementation uses this method to do
    preprocessing for a given layer stack.

    @param[in] layers_info \link HWLayersInfo \endlink
    @param[out] max_attempts Maximum calls to \link GetNextStrategy \endlink

    @return \link DisplayError \endlink
  */
  virtual DisplayError Start(HWLayersInfo *hw_layers_info, uint32_t *max_attempts) = 0;


  /*! @brief Method to get strategy for a layer stack. Caller can loop through this method to try
    get all applicable strategies.

    @param[in] constraints \link StrategyConstraints \endlink

    @return \link DisplayError \endlink
  */
  virtual DisplayError GetNextStrategy(StrategyConstraints *constraints) = 0;

  /*! @brief Method to indicate end of a strategy selection cycle.

    @return \link DisplayError \endlink
  */
  virtual DisplayError Stop() = 0;

 protected:
  virtual ~StrategyInterface() { }
};

}  // namespace sde

#endif  // __STRATEGY_INTERFACE_H__

