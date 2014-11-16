LOCAL_PATH := $(call my-dir)
include hardware/qcom/display/displayengine/libs/common.mk
include $(CLEAR_VARS)

LOCAL_MODULE                  := libsde
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes) $(kernel_includes)
LOCAL_CFLAGS                  := $(common_flags) -DLOG_TAG=\"SDE\"
LOCAL_SHARED_LIBRARIES        := $(common_libs) libdl libsdeutils
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps)
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
                                 offline_ctrl.cpp \
                                 hw_interface.cpp \
                                 hw_framebuffer.cpp \
                                 dump_impl.cpp

include $(BUILD_SHARED_LIBRARY)
