LOCAL_PATH := $(call my-dir)

# HAL module implemenation, not prelinked and stored in
# hw/<OVERLAY_HARDWARE_MODULE_ID>.<ro.product.board>.so
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SHARED_LIBRARIES := liblog libcutils libEGL libhardware libutils liboverlay

LOCAL_SRC_FILES := 	\
	hwcomposer.cpp
	
LOCAL_MODULE := hwcomposer.$(TARGET_BOARD_PLATFORM)
LOCAL_CFLAGS:= -DLOG_TAG=\"$(TARGET_BOARD_PLATFORM).hwcomposer\"
LOCAL_C_INCLUDES += hardware/qcom/display/libgralloc
LOCAL_C_INCLUDES += hardware/qcom/display/liboverlay
LOCAL_C_INCLUDES += hardware/qcom/display/libcopybit
LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
ifeq ($(TARGET_HAVE_HDMI_OUT),true)
LOCAL_CFLAGS += -DHDMI_DUAL_DISPLAY
endif
ifeq ($(TARGET_HAVE_BYPASS),true)
LOCAL_CFLAGS += -DCOMPOSITION_BYPASS
endif
ifeq ($(TARGET_USE_HDMI_AS_PRIMARY),true)
#LOCAL_CFLAGS += -DHDMI_AS_PRIMARY
endif
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
