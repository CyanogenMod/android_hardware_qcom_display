LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE                  := hwcomposer.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH    := hw
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := hardware/qcom/display/displayengine/include/ \
                                 hardware/qcom/display/libgralloc/ \
                                 hardware/qcom/display/libqservice/
LOCAL_CFLAGS                  := -Wno-missing-field-initializers -Wno-unused-parameter \
                                 -Wconversion -Wall -Werror \
                                 -DLOG_TAG=\"SDE\"
LOCAL_SHARED_LIBRARIES        := libsde libqservice libbinder libhardware libhardware_legacy \
                                 libutils libcutils
LOCAL_SRC_FILES               := hwc_session.cpp \
                                 hwc_display.cpp \
                                 hwc_display_primary.cpp \
                                 hwc_display_external.cpp \
                                 hwc_display_virtual.cpp \
                                 hwc_debugger.cpp

include $(BUILD_SHARED_LIBRARY)
