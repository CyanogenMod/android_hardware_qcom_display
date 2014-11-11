#Common headers
common_includes := $(call project-path-for,qcom-display)/$(TARGET_BOARD_PLATFORM)/libgralloc
common_includes += $(call project-path-for,qcom-display)/$(TARGET_BOARD_PLATFORM)/liboverlay
common_includes += $(call project-path-for,qcom-display)/$(TARGET_BOARD_PLATFORM)/libcopybit
common_includes += $(call project-path-for,qcom-display)/$(TARGET_BOARD_PLATFORM)/libqdutils
common_includes += $(call project-path-for,qcom-display)/$(TARGET_BOARD_PLATFORM)/libhwcomposer
common_includes += $(call project-path-for,qcom-display)/$(TARGET_BOARD_PLATFORM)/libexternal
common_includes += $(call project-path-for,qcom-display)/$(TARGET_BOARD_PLATFORM)/libqservice
common_includes += $(call project-path-for,qcom-display)/$(TARGET_BOARD_PLATFORM)/libvirtual
common_includes += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include

ifeq ($(TARGET_USES_POST_PROCESSING),true)
    common_flags     += -DUSES_POST_PROCESSING
    common_includes += $(TARGET_OUT_HEADERS)/pp/inc
endif

common_header_export_path := qcom/display

#Common libraries external to display HAL
common_libs := liblog libutils libcutils libhardware

ifeq ($(TARGET_USES_POST_PROCESSING),true)
    common_libs += libmm-abl
endif

#Common C flags
common_flags := -DDEBUG_CALC_FPS -Wno-missing-field-initializers
common_flags += -Werror -Wno-error=unused-parameter

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
ifeq ($(TARGET_USES_QCOM_BSP),true)
# This flag is used to compile out any features that depend on framework changes
    common_flags += -DQCOM_BSP
endif
ifneq (,$(filter $(QCOM_BOARD_PLATFORMS),$(TARGET_BOARD_PLATFORM)))
# This check is to pick the kernel headers from the right location.
# If the macro above is defined, we make the assumption that we have the kernel
# available in the build tree.
# If the macro is not present, the headers are picked from hardware/qcom/msmXXXX
# failing which, they are picked from bionic.
    common_deps += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
    kernel_includes += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
endif

ifeq ($(TARGET_DISPLAY_USE_RETIRE_FENCE),true)
    common_flags += -DUSE_RETIRE_FENCE
endif
