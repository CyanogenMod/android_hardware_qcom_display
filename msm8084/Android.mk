ifneq ($(filter msm8960,$(TARGET_BOARD_PLATFORM)),)

display-hals := libgralloc libgenlock libcopybit liblight
display-hals +=libhwcomposer liboverlay libqdutils

include $(call all-named-subdir-makefiles,$(display-hals))
endif
