# SPDX-License-Identifier: GPL-3.0-only
# ZEsarPCW -- Copyright (c) 2026 retrodiv <retrodiv@proton.me>. GNU GPL v3; see LICENSE.
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

ifeq (,$(filter $(TARGET_ARCH_ABI),arm64-v8a armeabi-v7a))
  $(error ZEsarPCW ndk-build supports arm64-v8a and armeabi-v7a)
endif

CORE_DIR := $(LOCAL_PATH)/../src
PLATFORM_SRC :=
include $(LOCAL_PATH)/../Makefile.common

# The libretro buildbot expects libs/<abi>/libretro.so.
LOCAL_MODULE := retro
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
  LOCAL_ARM_NEON := true
endif
LOCAL_SRC_FILES := $(patsubst $(LOCAL_PATH)/%,%,$(SOURCES_C))
LOCAL_C_INCLUDES := $(patsubst -I%,%,$(INCFLAGS))
LOCAL_CFLAGS := $(CORE_DEFINES) $(CORE_COMMON_CFLAGS)
LOCAL_LDFLAGS := -Wl,-Bsymbolic,--gc-sections,--no-undefined,-z,relro,-z,now,--version-script=$(CORE_DIR)/libretro/libretro.exports
LOCAL_LDFLAGS += -Wl,-z,max-page-size=16384,-z,common-page-size=16384
LOCAL_LDLIBS := -lm
include $(BUILD_SHARED_LIBRARY)
