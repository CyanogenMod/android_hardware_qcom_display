#libs to be built for QCOM targets only

ifeq ($(BOARD_USES_QCOM_HARDWARE),true)
    display-hals := libqcomui libtilerenderer
    display-hals += libhwcomposer liboverlay libgralloc libgenlock libcopybit
    include $(call all-named-subdir-makefiles,$(display-hals))
endif

