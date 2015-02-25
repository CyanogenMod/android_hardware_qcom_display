ifeq ($(call my-dir)/$(TARGET_BOARD_PLATFORM),$(call project-path-for,qcom-display))

ifneq ($(filter msm8084 msm8x84,$(TARGET_BOARD_PLATFORM)),)
    #This is for 8084 based platforms
    include $(call all-named-subdir-makefiles,msm8084)
else
ifneq ($(filter msm8974 msm8x74 msm8992 msm8994,$(TARGET_BOARD_PLATFORM)),)
    #This is for 8974 based (and B-family) platforms
    include $(call all-named-subdir-makefiles,msm8974)
else
ifneq ($(filter msm8226 msm8x26,$(TARGET_BOARD_PLATFORM)),)
    include $(call all-named-subdir-makefiles,msm8226)
else
    #This is for 8960 based platforms
    include $(call all-named-subdir-makefiles,msm8960)
endif
endif
endif

endif
