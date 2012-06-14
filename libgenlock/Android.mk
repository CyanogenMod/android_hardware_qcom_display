LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
LOCAL_SHARED_LIBRARIES := liblog libcutils
LOCAL_C_INCLUDES := hardware/qcom/display/libgralloc
LOCAL_ADDITIONAL_DEPENDENCIES :=
LOCAL_SRC_FILES := genlock.cpp
LOCAL_CFLAGS:= -DLOG_TAG=\"libgenlock\"
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libgenlock
include $(BUILD_SHARED_LIBRARY)

