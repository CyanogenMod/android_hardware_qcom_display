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

#gralloc module
LOCAL_PATH := $(call my-dir)
include $(LOCAL_PATH)/../common.mk
include $(CLEAR_VARS)

LOCAL_MODULE                  := gralloc.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_PATH             := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes)
LOCAL_SHARED_LIBRARIES        := $(common_libs) libmemalloc libgenlock libqdutils
LOCAL_SHARED_LIBRARIES        += libqdutils libGLESv1_CM
LOCAL_CFLAGS                  := $(common_flags) -DLOG_TAG=\"gralloc\"
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps)
LOCAL_SRC_FILES               :=  gpu.cpp gralloc.cpp framebuffer.cpp mapper.cpp
include $(BUILD_SHARED_LIBRARY)

#MemAlloc Library
include $(CLEAR_VARS)
LOCAL_MODULE           := libmemalloc
LOCAL_MODULE_TAGS      := optional
LOCAL_C_INCLUDES       := $(common_includes)
LOCAL_SHARED_LIBRARIES := $(common_libs) libgenlock libqdutils
LOCAL_CFLAGS           := $(common_flags) -DLOG_TAG=\"memalloc\"
LOCAL_SRC_FILES        := alloc_controller.cpp

ifneq ($(TARGET_USES_ION),false)
    LOCAL_SRC_FILES += ionalloc.cpp
else
    LOCAL_SRC_FILES += ashmemalloc.cpp \
                       pmemalloc.cpp \
                       pmem_bestfit_alloc.cpp
endif

include $(BUILD_SHARED_LIBRARY)
