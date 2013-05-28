#Common headers
common_includes := hardware/qcom/display/libgralloc
common_includes += hardware/qcom/display/liboverlay
common_includes += hardware/qcom/display/libcopybit
common_includes += hardware/qcom/display/libqdutils
common_includes += hardware/qcom/display/libhwcomposer
common_includes += hardware/qcom/display/libexternal
common_includes += hardware/qcom/display/libqservice

#Common libraries external to display HAL
common_libs := liblog libutils libcutils libhardware

#Common C flags
common_flags := -DDEBUG_CALC_FPS -Wno-missing-field-initializers
common_flags += -Werror

ifeq ($(ARCH_ARM_HAVE_NEON),true)
    common_flags += -D__ARM_HAVE_NEON
endif

ifneq ($(filter msm8974 msm8x74 msm8226 msm8x26,$(TARGET_BOARD_PLATFORM)),)
    # TODO: This define makes us pick a few inline functions
    # from the kernel header media/msm_media_info.h. However,
    # the bionic clean_headers utility scrubs them out.
    # Figure out a way to import those macros correctly
    # common_flags += -DVENUS_COLOR_FORMAT
    common_flags += -DMDSS_TARGET
endif

common_deps  :=
kernel_includes :=

# Executed only on QCOM BSPs
ifeq ($(call is-vendor-board-platform,QCOM),true)
    common_flags += -DQCOM_BSP
    common_deps += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
    kernel_includes += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
endif
