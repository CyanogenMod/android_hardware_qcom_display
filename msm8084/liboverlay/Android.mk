LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_SHARED_LIBRARIES += libcutils
LOCAL_SHARED_LIBRARIES += libutils
LOCAL_SHARED_LIBRARIES += libmemalloc
LOCAL_C_INCLUDES := hardware/qcom/display/libgralloc
LOCAL_SRC_FILES := \
      overlay.cpp \
      overlayCtrl.cpp \
      overlayUtils.cpp \
      overlayMdp.cpp \
      overlayRotator.cpp \
      overlayTransitions.cpp

LOCAL_CFLAGS:= -DLOG_TAG=\"overlay2\"
LOCAL_MODULE := liboverlay
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
