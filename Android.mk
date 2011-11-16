# Build only new gralloc

ifeq ($(TARGET_USES_ION),true)
    display-hals := libgralloc
    include $(call all-named-subdir-makefiles,$(display-hals))
endif

