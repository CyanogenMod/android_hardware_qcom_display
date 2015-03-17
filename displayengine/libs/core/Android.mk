LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE                  := libsde
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := hardware/qcom/display/displayengine/include/ \
                                 $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
LOCAL_CFLAGS                  := -Wno-missing-field-initializers -Wno-unused-parameter \
                                 -Wconversion -Wall -Werror \
                                 -DLOG_TAG=\"SDE\"
ifeq ($(TARGET_USES_SCALAR), true)
  LOCAL_C_INCLUDES  += $(TARGET_OUT_HEADERS)/scalar/inc
  LOCAL_CFLAGS      += -DUSES_SCALAR
endif
LOCAL_HW_INTF_PATH := fb
LOCAL_SHARED_LIBRARIES        := libdl libsdeutils libcutils
LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
LOCAL_SRC_FILES               := core_interface.cpp \
                                 core_impl.cpp \
                                 display_base.cpp \
                                 display_primary.cpp \
                                 display_hdmi.cpp \
                                 display_virtual.cpp \
                                 comp_manager.cpp \
                                 strategy_default.cpp \
                                 res_manager.cpp \
                                 res_config.cpp \
                                 rotator_ctrl.cpp \
                                 dump_impl.cpp \
                                 session_manager.cpp \
                                 scalar_helper.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_info.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_device.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_primary.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_rotator.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_hdmi.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_virtual.cpp

include $(BUILD_SHARED_LIBRARY)
