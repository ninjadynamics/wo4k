# Automated tests: every tests/core/test_*.c becomes build/tests/core/test_*.exe,
# linked with the core library and the harness in tests/wo_test.c.
# "make test" builds and runs them all from the repository root.

TEST_SOURCES := $(sort $(wildcard tests/core/test_*.c))
TEST_HARNESS := $(BUILD)/obj/tests/wo_test.o
TEST_EXES := $(patsubst tests/%.c,$(BUILD)/tests/%.exe,$(TEST_SOURCES))
TEST_PROGRAMS += $(TEST_EXES)
DEPFILES += $(patsubst tests/%.c,$(BUILD)/obj/tests/%.d,$(TEST_SOURCES)) \
	$(TEST_HARNESS:.o=.d)

$(BUILD)/obj/tests/%.o: CFLAGS += -Itests

$(BUILD)/tests/%.exe: $(BUILD)/obj/tests/%.o $(TEST_HARNESS) $(CORE_LIB)
	$(call MKDIR,$(@D))
	$(CC) $< $(TEST_HARNESS) $(CORE_LIB) $(CONSOLE_LDFLAGS) $(CORE_SYSLIBS) -o $@

.PHONY: tests
tests: $(TEST_EXES)

# Slow checks against this machine's real game sources and installation
# (they skip themselves elsewhere): "make test-local".
LOCAL_TEST_SOURCES := $(sort $(wildcard tests/local/test_*.c))
LOCAL_TEST_EXES := $(patsubst tests/%.c,$(BUILD)/tests/%.exe,$(LOCAL_TEST_SOURCES))
DEPFILES += $(patsubst tests/%.c,$(BUILD)/obj/tests/%.d,$(LOCAL_TEST_SOURCES))

.PHONY: test-local
test-local: $(patsubst %,%.run,$(LOCAL_TEST_EXES))
HELP_TARGETS += test-local
