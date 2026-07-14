
.PHONY: help mac-setup mac-configure mac-build mac-run mac-run-direct mac-app mac-clean build-mac run-mac package-mac

MAC_BUILD_DIR := build-mac
MAC_APP := $(abspath $(MAC_BUILD_DIR)/bin/MidiEditorAI.app)
MAC_BIN := $(MAC_APP)/Contents/MacOS/MidiEditorAI

help:
	@echo "macOS targets:"
	@echo "  make mac-setup      - install dependencies via Homebrew Bundle"
	@echo "  make mac-configure  - configure the build directory with CMake"
	@echo "  make mac-build      - build the macOS binary in $(MAC_BUILD_DIR)"
	@echo "  make mac-run        - run the binary directly (no .app bundle)"
	@echo "  make mac-run-direct - explicit alias for direct run (no .app bundle)"
	@echo "  make mac-app        - package and sign the .app in $(MAC_APP)"
	@echo "  make mac-clean      - remove macOS build and packaging artifacts"

# Install dependencies declared in Brewfile.
mac-setup:
	brew bundle

# Configure the project using CMake if not already configured.
mac-configure:
	@if [ ! -f "$(MAC_BUILD_DIR)/CMakeCache.txt" ]; then \
		cmake -S . -B $(MAC_BUILD_DIR); \
	fi

# Build the project for macOS using the existing build directory.
# First pass is parallel for speed, second pass is serial to surface
# the first clear error if final linking fails.
mac-build: mac-configure
	cmake --build $(MAC_BUILD_DIR) -j8
	cmake --build $(MAC_BUILD_DIR) -j1

# Run the binary directly (without an .app bundle).
mac-run: mac-build
	$(MAC_BIN)

# Explicit way to run without packaging as .app.
mac-run-direct: mac-build
	$(MAC_BIN)

# Package the binary into a self-contained .app with Qt frameworks/plugins,
# clear local quarantine attributes, and ad-hoc sign for local execution.
# CMake generates the .app bundle structure and Info.plist via MACOSX_BUNDLE
# properties; macdeployqt embeds Qt frameworks and plugins. Codesign with
# --force --sign - (not --deep, which Apple advises against) for ad-hoc signing.
mac-app: mac-build
	rm -rf $(MAC_APP)
	cmake --build $(MAC_BUILD_DIR) --target MidiEditorAI -j1
	macdeployqt $(MAC_APP) -always-overwrite -no-strip
	xattr -dr com.apple.quarantine $(MAC_APP) || true
	codesign --force --sign - $(MAC_APP)

# Fully clean macOS build/packaging output.
mac-clean:
	rm -rf $(MAC_BUILD_DIR)

# Backward-compatible aliases with previous target names.
build-mac: mac-build
run-mac: mac-run
package-mac: mac-app