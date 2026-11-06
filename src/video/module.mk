# src/video: runtime/wo4k-video.exe, the native movie player. The installer
# copies it beside the game as wobble/wo4k-video.exe; src/video/main.c
# documents its command line.

VIDEO_EXE := runtime/wo4k-video.exe
VIDEO_CORE_OBJECTS := $(BUILD)/obj/src/video/video_core.o
VIDEO_OBJECTS := $(VIDEO_CORE_OBJECTS) \
	$(patsubst %,$(BUILD)/obj/src/video/%.o,main video_backend video_dshow \
		video_evr video_input video_log video_mf video_window) \
	$(BUILD)/obj/src/video/video.res.o
# Media Foundation (mfplat.dll, mf.dll) is loaded at run time from System32,
# so only GUID libraries and always-present system DLLs are linked.
VIDEO_LIBS := -lstrmiids -lmfuuid -lole32 -loleaut32 -luuid -lshell32 \
	-luser32 -lgdi32

$(BUILD)/obj/src/video/video.res.o: src/common/version.rc \
	src/common/app.manifest $(GENERATED)/wo_version.h

$(VIDEO_EXE): $(VIDEO_OBJECTS)
	$(call MKDIR,$(@D))
	$(CC) $(GUI_LDFLAGS) $(VIDEO_OBJECTS) $(VIDEO_LIBS) -o $@

.PHONY: video
video: $(VIDEO_EXE)

# --- Tests (sources in tests/video) -----------------------------------------
# test_video_core: unit tests of the pure helpers.
# test_video_window: the player window's Z order, with invisible windows.
# test_video_probe: drives the built player; its playback cases need movie
# files from WO4K_TEST_MOVIES or wobble/movies and skip without them.
VIDEO_TEST_CORE := $(BUILD)/tests/test_video_core.exe
VIDEO_TEST_WINDOW := $(BUILD)/tests/test_video_window.exe
VIDEO_TEST_PROBE := $(BUILD)/tests/test_video_probe.exe
VIDEO_TEST_OBJECTS := $(BUILD)/obj/tests/video/test_video_core.o \
	$(BUILD)/obj/tests/video/test_video_window.o \
	$(BUILD)/obj/tests/video/test_video_probe.o

$(VIDEO_TEST_CORE): $(BUILD)/obj/tests/video/test_video_core.o $(VIDEO_CORE_OBJECTS)
	$(call MKDIR,$(@D))
	$(CC) $(CONSOLE_LDFLAGS) $^ -o $@

$(VIDEO_TEST_WINDOW): $(BUILD)/obj/tests/video/test_video_window.o \
	$(BUILD)/obj/src/video/video_window.o
	$(call MKDIR,$(@D))
	$(CC) $(CONSOLE_LDFLAGS) $^ -luser32 -lgdi32 -o $@

$(VIDEO_TEST_PROBE): $(BUILD)/obj/tests/video/test_video_probe.o $(VIDEO_CORE_OBJECTS)
	$(call MKDIR,$(@D))
	$(CC) $(CONSOLE_LDFLAGS) $^ -o $@

$(VIDEO_TEST_PROBE).run: $(VIDEO_EXE)

TEST_PROGRAMS += $(VIDEO_TEST_CORE) $(VIDEO_TEST_WINDOW) $(VIDEO_TEST_PROBE)

OUTPUTS += $(VIDEO_EXE)
DEPFILES += $(patsubst %.o,%.d,$(filter-out %.res.o,$(VIDEO_OBJECTS)) \
	$(VIDEO_TEST_OBJECTS))
CLEAN_FILES += $(VIDEO_EXE)
HELP_TARGETS += video
