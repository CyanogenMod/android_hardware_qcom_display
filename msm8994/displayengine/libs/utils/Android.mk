LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE                  := libsdeutils
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := hardware/qcom/display/displayengine/include/
LOCAL_CFLAGS                  := -Wno-missing-field-initializers -Wno-unused-parameter \
                                 -Wconversion -Wall -Werror \
                                 -DLOG_TAG=\"SDE\"
LOCAL_SHARED_LIBRARIES        := libcutils
LOCAL_SRC_FILES               := debug_android.cpp

include $(BUILD_SHARED_LIBRARY)
