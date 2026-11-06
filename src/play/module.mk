# Play.exe: check/refresh the installation, start the game and keep a log.

PLAY_SOURCES := $(sort $(wildcard src/play/*.c))
PLAY_OBJECTS := $(patsubst %.c,$(BUILD)/obj/%.o,$(PLAY_SOURCES))
PLAY_RESOURCE := $(BUILD)/obj/src/play/play.res.o
DEPFILES += $(PLAY_OBJECTS:.o=.d)

$(PLAY_RESOURCE): src/play/play.rc src/common/version.rc src/common/app.manifest \
	$(GENERATED)/play.ico $(GENERATED)/wo_version.h

Play.exe: $(PLAY_OBJECTS) $(PLAY_RESOURCE) $(CORE_LIB)
	$(CC) $(PLAY_OBJECTS) $(PLAY_RESOURCE) $(CORE_LIB) $(GUI_LDFLAGS) $(CORE_SYSLIBS) \
		-lwevtapi -lpsapi -o $@

OUTPUTS += Play.exe
CLEAN_FILES += Play.exe
HELP_TARGETS += Play.exe

.PHONY: play
play: Play.exe
