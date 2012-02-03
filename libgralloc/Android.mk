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

# Use this flag until pmem/ashmem is implemented in the new gralloc
LOCAL_PATH := $(call my-dir)

# HAL module implemenation, not prelinked and stored in
# hw/<OVERLAY_HARDWARE_MODULE_ID>.<ro.product.board>.so
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SHARED_LIBRARIES := liblog libcutils libGLESv1_CM libutils libmemalloc
LOCAL_SHARED_LIBRARIES += libgenlock
LOCAL_C_INCLUDES += hardware/qcom/display/libgenlock
LOCAL_SRC_FILES :=  framebuffer.cpp \
                    gpu.cpp         \
                    gralloc.cpp     \
                    mapper.cpp

LOCAL_MODULE := gralloc.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS:= -DLOG_TAG=\"$(TARGET_BOARD_PLATFORM).gralloc\" -DHOST -DDEBUG_CALC_FPS
LOCAL_CFLAGS += -DQCOM_HARDWARE

ifeq ($(call is-board-platform,msm7x27),true)
    LOCAL_CFLAGS += -DTARGET_MSM7x27
endif

ifeq ($(TARGET_QCOM_HDMI_OUT),true)
    LOCAL_CFLAGS += -DHDMI_DUAL_DISPLAY -DQCOM_HDMI_OUT
    LOCAL_C_INCLUDES += hardware/qcom/display/liboverlay
    LOCAL_SHARED_LIBRARIES += liboverlay
endif

ifeq ($(TARGET_USES_SF_BYPASS),true)
    LOCAL_CFLAGS += -DSF_BYPASS
endif

ifeq ($(TARGET_GRALLOC_USES_ASHMEM),true)
    LOCAL_CFLAGS += -DUSE_ASHMEM
endif

include $(BUILD_SHARED_LIBRARY)

#MemAlloc Library
include $(CLEAR_VARS)


LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
LOCAL_SHARED_LIBRARIES := liblog libcutils libutils
LOCAL_SRC_FILES +=  ashmemalloc.cpp \
                    pmemalloc.cpp \
                    pmem_bestfit_alloc.cpp \
                    alloc_controller.cpp

LOCAL_CFLAGS:= -DLOG_TAG=\"memalloc\" -DLOG_NDDEBUG=0

ifeq ($(TARGET_USES_ION),true)
    LOCAL_CFLAGS += -DUSE_ION
    LOCAL_SRC_FILES += ionalloc.cpp
endif

LOCAL_MODULE := libmemalloc
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
