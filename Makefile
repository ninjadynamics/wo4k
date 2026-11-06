# WO4K build. Requires GNU Make 4.x (mingw32-make) and a MinGW-w64 toolchain
# with GCC, Clang, lld and windres; see HOW-TO-COMPILE.md.
#
#   mingw32-make               build every program
#   mingw32-make test          build and run the automated tests
#   mingw32-make release       build, test and package dist/
#   mingw32-make TOOLCHAIN=C:/mingw64/bin release
#
# Each component describes itself in src/<component>/module.mk.

SHELL := cmd.exe
.SHELLFLAGS := /d /s /c
.DEFAULT_GOAL := all
.SUFFIXES:
.DELETE_ON_ERROR:
# Keep intermediate objects so tests and tools are not rebuilt every run.
.SECONDARY:

include mk/config.mk

# Components append to these.
OUTPUTS :=
TEST_PROGRAMS :=
DEPFILES :=
CLEAN_FILES :=

include $(sort $(wildcard src/*/module.mk)) $(wildcard tests/module.mk) $(wildcard fuzz/module.mk)
include $(wildcard mk/release.mk)

.PHONY: all test clean help
all: $(OUTPUTS)

test: $(patsubst %,%.run,$(TEST_PROGRAMS))

# Running a test is never up to date; the .run target is not a real file.
%.exe.run: %.exe
	@echo Running $(notdir $<)
	@$(call winpath,$<)

# 64-bit objects mirror their source path under build/obj.
$(BUILD)/obj/%.o: %.c
	$(call MKDIR,$(@D))
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/%.res.o: %.rc
	$(call MKDIR,$(@D))
	$(WINDRES) $(RCFLAGS) -i $< -o $@

define newline


endef

clean:
	$(call RMDIR,$(BUILD)/obj)
	$(call RMDIR,$(BUILD)/lib)
	$(call RMDIR,$(BUILD)/tests)
	$(call RMDIR,$(BUILD)/runtime)
	$(call RMDIR,$(BUILD)/test-work)
	$(foreach file,$(CLEAN_FILES),$(call RMFILE,$(file))$(newline))

help:
	@echo Targets: all test clean $(sort $(HELP_TARGETS))
	@echo Options: TOOLCHAIN=C:/path/to/mingw64/bin DEPS_DIR=.work/deps

-include $(DEPFILES)
