# Shared toolchain, version and flag configuration. Included by the Makefile.

# Optional directory holding gcc.exe, clang.exe, lld-link.exe, llvm-dlltool.exe
# and windres.exe. Empty means "find them on PATH".
TOOLCHAIN ?=
BIN := $(if $(strip $(TOOLCHAIN)),$(patsubst %/,%,$(subst \,/,$(TOOLCHAIN)))/,)
CC := $(BIN)gcc
CLANG := $(BIN)clang
LLD_LINK := $(BIN)lld-link
DLLTOOL := $(BIN)llvm-dlltool
WINDRES := $(BIN)windres
AR := $(BIN)ar

BUILD := build
GENERATED := $(BUILD)/generated
# Third-party archives fetched by "make deps" (SDL3, DDrawCompat).
DEPS_DIR ?= .work/deps

# Windows path helpers for cmd.exe recipes.
winpath = $(subst /,\,$(1))
MKDIR = @if not exist "$(call winpath,$(1))" mkdir "$(call winpath,$(1))"
RMFILE = @if exist "$(call winpath,$(1))" del /f /q "$(call winpath,$(1))"
RMDIR = @if exist "$(call winpath,$(1))" rmdir /s /q "$(call winpath,$(1))"

# --- Version: the VERSION file is the single source of truth ---------------
VERSION_FILE := $(strip $(file <VERSION))
VERSION ?= $(VERSION_FILE)
override VERSION := $(patsubst v%,%,$(strip $(VERSION)))
ifneq ($(VERSION),$(VERSION_FILE))
$(error VERSION=$(VERSION) does not match the VERSION file ($(VERSION_FILE)). Update VERSION instead)
endif
strip_digits = $(subst 0,,$(subst 1,,$(subst 2,,$(subst 3,,$(subst 4,,$(subst 5,,$(subst 6,,$(subst 7,,$(subst 8,,$(subst 9,,$(1)))))))))))
is_number = $(if $(strip $(1)),$(if $(call strip_digits,$(1)),,yes),)
_version_split := $(subst -, ,$(VERSION))
_version_core := $(subst ., ,$(word 1,$(_version_split)))
VERSION_PRERELEASE := $(word 2,$(_version_split))
VERSION_MAJOR := $(word 1,$(_version_core))
VERSION_MINOR := $(word 2,$(_version_core))
VERSION_PATCH := $(word 3,$(_version_core))
# The numeric fourth field is the prerelease number (beta.24 -> 24), else 0.
VERSION_BUILD := $(if $(VERSION_PRERELEASE),$(lastword $(subst ., ,$(VERSION_PRERELEASE))),0)
ifneq ($(words $(_version_core)) $(words $(_version_split)),3 $(if $(VERSION_PRERELEASE),2,1))
$(error Invalid VERSION "$(VERSION)"; expected MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-label.N)
endif
ifneq ($(call is_number,$(VERSION_MAJOR))$(call is_number,$(VERSION_MINOR))$(call is_number,$(VERSION_PATCH))$(call is_number,$(VERSION_BUILD)),yesyesyesyes)
$(error Invalid VERSION "$(VERSION)"; numeric fields must be decimal)
endif

# Generated headers are rewritten only when their content changes, so a
# rebuild never happens merely because make was invoked again.
$(shell if not exist "$(call winpath,$(GENERATED))" mkdir "$(call winpath,$(GENERATED))")
define WO_VERSION_H_TEXT
/* Generated from VERSION by mk/config.mk. Do not edit. */
#ifndef WO_VERSION_H
#define WO_VERSION_H
#define WO_VERSION_STRING "$(VERSION)"
#define WO_VERSION_MAJOR $(VERSION_MAJOR)
#define WO_VERSION_MINOR $(VERSION_MINOR)
#define WO_VERSION_PATCH $(VERSION_PATCH)
#define WO_VERSION_BUILD $(VERSION_BUILD)
#define WO_VERSION_NUMERIC $(VERSION_MAJOR),$(VERSION_MINOR),$(VERSION_PATCH),$(VERSION_BUILD)
#define WO_VERSION_IS_PRERELEASE $(if $(VERSION_PRERELEASE),1,0)
#endif
endef
ifneq ($(file <$(GENERATED)/wo_version.h),$(WO_VERSION_H_TEXT))
$(file >$(GENERATED)/wo_version.h,$(WO_VERSION_H_TEXT))
endif

# --- Flags for the 64-bit programs and the shared core library -------------
WIN_DEFINES := -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
	-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DCOBJMACROS
WARNINGS := -Wall -Wextra -Werror -Wshadow -Wformat=2 -Wvla \
	-Wimplicit-fallthrough -Wstrict-prototypes -Wmissing-prototypes \
	-Wpointer-arith -Wcast-qual -Wundef
HARDENING := -fstack-protector-strong -D_FORTIFY_SOURCE=2
CFLAGS := -std=c11 -O2 $(WARNINGS) $(WIN_DEFINES) $(HARDENING) \
	-I$(GENERATED) -Isrc -Ithird_party
# ASLR (including high-entropy) and DEP for every 64-bit image.
LDFLAGS := -static -s -Wl,--dynamicbase,--nxcompat,--high-entropy-va
GUI_LDFLAGS := $(LDFLAGS) -municode -mwindows
CONSOLE_LDFLAGS := $(LDFLAGS) -mconsole
RCFLAGS := -I$(GENERATED) -Isrc/common -O coff

# System libraries used by the core library (all part of Windows).
CORE_SYSLIBS := -lwinhttp -lbcrypt -lcrypt32 -lwintrust -lwindowscodecs \
	-lole32 -loleaut32 -luuid -lshell32 -lshlwapi -luser32 -lgdi32 \
	-lversion -ladvapi32
