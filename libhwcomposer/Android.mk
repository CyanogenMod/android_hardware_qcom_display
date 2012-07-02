LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SHARED_LIBRARIES := liblog libcutils libhardware libutils
LOCAL_SHARED_LIBRARIES += libEGL liboverlay libgenlock libqdutils
LOCAL_SRC_FILES :=  hwc.cpp          \
                    hwc_overlay.cpp  \
                    hwc_utils.cpp
LOCAL_MODULE := hwcomposer.$(TARGET_BOARD_PLATFORM)
LOCAL_C_INCLUDES := hardware/qcom/display/libgralloc
LOCAL_C_INCLUDES += hardware/qcom/display/libgenlock
LOCAL_C_INCLUDES += hardware/qcom/display/liboverlay
LOCAL_C_INCLUDES += hardware/qcom/display/libqdutils
LOCAL_CFLAGS:= -DLOG_TAG=\"$(TARGET_BOARD_PLATFORM).hwcomposer\"

LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
