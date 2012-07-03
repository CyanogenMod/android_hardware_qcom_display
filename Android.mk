ifneq ($(filter msm8960,$(TARGET_BOARD_PLATFORM)),)

display-hals := libqcomui
display-hals += libgralloc libgenlock libcopybit libhwcomposer liboverlay liblight

include $(call all-named-subdir-makefiles,$(display-hals))

endif
