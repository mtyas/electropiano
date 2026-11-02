CXX := clang++
# ARCHFLAGS: e.g. make ARCHFLAGS="-arch arm64 -arch x86_64" for universal binaries
ARCHFLAGS ?=
CXXFLAGS := -std=c++17 -O3 -ffast-math -Wall -Wextra -fPIC -fvisibility=hidden $(ARCHFLAGS)
OBJCXXFLAGS := $(CXXFLAGS) -ObjC++ -fno-objc-arc
FRAMEWORKS := -framework Cocoa -framework QuartzCore -framework AudioUnit -framework AudioToolbox -framework CoreAudio -framework CoreMIDI -framework CoreFoundation

PLUGIN_NAME := electropiano
BUILD_DIR := build

CLAP_BUNDLE := $(BUILD_DIR)/$(PLUGIN_NAME).clap
VST3_BUNDLE := $(BUILD_DIR)/$(PLUGIN_NAME).vst3
AU_BUNDLE   := $(BUILD_DIR)/$(PLUGIN_NAME).component
APP_BUNDLE  := $(BUILD_DIR)/$(PLUGIN_NAME).app
TEST_BIN    := $(BUILD_DIR)/test_phs_piano

DSP_HEADERS := src/dsp/PhsParameters.hpp src/dsp/PhsVoice.hpp src/dsp/PhsEffects.hpp src/dsp/PhsSynthEngine.hpp \
               src/dsp/PhsPresetManager.hpp src/dsp/wurli/WurliVoice.hpp src/dsp/wurli/WurliAmp.hpp \
               src/dsp/wurli/WurliPickupTables.hpp
GUI_SOURCES := src/gui/PhsCocoaView.mm src/gui/PhsEditor.cpp
GUI_HEADERS := src/gui/PhsCocoaView.h src/gui/PhsEditor.hpp src/gui/PhsCanvas.hpp

.PHONY: all clap vst3 au app test install clean

all: clap vst3 au app $(TEST_BIN)

# 1. CLAP Plugin Bundle (.clap)
clap: src/formats/clap_entry.mm $(GUI_SOURCES) $(DSP_HEADERS) $(GUI_HEADERS) include/clap/clap.h
	@mkdir -p "$(CLAP_BUNDLE)/Contents/MacOS" "$(CLAP_BUNDLE)/Contents/Resources"
	@cp -f resources/gui/*.png "$(CLAP_BUNDLE)/Contents/Resources/" 2>/dev/null || true
	$(CXX) $(OBJCXXFLAGS) -bundle -DPHS_VIEW_CLASS_NAME=PhsEditorView_CLAP \
		src/formats/clap_entry.mm $(GUI_SOURCES) \
		$(FRAMEWORKS) -o "$(CLAP_BUNDLE)/Contents/MacOS/$(PLUGIN_NAME)"
	@printf '%s\n' \
		'<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0">' \
		'<dict>' \
		'  <key>CFBundleDevelopmentRegion</key><string>English</string>' \
		'  <key>CFBundleExecutable</key><string>electropiano</string>' \
		'  <key>CFBundleIdentifier</key><string>com.mtyas.electropiano.clap</string>' \
		'  <key>CFBundleName</key><string>electropiano</string>' \
		'  <key>CFBundlePackageType</key><string>BNDL</string>' \
		'  <key>CFBundleShortVersionString</key><string>0.79</string>' \
		'  <key>CFBundleVersion</key><string>0.79</string>' \
		'  <key>CSResourcesFileMapped</key><true/>' \
		'</dict>' \
		'</plist>' > "$(CLAP_BUNDLE)/Contents/Info.plist"
	@printf 'BNDL????' > "$(CLAP_BUNDLE)/Contents/PkgInfo"
	@codesign --force --sign - "$(CLAP_BUNDLE)" 2>/dev/null || true

# 2. VST3 Plugin Bundle (.vst3)
vst3: src/formats/vst3_entry.mm $(GUI_SOURCES) $(DSP_HEADERS) $(GUI_HEADERS) include/vst3/vst3_c_api.h
	@mkdir -p "$(VST3_BUNDLE)/Contents/MacOS" "$(VST3_BUNDLE)/Contents/Resources"
	@cp -f resources/gui/*.png "$(VST3_BUNDLE)/Contents/Resources/" 2>/dev/null || true
	$(CXX) $(OBJCXXFLAGS) -bundle -DPHS_VIEW_CLASS_NAME=PhsEditorView_VST3 \
		src/formats/vst3_entry.mm $(GUI_SOURCES) \
		$(FRAMEWORKS) -o "$(VST3_BUNDLE)/Contents/MacOS/$(PLUGIN_NAME)"
	@printf '%s\n' \
		'<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0">' \
		'<dict>' \
		'  <key>CFBundleDevelopmentRegion</key><string>English</string>' \
		'  <key>CFBundleExecutable</key><string>electropiano</string>' \
		'  <key>CFBundleIdentifier</key><string>com.mtyas.electropiano.vst3</string>' \
		'  <key>CFBundleName</key><string>electropiano</string>' \
		'  <key>CFBundlePackageType</key><string>BNDL</string>' \
		'  <key>CFBundleShortVersionString</key><string>0.79</string>' \
		'  <key>CFBundleVersion</key><string>0.79</string>' \
		'  <key>CSResourcesFileMapped</key><true/>' \
		'</dict>' \
		'</plist>' > "$(VST3_BUNDLE)/Contents/Info.plist"
	@printf 'BNDL????' > "$(VST3_BUNDLE)/Contents/PkgInfo"
	@codesign --force --sign - "$(VST3_BUNDLE)" 2>/dev/null || true

# 3. AudioUnit v2 Plugin Bundle (.component)
au: src/formats/au_entry.mm $(GUI_SOURCES) $(DSP_HEADERS) $(GUI_HEADERS)
	@mkdir -p "$(AU_BUNDLE)/Contents/MacOS" "$(AU_BUNDLE)/Contents/Resources"
	@cp -f resources/gui/*.png "$(AU_BUNDLE)/Contents/Resources/" 2>/dev/null || true
	$(CXX) $(OBJCXXFLAGS) -bundle -DPHS_VIEW_CLASS_NAME=PhsEditorView_AU \
		src/formats/au_entry.mm $(GUI_SOURCES) \
		$(FRAMEWORKS) -o "$(AU_BUNDLE)/Contents/MacOS/$(PLUGIN_NAME)"
	@printf '%s\n' \
		'<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0">' \
		'<dict>' \
		'  <key>CFBundleDevelopmentRegion</key><string>English</string>' \
		'  <key>CFBundleExecutable</key><string>electropiano</string>' \
		'  <key>CFBundleIdentifier</key><string>com.mtyas.electropiano.audiounit</string>' \
		'  <key>CFBundleName</key><string>electropiano</string>' \
		'  <key>CFBundlePackageType</key><string>BNDL</string>' \
		'  <key>CFBundleShortVersionString</key><string>0.79</string>' \
		'  <key>CFBundleVersion</key><string>0.79</string>' \
		'  <key>AudioComponents</key>' \
		'  <array>' \
		'    <dict>' \
		'      <key>name</key><string>mtyas: electropiano</string>' \
		'      <key>description</key><string>electropiano by mtyas - Port-Hamiltonian Rhodes &amp; Wurlitzer</string>' \
		'      <key>factoryFunction</key><string>PhsElectricPianoAUFactory</string>' \
		'      <key>manufacturer</key><string>mtya</string>' \
		'      <key>subtype</key><string>ePno</string>' \
		'      <key>type</key><string>aumu</string>' \
		'      <key>version</key><integer>19712</integer>' \
		'      <key>sandboxSafe</key><true/>' \
		'    </dict>' \
		'  </array>' \
		'</dict>' \
		'</plist>' > "$(AU_BUNDLE)/Contents/Info.plist"
	@printf 'BNDL????' > "$(AU_BUNDLE)/Contents/PkgInfo"
	@codesign --force --sign - "$(AU_BUNDLE)" 2>/dev/null || true

# 4. Standalone macOS App (.app)
app: src/formats/standalone_app.mm $(GUI_SOURCES) $(DSP_HEADERS) $(GUI_HEADERS)
	@mkdir -p "$(APP_BUNDLE)/Contents/MacOS" "$(APP_BUNDLE)/Contents/Resources"
	@cp -f resources/gui/*.png "$(APP_BUNDLE)/Contents/Resources/" 2>/dev/null || true
	$(CXX) $(OBJCXXFLAGS) -DPHS_VIEW_CLASS_NAME=PhsEditorView_Standalone \
		src/formats/standalone_app.mm $(GUI_SOURCES) \
		$(FRAMEWORKS) -o "$(APP_BUNDLE)/Contents/MacOS/$(PLUGIN_NAME)"
	@printf '%s\n' \
		'<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0">' \
		'<dict>' \
		'  <key>CFBundleDevelopmentRegion</key><string>English</string>' \
		'  <key>CFBundleExecutable</key><string>electropiano</string>' \
		'  <key>CFBundleIdentifier</key><string>com.mtyas.electropiano.standalone</string>' \
		'  <key>CFBundleName</key><string>electropiano</string>' \
		'  <key>CFBundlePackageType</key><string>APPL</string>' \
		'  <key>CFBundleShortVersionString</key><string>0.79</string>' \
		'  <key>CFBundleVersion</key><string>0.79</string>' \
		'  <key>NSHighResolutionCapable</key><true/>' \
		'</dict>' \
		'</plist>' > "$(APP_BUNDLE)/Contents/Info.plist"
	@printf 'APPL????' > "$(APP_BUNDLE)/Contents/PkgInfo"
	@codesign --force --sign - "$(APP_BUNDLE)" 2>/dev/null || true

# 5. Test & Audio Verification Harness
$(TEST_BIN): tools/test_phs_piano.cpp $(DSP_HEADERS) include/clap/clap.h include/vst3/vst3_c_api.h
	@mkdir -p $(BUILD_DIR) renders
	$(CXX) $(CXXFLAGS) tools/test_phs_piano.cpp $(FRAMEWORKS) -o $(TEST_BIN)

test: all build/test_wurli_port
	./build/test_wurli_port
	./$(TEST_BIN)

# OpenWurli port verification against upstream's SPICE-derived acceptance targets
build/test_wurli_port: tools/test_wurli_port.cpp $(DSP_HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) tools/test_wurli_port.cpp -o build/test_wurli_port

install: all
	@mkdir -p "$$HOME/Library/Audio/Plug-Ins/CLAP"
	@mkdir -p "$$HOME/Library/Audio/Plug-Ins/VST3"
	@mkdir -p "$$HOME/Library/Audio/Plug-Ins/Components"
	cp -R "$(CLAP_BUNDLE)" "$$HOME/Library/Audio/Plug-Ins/CLAP/"
	cp -R "$(VST3_BUNDLE)" "$$HOME/Library/Audio/Plug-Ins/VST3/"
	cp -R "$(AU_BUNDLE)"   "$$HOME/Library/Audio/Plug-Ins/Components/"
	@echo "Installed CLAP, VST3, and AU plugins to ~/Library/Audio/Plug-Ins/"

clean:
	rm -rf $(BUILD_DIR) renders/*.wav
