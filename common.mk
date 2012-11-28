#Common headers
common_includes := hardware/qcom/display/libgralloc
common_includes += hardware/qcom/display/libgenlock
common_includes += hardware/qcom/display/liboverlay
common_includes += hardware/qcom/display/libcopybit
common_includes += hardware/qcom/display/libqdutils
common_includes += hardware/qcom/display/libhwcomposer
common_includes += hardware/qcom/display/libexternal
common_includes += hardware/qcom/display/libqservice

ifeq ($(TARGET_USES_POST_PROCESSING),true)
    common_flags     += -DUSES_POST_PROCESSING
    common_includes += $(TARGET_OUT_HEADERS)/pp/inc
endif


#Common libraries external to display HAL
common_libs := liblog libutils libcutils libhardware

#Common C flags
common_flags := -DDEBUG_CALC_FPS -Wno-missing-field-initializers

#TODO
#ifeq ($(call is-vendor-board-platform,QCOM),true)
ifeq ($(TARGET_BOARD_PLATFORM), msm8960)
    common_flags += -DUSE_FENCE_SYNC
endif

ifeq ($(ARCH_ARM_HAVE_NEON),true)
    common_flags += -D__ARM_HAVE_NEON
endif

ifeq ($(TARGET_NO_HW_VSYNC),true)
    common_flags += -DNO_HW_VSYNC
endif

ifeq ($(TARGET_NO_HW_OVERLAY),true)
    common_flags += -DNO_HW_OVERLAY
endif

ifneq ($(BOARD_HAVE_HDMI_SUPPORT),false)
    common_flags += -DBOARD_USES_HDMI
endif

ifneq ($(TARGET_USES_ION), false)
    common_flags += -DUSE_ION
endif

common_deps  :=
kernel_includes :=

#Kernel includes. Not being executed on JB+
ifeq ($(call is-vendor-board-platform,QCOM),true)
    common_deps += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
    kernel_includes += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
endif
