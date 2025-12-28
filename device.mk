#
# Copyright (C) 2024 The Android Open Source Project
# Copyright (C) 2024 SebaUbuntu's TWRP device tree generator
#
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := device/cubot/marlon

# Dynamic Partitions
PRODUCT_USE_DYNAMIC_PARTITIONS := true

# Minimal Boot Control HAL
PRODUCT_PACKAGES += \
    android.hardware.boot@1.1-mtkimpl.recovery \
    bootctl

# Fastbootd
PRODUCT_PACKAGES += \
    android.hardware.fastboot@1.0-impl-mock \
    fastbootd

# Recovery packages
PRODUCT_PACKAGES += \
    update_engine_sideload \
    libSoftGatekeeper

# VNDK
PRODUCT_TARGET_VNDK_VERSION := 30

# API
PRODUCT_SHIPPING_API_LEVEL := 30

# Health HAL
PRODUCT_PACKAGES += \
    android.hardware.health@2.1-impl \
    android.hardware.health@2.1-service \
    libhealthd.$(PRODUCT_PLATFORM)

# OEM otacerts
PRODUCT_EXTRA_RECOVERY_KEYS += \
    $(LOCAL_PATH)/security/cubot
