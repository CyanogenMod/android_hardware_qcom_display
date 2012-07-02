#Enables the listed display HAL modules
#libs to be built for QCOM targets only

ifeq ($(call is-vendor-board-platform,QCOM),true)
display-hals := libgralloc libgenlock libcopybit
display-hals +=libhwcomposer liboverlay libqdutils
endif

include $(call all-named-subdir-makefiles,$(display-hals))
