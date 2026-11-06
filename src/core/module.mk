# Shared core library (64-bit): portable parsers and the Windows platform
# layer (*_win.c). Every program and test links build/lib/libwocore.a.

CORE_SOURCES := $(sort $(wildcard src/core/*.c))
CORE_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(CORE_SOURCES)) \
	$(BUILD)/obj/third_party/miniz/miniz.o
CORE_LIB := $(BUILD)/lib/libwocore.a
DEPFILES += $(CORE_OBJECTS:.o=.d)
HELP_TARGETS += core

# Must match src/core/wo_miniz.h.
MINIZ_DEFINES := -DMINIZ_NO_STDIO -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES

# miniz is vendored unmodified, so it keeps its own warning level.
$(BUILD)/obj/third_party/miniz/miniz.o: CFLAGS := -std=c11 -O2 -w $(MINIZ_DEFINES) \
	$(WIN_DEFINES) $(HARDENING) -Ithird_party

$(CORE_LIB): $(CORE_OBJECTS)
	$(call MKDIR,$(@D))
	$(call RMFILE,$@)
	$(AR) rcs $@ $^

.PHONY: core
core: $(CORE_LIB)
