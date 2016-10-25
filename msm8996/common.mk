#Common headers
display_top := $(call my-dir)

use_hwc2 := false
ifeq ($(TARGET_USES_HWC2), true)
    use_hwc2 := true
endif

common_includes := $(display_top)/libqdutils
common_includes += $(display_top)/libqservice
common_includes += $(display_top)/libcopybit
common_includes += $(display_top)/sdm/include

common_header_export_path := qcom/display

#Common libraries external to display HAL
common_libs := liblog libutils libcutils libhardware

#Common C flags
common_flags := -DDEBUG_CALC_FPS -Wno-missing-field-initializers
common_flags += -Wconversion -Wall -Werror
ifneq ($(TARGET_USES_GRALLOC1), true)
    common_flags += -isystem $(display_top)/libgralloc
else
    common_flags += -isystem $(display_top)/libgralloc1
endif

ifeq ($(TARGET_USES_POST_PROCESSING),true)
    common_flags     += -DUSES_POST_PROCESSING
    common_includes  += $(TARGET_OUT_HEADERS)/pp/inc
endif

ifeq ($(ARCH_ARM_HAVE_NEON),true)
    common_flags += -D__ARM_HAVE_NEON
endif

ifeq ($(call is-board-platform-in-list, $(MSM_VIDC_TARGET_LIST)), true)
    common_flags += -DVENUS_COLOR_FORMAT
endif

ifeq ($(call is-board-platform-in-list, $(MASTER_SIDE_CP_TARGET_LIST)), true)
    common_flags += -DMASTER_SIDE_CP
endif

common_deps  :=
kernel_includes :=

# Executed only on QCOM BSPs
# ifeq ($(TARGET_USES_QCOM_BSP),true)
# Enable QCOM Display features
#    common_flags += -DQTI_BSP
# endif

ifeq ($(TARGET_COMPILE_WITH_MSM_KERNEL),true)
# This check is to pick the kernel headers from the right location.
# If the macro above is defined, we make the assumption that we have the kernel
# available in the build tree.
# If the macro is not present, the headers are picked from hardware/qcom/msmXXXX
# failing which, they are picked from bionic.
    common_deps += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
    kernel_includes += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
endif
