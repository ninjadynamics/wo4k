# Dependencies, release packaging and signing input (included by Makefile).
#
#   make deps          fetch and verify the pinned third-party archives
#   make sign-input    copy the binaries to sign into build/unsigned
#   make package       dist/wo4k-<VERSION>.zip (+ .sha256, release-files.json)
#                      SIGNED=build/signed uses the signed binaries instead
#   make release       build, test and package
#   make release-notes build/release-notes.md from the VERSION section of
#                      docs/CHANGELOG.md (fails when there is none)

SIGNED ?=
DIST := dist

.PHONY: deps manifests sign-input package release release-notes
HELP_TARGETS += deps sign-input package release release-notes

deps: $(BUILD_TOOL)
	$(call winpath,$(BUILD_TOOL)) fetch manifests/dependencies.json $(DEPS_DIR)

# DDrawCompat ships unmodified, as its upstream project publishes it.
runtime/ddraw.dll: $(DEPS_DIR)/ddraw.dll
	copy /y "$(call winpath,$<)" "$(call winpath,$@)" >nul

OUTPUTS += runtime/ddraw.dll

manifests: $(OUTPUTS) $(BUILD_TOOL)
	$(call winpath,$(BUILD_TOOL)) manifests

sign-input: $(OUTPUTS) $(BUILD_TOOL)
	$(call RMDIR,$(BUILD)/unsigned)
	$(call winpath,$(BUILD_TOOL)) sign-input $(BUILD)/unsigned

package: manifests
	$(call winpath,$(BUILD_TOOL)) package $(VERSION) $(DIST) $(SIGNED)

release: test package

release-notes: $(BUILD_TOOL)
	$(call winpath,$(BUILD_TOOL)) release-notes docs/CHANGELOG.md $(VERSION) $(BUILD)/release-notes.md
