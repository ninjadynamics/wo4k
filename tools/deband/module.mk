# Maintainer tool (never shipped): the debander and lossless PNG trimmer.
#
#   make deband        tools/deband/deband.exe, with its pinned libraries
#   make deband-test   pngtrim tests, decoding every result with libpng
#
# libpng is compiled from source with this toolchain; libjpeg-turbo and zlib
# are MSYS2's static UCRT64 builds. All three are pinned in
# tools/deband/dependencies.json and fetched into $(DEPS_DIR).

DEBAND_EXE := tools/deband/deband.exe
DEBAND_TEST := $(BUILD)/tools/pngtrim_test.exe
DEBAND_OBJ := $(BUILD)/obj/tools/deband
LIBPNG_DIR := $(DEPS_DIR)/libpng-1.6.59
DEBAND_UCRT := $(DEPS_DIR)/ucrt64
LIBPNG_LIB := $(BUILD)/lib/libpng16.a
DEBAND_MARKERS := $(LIBPNG_DIR)/png.h $(DEBAND_UCRT)/lib/libjpeg.a $(DEBAND_UCRT)/lib/libz.a
DEBAND_LIBS := $(LIBPNG_LIB) $(DEBAND_UCRT)/lib/libjpeg.a $(DEBAND_UCRT)/lib/libz.a

LIBPNG_NAMES := png pngerror pngget pngmem pngpread pngread pngrio pngrtran pngrutil pngset \
	pngtrans pngwio pngwrite pngwtran pngwutil
LIBPNG_OBJECTS := $(patsubst %,$(DEBAND_OBJ)/libpng/%.o,$(LIBPNG_NAMES))
LIBPNG_CFLAGS := -std=c99 -O2 -I$(DEBAND_OBJ)/libpng -I$(LIBPNG_DIR) -I$(DEBAND_UCRT)/include
DEBAND_CFLAGS := -std=c99 -O3 -Wall -Wextra -Werror -D__USE_MINGW_ANSI_STDIO=1 $(HARDENING) \
	-I$(DEBAND_OBJ)/libpng -I$(LIBPNG_DIR) -I$(DEBAND_UCRT)/include

.PHONY: deband deband-test
HELP_TARGETS += deband deband-test
CLEAN_FILES += $(DEBAND_EXE)
DEPFILES += $(DEBAND_OBJ)/deband.d $(DEBAND_OBJ)/pngtrim.d $(DEBAND_OBJ)/pngtrim_test.d

deband: $(DEBAND_EXE)

deband-test: $(DEBAND_TEST)
	$(call winpath,$(DEBAND_TEST))

# zstd from the main dependencies unpacks the MSYS2 packages.
$(DEBAND_MARKERS) &: | $(BUILD_TOOL)
	$(call winpath,$(BUILD_TOOL)) fetch manifests/dependencies.json $(DEPS_DIR)
	$(call winpath,$(BUILD_TOOL)) fetch tools/deband/dependencies.json $(DEPS_DIR)

$(DEBAND_OBJ)/libpng/pnglibconf.h: | $(DEBAND_MARKERS)
	$(call MKDIR,$(@D))
	copy /y "$(call winpath,$(LIBPNG_DIR)/scripts/pnglibconf.h.prebuilt)" "$(call winpath,$@)" >nul

$(DEBAND_OBJ)/libpng/%.o: $(LIBPNG_DIR)/%.c $(DEBAND_OBJ)/libpng/pnglibconf.h
	$(call MKDIR,$(@D))
	$(CC) $(LIBPNG_CFLAGS) -c $< -o $@

$(LIBPNG_LIB): $(LIBPNG_OBJECTS)
	$(call MKDIR,$(@D))
	$(AR) rcs $@ $^

$(DEBAND_OBJ)/deband.o $(DEBAND_OBJ)/pngtrim.o $(DEBAND_OBJ)/pngtrim_test.o: \
		$(DEBAND_OBJ)/%.o: tools/deband/%.c $(DEBAND_OBJ)/libpng/pnglibconf.h
	$(call MKDIR,$(@D))
	$(CC) $(DEBAND_CFLAGS) -MMD -MP -c $< -o $@

$(DEBAND_EXE): $(DEBAND_OBJ)/deband.o $(DEBAND_OBJ)/pngtrim.o $(DEBAND_LIBS)
	$(CC) $^ $(LDFLAGS) -o $@ -lm

$(DEBAND_TEST): $(DEBAND_OBJ)/pngtrim_test.o $(DEBAND_OBJ)/pngtrim.o $(DEBAND_LIBS)
	$(call MKDIR,$(@D))
	$(CC) $^ $(LDFLAGS) -o $@ -lm
