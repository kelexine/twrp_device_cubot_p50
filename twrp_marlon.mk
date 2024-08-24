#
# Copyright (C) 2024 The Android Open Source Project
# Copyright (C) 2024 SebaUbuntu's TWRP device tree generator
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit from marlon device
$(call inherit-product, device/cubot/marlon/device.mk)

# Inherit some common twrp stuff.
$(call inherit-product, vendor/twrp/config/common.mk)

PRODUCT_DEVICE := marlon
PRODUCT_NAME := twrp_marlon
PRODUCT_BRAND := CUBOT
PRODUCT_MODEL := Marlon
PRODUCT_MANUFACTURER := CUBOT MOBILE LIMITED
PRODUCT_RELEASE_NAME := CUBOT Marlon
