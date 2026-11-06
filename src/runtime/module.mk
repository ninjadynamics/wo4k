# src/runtime: the 32-bit in-process game runtime (runtime/wohr.dll and its
# map) and the 64-bit SDL3 controller host (runtime/wo2097-controller.exe).

# --- 32-bit runtime ---------------------------------------------------------
# The runtime is freestanding: no CRT, hand-made import libraries from
# src/runtime/imports, and three translation units that #include the rest.
X86_BUILD := $(BUILD)/runtime

# MinGW-w64 headers serve both architectures. Standalone MinGW-w64 keeps them
# in ../x86_64-w64-mingw32/include beside the compiler, MSYS2 in ../include.
ifndef X86_INCLUDE
X86_TOOL_DIR := $(if $(BIN),$(BIN),$(dir $(subst \,/,$(firstword $(shell where $(notdir $(CC)) 2>NUL)))))
X86_INCLUDE := $(patsubst %/windows.h,%,$(firstword $(wildcard \
	$(X86_TOOL_DIR)../x86_64-w64-mingw32/include/windows.h \
	$(X86_TOOL_DIR)../include/windows.h)))
endif
x86_include_check = $(if $(X86_INCLUDE),,$(error Cannot find MinGW-w64 windows.h beside $(CC); set X86_INCLUDE=C:/path/to/include))

X86_TARGET := --target=i686-w64-windows-gnu
X86_CFLAGS := $(X86_TARGET) -isystem $(X86_INCLUDE) -Os -fno-builtin \
	-fno-stack-protector -Wall -Wextra -Wno-unused-parameter -Werror \
	-I$(GENERATED)

X86_UNITS := wohd render_patch simulation_patch
X86_OBJECTS := $(patsubst %,$(X86_BUILD)/obj/%.o,$(X86_UNITS))
X86_RES := $(X86_BUILD)/obj/wohr.res
X86_IMPORTS := kernel32 winmm user32 ole32
X86_IMPORT_LIBS := $(patsubst %,$(X86_BUILD)/lib/%.lib,$(X86_IMPORTS))
RUNTIME_DLL := runtime/wohr.dll
RUNTIME_MAP := runtime/wohr.map

$(X86_BUILD)/obj/%.o: src/runtime/%.c
	$(x86_include_check)
	$(call MKDIR,$(@D))
	$(CLANG) $(X86_CFLAGS) -MMD -MP -c $< -o $@

# The HD texture catalog is generated from manifests/hd-textures.json.
$(X86_BUILD)/obj/render_patch.o: $(GENERATED)/hd_catalog.h

$(X86_BUILD)/lib/%.lib: src/runtime/imports/%.def
	$(call MKDIR,$(@D))
	$(DLLTOOL) -m i386 -k -d $< -l $@

# A .res file rather than a windres COFF object: lld-link converts it itself,
# so the DLL keeps its SafeSEH (NO_SEH) marking.
$(X86_RES): src/runtime/wohr.rc src/common/version.rc $(GENERATED)/wo_version.h
	$(call MKDIR,$(@D))
	$(WINDRES) $(filter-out -O coff,$(RCFLAGS)) -O res -i $< -o $@

# ASLR and DEP; a DLL keeps its relocation table (lld-link /fixed:no default).
X86_DLL_LDFLAGS := /nologo /dll /machine:x86 /entry:DllMain@12 /nodefaultlib \
	/dynamicbase /nxcompat

$(RUNTIME_DLL) $(RUNTIME_MAP) &: $(X86_OBJECTS) $(X86_RES) $(X86_IMPORT_LIBS) src/runtime/wohd.def
	$(call MKDIR,runtime)
	$(LLD_LINK) $(X86_DLL_LDFLAGS) /def:src/runtime/wohd.def /map:$(RUNTIME_MAP) \
		/implib:$(X86_BUILD)/wohr.lib /out:$(RUNTIME_DLL) \
		$(X86_OBJECTS) $(X86_RES) $(X86_IMPORT_LIBS)

# --- 64-bit SDL3 controller host --------------------------------------------
SDL3_DIR ?= $(DEPS_DIR)/mingw64
SDL3_PC := $(SDL3_DIR)/lib/pkgconfig/sdl3.pc
SDL3_LIB := $(SDL3_DIR)/lib/libSDL3.a
# Static link flags come from the package's Libs.private. libiconv stays a
# dynamically linked DLL: name its import library explicitly, because the
# -static in LDFLAGS would otherwise resolve -liconv to the static archive.
SDL3_LIBS_PRIVATE := $(if $(wildcard $(SDL3_PC)),$(wordlist 2,999,$(shell findstr /b /c:"Libs.private:" "$(call winpath,$(SDL3_PC))")))
SDL3_STATIC_LIBS := $(patsubst -liconv,$(SDL3_DIR)/lib/libiconv.dll.a,$(SDL3_LIBS_PRIVATE))

CONTROLLER_EXE := runtime/wo2097-controller.exe
CONTROLLER_OBJECTS := $(BUILD)/obj/src/runtime/controller_host.o \
	$(BUILD)/obj/src/runtime/controller.res.o
ICONV_DLL := runtime/libiconv-2.dll

$(BUILD)/obj/src/runtime/controller_host.o: CFLAGS += -isystem $(SDL3_DIR)/include
$(BUILD)/obj/src/runtime/controller.res.o: src/common/version.rc \
	src/common/app.manifest $(GENERATED)/wo_version.h

$(CONTROLLER_EXE): $(CONTROLLER_OBJECTS) $(SDL3_LIB) $(SDL3_PC)
	$(if $(SDL3_STATIC_LIBS),,$(error SDL3 static link flags are missing from $(SDL3_PC)))
	$(call MKDIR,$(@D))
	$(CC) $(GUI_LDFLAGS) $(CONTROLLER_OBJECTS) $(SDL3_LIB) $(SDL3_STATIC_LIBS) -o $@

# The host loads the package's libiconv DLL from its own folder.
$(ICONV_DLL): $(SDL3_DIR)/bin/libiconv-2.dll
	$(call MKDIR,$(@D))
	@copy /y "$(call winpath,$<)" "$(call winpath,$@)" >NUL

.PHONY: runtime controller
runtime: $(RUNTIME_DLL) $(RUNTIME_MAP)
controller: $(CONTROLLER_EXE) $(ICONV_DLL)

# --- Tests (sources in tests/ and tests/runtime) ----------------------------
# 32-bit fixtures compile like the runtime and link its import libraries plus
# the few KERNEL32 functions in tests/runtime/imports/kernel32-test.def. A
# 64-bit driver runs each fixture and checks the result.
X86_TEST_LIB := $(X86_BUILD)/lib/kernel32-test.lib
X86_TEST_LDFLAGS := /nologo /machine:x86 /subsystem:console \
	/entry:test_entry@0 /nodefaultlib
CRASH_FIXTURE := $(BUILD)/tests/crash_fixture.exe
RENDER_FIXTURE := $(BUILD)/tests/render_init_fixture.exe
X86_TEST_OBJECTS := $(X86_BUILD)/tests/test_crash_diagnostics.o \
	$(X86_BUILD)/tests/test_render_init.o
RUNTIME_TEST_CRASH := $(BUILD)/tests/run_crash_diagnostics.exe
RUNTIME_TEST_RENDER := $(BUILD)/tests/run_render_init.exe

$(X86_TEST_LIB): tests/runtime/imports/kernel32-test.def
	$(call MKDIR,$(@D))
	$(DLLTOOL) -m i386 -k -d $< -l $@

$(X86_BUILD)/tests/%.o: tests/%.c
	$(x86_include_check)
	$(call MKDIR,$(@D))
	$(CLANG) $(X86_CFLAGS) -MMD -MP -c $< -o $@

$(X86_BUILD)/tests/test_render_init.o: $(GENERATED)/hd_catalog.h

$(CRASH_FIXTURE): $(X86_BUILD)/tests/test_crash_diagnostics.o \
	$(X86_BUILD)/lib/kernel32.lib $(X86_TEST_LIB)
	$(call MKDIR,$(@D))
	$(LLD_LINK) $(X86_TEST_LDFLAGS) /out:$@ $^

# The renderer fixture keeps the fixed base its PowerShell build used.
$(RENDER_FIXTURE): $(X86_BUILD)/tests/test_render_init.o \
	$(X86_BUILD)/lib/kernel32.lib $(X86_TEST_LIB) \
	$(X86_BUILD)/lib/user32.lib $(X86_BUILD)/lib/ole32.lib
	$(call MKDIR,$(@D))
	$(LLD_LINK) $(X86_TEST_LDFLAGS) /base:0x10000000 /dynamicbase:no /out:$@ $^

$(RUNTIME_TEST_CRASH): $(BUILD)/obj/tests/runtime/run_crash_diagnostics.o
	$(call MKDIR,$(@D))
	$(CC) $(CONSOLE_LDFLAGS) $^ -o $@

$(RUNTIME_TEST_RENDER): $(BUILD)/obj/tests/runtime/run_render_init.o
	$(call MKDIR,$(@D))
	$(CC) $(CONSOLE_LDFLAGS) $^ -o $@

$(RUNTIME_TEST_CRASH).run: $(CRASH_FIXTURE)
$(RUNTIME_TEST_RENDER).run: $(RENDER_FIXTURE)

TEST_PROGRAMS += $(RUNTIME_TEST_CRASH) $(RUNTIME_TEST_RENDER)

OUTPUTS += $(RUNTIME_DLL) $(RUNTIME_MAP) $(CONTROLLER_EXE) $(ICONV_DLL)
DEPFILES += $(X86_OBJECTS:.o=.d) $(X86_TEST_OBJECTS:.o=.d) \
	$(BUILD)/obj/src/runtime/controller_host.d \
	$(BUILD)/obj/tests/runtime/run_crash_diagnostics.d \
	$(BUILD)/obj/tests/runtime/run_render_init.d
CLEAN_FILES += $(RUNTIME_DLL) $(RUNTIME_MAP) $(CONTROLLER_EXE) $(ICONV_DLL)
HELP_TARGETS += runtime controller
