# Copyright (C) 2008 The Android Open Source Project
# Copyright (c) 2011, Code Aurora Forum. All rights reserved.
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
LOCAL_SHARED_LIBRARIES := liblog libcutils
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
LOCAL_C_INCLUDES += hardware/msm7k/libgralloc-qsd8k
LOCAL_SRC_FILES := \
    overlayLib.cpp \
    overlayLibUI.cpp \
LOCAL_CFLAGS:= -DLOG_TAG=\"OverlayLib\"
ifeq ($(TARGET_USE_HDMI_AS_PRIMARY),true)
LOCAL_CFLAGS += -DHDMI_AS_PRIMARY
endif
LOCAL_MODULE := liboverlay
include $(BUILD_SHARED_LIBRARY)

# HAL module implemenation, not prelinked and stored in
# hw/<OVERLAY_HARDWARE_MODULE_ID>.<ro.product.board>.so
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SHARED_LIBRARIES := liblog liboverlay libcutils
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
LOCAL_C_INCLUDES += hardware/msm7k/libgralloc-qsd8k
LOCAL_SRC_FILES := overlay.cpp
LOCAL_MODULE := overlay.default
include $(BUILD_SHARED_LIBRARY)
