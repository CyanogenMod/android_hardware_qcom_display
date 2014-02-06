ifneq ($(filter msm8974 msm8x74 msm8226,$(TARGET_BOARD_PLATFORM)),)
    #This is for 8974 based (and B-family) platforms
    include $(call all-named-subdir-makefiles,msm8974)
else
    #This is for 8960 based platforms
    include $(call all-named-subdir-makefiles,msm8960)
endif

