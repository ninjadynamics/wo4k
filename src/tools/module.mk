# Build-time helper (never shipped): icons, generated headers and, later,
# dependency fetching and release packaging.

BUILD_TOOL := $(BUILD)/tools/wo4k-build.exe
BUILD_TOOL_SOURCES := $(sort $(wildcard src/tools/*.c))
BUILD_TOOL_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(BUILD_TOOL_SOURCES))
DEPFILES += $(BUILD_TOOL_OBJECTS:.o=.d)

$(BUILD_TOOL): $(BUILD_TOOL_OBJECTS) $(CORE_LIB)
	$(call MKDIR,$(@D))
	$(CC) $(BUILD_TOOL_OBJECTS) $(CORE_LIB) $(LDFLAGS) -municode -mconsole $(CORE_SYSLIBS) -o $@

# Generated inputs shared by several programs.
$(GENERATED)/setup.ico: assets/icons/setup.png $(BUILD_TOOL)
	$(call winpath,$(BUILD_TOOL)) icon $< $@

$(GENERATED)/play.ico: assets/icons/play.png $(BUILD_TOOL)
	$(call winpath,$(BUILD_TOOL)) icon $< $@

$(GENERATED)/hd_catalog.h: manifests/hd-textures.json $(BUILD_TOOL)
	$(call winpath,$(BUILD_TOOL)) hd-catalog $< $@

.PHONY: tools
tools: $(BUILD_TOOL)
HELP_TARGETS += tools
