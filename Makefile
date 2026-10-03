# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 retrodiv <retrodiv@proton.me>
# ZEsarPCW -- Amstrad PCW libretro core (derived from ZEsarUX 13.0, GPLv3).
# Standard libretro build: make platform=<unix|linux-*|osx|win32|win64|android[-armv7]>.

TARGET_NAME := zesarpcw_libretro
CORE_DIR    := src

ifeq ($(platform),)
  platform = unix
  ifeq ($(shell uname -s),Darwin)
    platform = osx
  endif
  ifneq ($(findstring MINGW,$(shell uname -s)),)
    platform = win
  endif
  ifneq ($(findstring MSYS,$(shell uname -s)),)
    platform = win
  endif
endif

# Select a platform default only when the caller did not choose a compiler.
DEFAULT_CC := gcc
CORE_PLATFORM_FLAGS :=
fpic      := -fPIC
PLATFORM_SRC :=
EXTRA_CFLAGS :=
LIBS         := -lm

ifneq (,$(findstring unix,$(platform)))
  TARGET := $(TARGET_NAME).so
  SHARED := -shared -Wl,-Bsymbolic,--gc-sections,--no-undefined,-z,relro,-z,now,--version-script=src/libretro/libretro.exports
else ifneq (,$(findstring linux,$(platform)))
  TARGET := $(TARGET_NAME).so
  SHARED := -shared -Wl,-Bsymbolic,--gc-sections,--no-undefined,-z,relro,-z,now,--version-script=src/libretro/libretro.exports
else ifneq (,$(findstring osx,$(platform)))
  DEFAULT_CC := clang
  TARGET := $(TARGET_NAME).dylib
  SHARED := -dynamiclib -Wl,-dead_strip,-exported_symbols_list,src/libretro/libretro.exports.macho
  LIBS   := -lm
  ifeq ($(CROSS_COMPILE),1)
    ifeq ($(strip $(LIBRETRO_APPLE_PLATFORM)),)
      $(error CROSS_COMPILE=1 requires LIBRETRO_APPLE_PLATFORM)
    endif
    ifeq ($(strip $(LIBRETRO_APPLE_ISYSROOT)),)
      $(error CROSS_COMPILE=1 requires LIBRETRO_APPLE_ISYSROOT)
    endif
    CORE_PLATFORM_FLAGS := -target $(LIBRETRO_APPLE_PLATFORM) -isysroot "$(LIBRETRO_APPLE_ISYSROOT)"
  else ifneq (,$(findstring arm64,$(platform)))
    CORE_PLATFORM_FLAGS := -arch arm64
  else ifneq (,$(findstring x86_64,$(platform)))
    CORE_PLATFORM_FLAGS := -arch x86_64
  endif
else ifneq (,$(findstring win,$(platform)))
  DEFAULT_CC := x86_64-w64-mingw32-gcc
  ifneq (,$(filter win32 windows-x86 win-i686,$(platform)))
    DEFAULT_CC := i686-w64-mingw32-gcc
  endif
  TARGET := $(TARGET_NAME).dll
  SHARED := -shared -static-libgcc -Wl,-Bsymbolic,--gc-sections,--no-undefined,--no-insert-timestamp,--exclude-all-symbols
  EXTRA_CFLAGS := -DMINGW -include $(CORE_DIR)/libretro/win_compat.h
  PLATFORM_SRC := libretro/win_posix_compat.c
  LIBS   := -static -static-libgcc -lm
else ifneq (,$(findstring android,$(platform)))
  TARGET := $(TARGET_NAME).so
  SHARED := -shared -Wl,-Bsymbolic,--gc-sections,--no-undefined,-z,relro,-z,now,--version-script=src/libretro/libretro.exports -Wl,-z,max-page-size=16384,-z,common-page-size=16384
  LIBS   := -lm
  PLATFORM_SRC :=
  ifeq ($(platform),android-armv7)
    CORE_PLATFORM_FLAGS := -march=armv7-a -mfpu=neon -mfloat-abi=softfp
  endif
else
  TARGET := $(TARGET_NAME).so
  SHARED := -shared -Wl,-Bsymbolic,--gc-sections,--no-undefined
endif

# The i686 buildbot sets ARCH=x86 with platform=unix. Explicit platform names
# also select 32-bit code for contributor builds on an x86-64 Linux host.
ifneq (,$(findstring unix,$(platform))$(findstring linux,$(platform)))
  ifneq (,$(filter linux-x86 linux-i686 unix-x86,$(platform))$(filter x86,$(ARCH)))
    CORE_PLATFORM_FLAGS += -m32
  endif
  ifneq (,$(filter linux-armv7 linux-armhf,$(platform)))
    CORE_PLATFORM_FLAGS += -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard
  endif
endif

ifneq (,$(filter default undefined,$(origin CC)))
  CC := $(DEFAULT_CC)
endif

include Makefile.common

CFLAGS ?= -O2
CORE_CPPFLAGS := $(CORE_DEFINES) $(EXTRA_CFLAGS) $(INCFLAGS) $(CORE_PLATFORM_FLAGS)
CORE_CFLAGS := $(CORE_COMMON_CFLAGS) -MMD -MP $(fpic)

BUILD_TAG := $(subst /,_,$(subst :,_,$(platform)))
OBJDIR := build/$(BUILD_TAG)/obj
OBJECTS := $(addprefix $(OBJDIR)/,$(SOURCES_C:.c=.o))
BUILD_TARGET := $(OBJDIR)/$(TARGET)
DEPS := $(OBJECTS:.o=.d)
GLUE_SOURCES := $(LIBRETRO_SRC) $(PLATFORM_SRC)
GLUE_OBJECTS := $(addprefix $(OBJDIR)/,$(addprefix $(CORE_DIR)/,$(GLUE_SOURCES:.c=.o)))
UPSTREAM_OBJECTS := $(filter-out $(GLUE_OBJECTS),$(OBJECTS))

# New port glue is maintained with the full warning set.  The retained ZEsarUX
# 13.0 dependency closure still contains many PATH_MAX-to-PATH_MAX sprintf()
# operations which GCC diagnoses from the theoretical input bounds. Keep that
# one inherited diagnostic scoped to upstream objects; every other diagnostic
# remains fatal for the complete core on every target.
$(GLUE_OBJECTS): CORE_CFLAGS += -Wall -Wextra
# ZEsarUX predates GCC's -fno-common default.  Building every unit that way gives
# each global its own data section, allowing --gc-sections to discard unreachable
# foreign-machine state instead of retaining an entire common allocation pool.
ifeq (,$(findstring clang,$(notdir $(CC))))
$(UPSTREAM_OBJECTS): CORE_CFLAGS += -Wno-format-overflow
endif

all: $(TARGET)

# Record effective caller settings. Keep link-only changes out of object rebuilds.
build_quote = '$(subst ','"'"',$(1))'
COMPILE_CONFIG := $(OBJDIR)/.compile-config
LINK_CONFIG := $(OBJDIR)/.link-config
COMPILE_SETTINGS := $(foreach name,CC CPPFLAGS CORE_CPPFLAGS CFLAGS CORE_CFLAGS MACOSX_DEPLOYMENT_TARGET SDKROOT PATH CPATH C_INCLUDE_PATH,$(call build_quote,$(name)=$($(name))))
LINK_SETTINGS := $(foreach name,CC fpic SHARED LDFLAGS CORE_PLATFORM_FLAGS LDLIBS LIBS OBJECTS MACOSX_DEPLOYMENT_TARGET SDKROOT PATH LIBRARY_PATH,$(call build_quote,$(name)=$($(name))))

$(COMPILE_CONFIG): FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' $(COMPILE_SETTINGS) > $@.tmp
	@cmp -s $@.tmp $@ || cp $@.tmp $@

$(LINK_CONFIG): FORCE
	@mkdir -p $(dir $@)
	@printf '%s\n' $(LINK_SETTINGS) > $@.tmp
	@cmp -s $@.tmp $@ || cp $@.tmp $@

check: $(TARGET)
	python3 check.py --core ./$(TARGET) --platform $(platform)

$(TARGET): $(BUILD_TARGET) FORCE
	cp $< $@

$(BUILD_TARGET): $(OBJECTS) $(LINK_CONFIG) src/libretro/libretro.exports src/libretro/libretro.exports.macho
	$(CC) $(fpic) $(SHARED) $(LDFLAGS) $(CORE_PLATFORM_FLAGS) $(OBJECTS) -o $@ $(LDLIBS) $(LIBS)
	@echo "built $@  (platform=$(platform))"

$(OBJECTS): $(COMPILE_CONFIG) Makefile Makefile.common $(CORE_DIR)/libretro/libretro_sources.mk

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CORE_CPPFLAGS) $(CFLAGS) $(CORE_CFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -rf build
	find $(CORE_DIR) -type f \( -name '*.o' -o -name '*.d' \) -delete
	rm -f *.o $(TARGET_NAME).so $(TARGET_NAME).dll $(TARGET_NAME).dylib

# The public filename is shared by several platforms. Always restore it from
# the selected platform's library, even when that platform needs no rebuild.
FORCE:

.PHONY: all check clean FORCE
