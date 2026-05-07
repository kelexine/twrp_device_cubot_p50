#
# Copyright (C) 2024 The Android Open Source Project
# Copyright (C) 2024 SebaUbuntu's TWRP device tree generator
#
# SPDX-License-Identifier: Apache-2.0
#


# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/base.mk)

# Installs gsi keys into ramdisk, to boot a developer GSI with verified boot.
$(call inherit-product, $(SRC_TARGET_DIR)/product/gsi_keys.mk)

# Inherit from marlon device
$(call inherit-product, device/cubot/marlon/device.mk)

# Inherit some common twrp stuff.
$(call inherit-product, vendor/twrp/config/common.mk)

PRODUCT_DEVICE := marlon
PRODUCT_NAME := twrp_marlon
PRODUCT_BRAND := CUBOT
PRODUCT_MODEL := P50
PRODUCT_MANUFACTURER := CUBOT MOBILE LIMITED
PRODUCT_RELEASE_NAME := CUBOT P50

PRODUCT_BOARD := v956

# Add fingerprint from Stock ROM build.prop
BUILD_FINGERPRINT := "CUBOT/P50_EEA/P50:11/RP1A.200720.011/20220816:user/release-keys"
PRODUCT_BUILD_PROP_OVERRIDES += \
    TARGET_DEVICE=P50_EEA \
    PRODUCT_NAME=P50_EEA \
    PRIVATE_BUILD_DESC="P50_EEA-user 11 RP1A.200720.011 root.20220816 release-keys"
