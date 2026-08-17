#
# Copyright (C) 2026 The Android Open Source Project
# Author: kelexine <https://github.com/kelexine>
#
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := trustkernel.twrp
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := twrp_tk.c

LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/vendor/bin
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libc

LOCAL_CFLAGS := -Wall -Wextra -Werror

include $(BUILD_EXECUTABLE)
