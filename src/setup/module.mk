# Setup.exe: the installer window, drawn with raylib (built here from its
# pinned source with only the modules Setup needs; see raylib_config.h).

RAYLIB_SRC ?= $(DEPS_DIR)/raylib-5.5/src
RAYLIB_MODULES := rcore rshapes rtextures rtext utils rglfw
RAYLIB_OBJECTS := $(patsubst %,$(BUILD)/obj/raylib/%.o,$(RAYLIB_MODULES))
RAYLIB_LIB := $(BUILD)/lib/libraylib.a
RAYLIB_CFLAGS := -std=gnu99 -O2 -w -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33 \
	-DEXTERNAL_CONFIG_FLAGS -include src/setup/raylib_config.h \
	-I$(RAYLIB_SRC) -I$(RAYLIB_SRC)/external/glfw/include \
	-ffunction-sections -fdata-sections $(HARDENING) -D_WIN32_WINNT=0x0A00

$(BUILD)/obj/raylib/%.o: $(RAYLIB_SRC)/%.c src/setup/raylib_config.h
	$(call MKDIR,$(@D))
	$(CC) $(RAYLIB_CFLAGS) -c $< -o $@

$(RAYLIB_LIB): $(RAYLIB_OBJECTS)
	$(call MKDIR,$(@D))
	$(call RMFILE,$@)
	$(AR) rcs $@ $^

SETUP_SOURCES := $(sort $(wildcard src/setup/*.c))
SETUP_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(SETUP_SOURCES))
SETUP_RESOURCE := $(BUILD)/obj/src/setup/setup.res.o
DEPFILES += $(SETUP_OBJECTS:.o=.d)

$(BUILD)/obj/src/setup/%.o: CFLAGS += -I$(RAYLIB_SRC)
$(SETUP_RESOURCE): src/setup/setup.rc src/common/version.rc src/common/app.manifest \
	$(GENERATED)/setup.ico $(GENERATED)/wo_version.h

Setup.exe: $(SETUP_OBJECTS) $(SETUP_RESOURCE) $(CORE_LIB) $(RAYLIB_LIB)
	$(CC) $(SETUP_OBJECTS) $(SETUP_RESOURCE) $(CORE_LIB) $(RAYLIB_LIB) $(GUI_LDFLAGS) \
		-Wl,--gc-sections $(CORE_SYSLIBS) -lopengl32 -lwinmm -lmsimg32 -o $@

OUTPUTS += Setup.exe
CLEAN_FILES += Setup.exe
HELP_TARGETS += Setup.exe

.PHONY: setup
setup: Setup.exe
