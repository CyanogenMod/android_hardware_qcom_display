# Copyright (C) 2008 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
LOCAL_PATH := $(call my-dir)

# HAL module implemenation, not prelinked and stored in
# hw/<OVERLAY_HARDWARE_MODULE_ID>.<ro.product.board>.so
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE   := false
LOCAL_MODULE_PATH      := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SHARED_LIBRARIES := liblog libcutils libutils libmemalloc
LOCAL_SHARED_LIBRARIES += libgenlock libqdutils libGLESv1_CM
LOCAL_C_INCLUDES       := hardware/qcom/display/liboverlay/
LOCAL_C_INCLUDES       += hardware/qcom/display/libgenlock
LOCAL_C_INCLUDES       += hardware/qcom/display/libqdutils
LOCAL_MODULE           := gralloc.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS      := optional
LOCAL_CFLAGS           := -DLOG_TAG=\"$(TARGET_BOARD_PLATFORM).gralloc\" \
                          -DDEBUG_CALC_FPS -Wno-missing-field-initializers
LOCAL_SRC_FILES :=  gpu.cpp gralloc.cpp framebuffer.cpp mapper.cpp

ifeq ($(TARGET_USES_POST_PROCESSING),true)
    LOCAL_CFLAGS     += -DUSES_POST_PROCESSING
    LOCAL_C_INCLUDES += $(TARGET_OUT_HEADERS)/pp/inc
endif

ifeq ($(TARGET_USES_MDP3), true)
    LOCAL_CFLAGS += -DUSE_MDP3
endif

ifeq ($(TARGET_HAVE_HDMI_OUT),true)
    LOCAL_CFLAGS += -DHDMI_DUAL_DISPLAY
    LOCAL_SHARED_LIBRARIES += liboverlay
endif
include $(BUILD_SHARED_LIBRARY)

#MemAlloc Library
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
LOCAL_C_INCLUDES := hardware/qcom/display/libqdutils
LOCAL_SHARED_LIBRARIES := liblog libcutils libutils
LOCAL_SRC_FILES :=  ionalloc.cpp alloc_controller.cpp
LOCAL_CFLAGS:= -DLOG_TAG=\"memalloc\"
LOCAL_CFLAGS += -DUSE_ION
LOCAL_MODULE := libmemalloc
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
