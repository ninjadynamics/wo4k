SHELL := cmd.exe
.SHELLFLAGS := /d /s /c
POWERSHELL ?= powershell.exe
TOOLCHAIN ?=
VERSION ?=

.PHONY: all deps runtime controller audio launchers patches test package release clean help
all deps runtime controller audio launchers patches test package release clean:
	$(POWERSHELL) -NoLogo -NoProfile -ExecutionPolicy Bypass -File scripts/Build.ps1 -Target "$@" -CompilerDirectory "$(TOOLCHAIN)" -Version "$(VERSION)"

help:
	@echo make [all^|deps^|runtime^|controller^|audio^|launchers^|patches^|test^|package^|release^|clean]
	@echo Optional: TOOLCHAIN=C:/path/to/mingw64/bin VERSION=0.1.0-beta.16
