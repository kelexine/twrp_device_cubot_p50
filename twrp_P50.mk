#
# Copyright (C) 2024 The Android Open Source Project
# Copyright (C) 2024 SebaUbuntu's TWRP device tree generator
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit some common twrp stuff.
$(call inherit-product, vendor/twrp/config/common.mk)

# Inherit from P50 device
$(call inherit-product, device/cubot/P50/device.mk)

PRODUCT_DEVICE := P50
PRODUCT_NAME := twrp_P50
PRODUCT_BRAND := CUBOT
PRODUCT_MODEL := P50
PRODUCT_MANUFACTURER := cubot

PRODUCT_GMS_CLIENTID_BASE := android-cubot

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRIVATE_BUILD_DESC="full_v956-user 11 RP1A.200720.011 20220816 release-keys"

BUILD_FINGERPRINT := CUBOT/P50_EEA/P50:11/RP1A.200720.011/20220816:user/release-keys
