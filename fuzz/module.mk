# Fuzz harnesses (fuzz/fuzz_*.c). Each one links only portable core
# sources, which also proves those build without the Windows layer.
#
#   mingw32-make fuzz-smoke   build the standalone runners and run each over
#                             its seed corpus in fuzz/corpus/<name> (plus
#                             deterministic truncations and byte flips)
#   fuzz/build-libfuzzer.sh   clang libFuzzer + ASan/UBSan builds (Linux CI)

FUZZ_COMMON := src/core/wo_base.c src/core/wo_io.c src/core/wo_json.c
FUZZ_SOURCES_iso := src/core/wo_iso.c
FUZZ_SOURCES_cue := src/core/wo_disctracks.c
FUZZ_SOURCES_mds := src/core/wo_disctracks.c
FUZZ_SOURCES_tim := src/core/wo_texfmt.c
FUZZ_SOURCES_tga := src/core/wo_texfmt.c
FUZZ_SOURCES_cmp := src/core/wo_texfmt.c
FUZZ_SOURCES_wad := src/core/wo_texfmt.c
FUZZ_SOURCES_recipe := src/core/wo_patch.c
FUZZ_SOURCES_json :=
FUZZ_SOURCES_wav := src/core/wo_audioclean.c
FUZZ_SOURCES_cdaudio := src/core/wo_cdaudio.c
FUZZ_SOURCES_texsrc := src/core/wo_texsrc.c src/core/wo_texfmt.c
FUZZ_NAMES := iso cue mds tim tga cmp wad recipe json wav cdaudio texsrc
FUZZ_EXES := $(patsubst %,$(BUILD)/fuzz/fuzz_%.exe,$(FUZZ_NAMES))
HELP_TARGETS += fuzz fuzz-smoke

define fuzz_rule
$(BUILD)/fuzz/fuzz_$(1).exe: fuzz/fuzz_$(1).c fuzz/standalone.c fuzz/fuzz.h $(FUZZ_COMMON) \
		$(FUZZ_SOURCES_$(1)) $(wildcard src/core/*.h)
	$$(call MKDIR,$$(@D))
	$$(CC) $$(CFLAGS) -Ifuzz fuzz/fuzz_$(1).c fuzz/standalone.c $(FUZZ_COMMON) \
		$(FUZZ_SOURCES_$(1)) $$(CONSOLE_LDFLAGS) -lm -o $$@
endef
$(foreach name,$(FUZZ_NAMES),$(eval $(call fuzz_rule,$(name))))

.PHONY: fuzz fuzz-smoke
fuzz: $(FUZZ_EXES)

fuzz-smoke: $(FUZZ_EXES)
	$(foreach name,$(FUZZ_NAMES),@$(call winpath,$(BUILD)/fuzz/fuzz_$(name).exe) \
		$(call winpath,fuzz/corpus/$(name))$(newline))
