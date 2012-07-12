#Enables the listed display HAL modules
#libs to be built for QCOM targets only

ifeq ($(BOARD_USES_QCOM_HARDWARE),true)
display-hals := libgralloc libgenlock libcopybit
display-hals += libhwcomposer liboverlay libqdutils

ifneq ($(TARGET_PROVIDES_LIBLIGHTS),true)
display-hals += liblight
endif

endif

display-hals += libtilerenderer

include $(call all-named-subdir-makefiles,$(display-hals))
