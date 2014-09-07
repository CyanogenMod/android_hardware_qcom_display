ifeq ($(TARGET_QCOM_DISPLAY_VARIANT),)
ifneq ($(filter msm8974 msm8x74,$(TARGET_BOARD_PLATFORM)),)
    #This is for 8974 based platforms
    include $(call all-named-subdir-makefiles,msm8974)
else
ifneq ($(filter msm8226 msm8610,$(TARGET_BOARD_PLATFORM)),)
    #This is for 8226 and 8610 based platforms
    include $(call all-named-subdir-makefiles,msm8974)
else
    #This is for 8960 based platforms
    include $(call all-named-subdir-makefiles,msm8960)
endif
endif
endif
