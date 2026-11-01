CXX := clang++
CXXFLAGS := -std=c++17 -O3 -ffast-math -Wall -Wextra -fPIC -fvisibility=hidden
OBJCXXFLAGS := $(CXXFLAGS) -ObjC++ -fno-objc-arc
FRAMEWORKS := -framework Cocoa -framework QuartzCore -framework AudioUnit -framework AudioToolbox -framework CoreAudio -framework CoreMIDI -framework CoreFoundation

PLUGIN_NAME := PyPHS Electric Piano
BUILD_DIR := build

CLAP_BUNDLE := $(BUILD_DIR)/$(PLUGIN_NAME).clap
VST3_BUNDLE := $(BUILD_DIR)/$(PLUGIN_NAME).vst3
AU_BUNDLE   := $(BUILD_DIR)/$(PLUGIN_NAME).component
APP_BUNDLE  := $(BUILD_DIR)/$(PLUGIN_NAME).app
TEST_BIN    := $(BUILD_DIR)/test_phs_piano

DSP_HEADERS := src/dsp/PhsParameters.hpp src/dsp/PhsVoice.hpp src/dsp/PhsEffects.hpp src/dsp/PhsSynthEngine.hpp
GUI_SOURCES := src/gui/PhsCocoaView.mm
GUI_HEADERS := src/gui/PhsCocoaView.h

.PHONY: all clap vst3 au app test install clean

all: clap vst3 au app $(TEST_BIN)

# 1. CLAP Plugin Bundle (.clap)
clap: src/formats/clap_entry.mm $(GUI_SOURCES) $(DSP_HEADERS) $(GUI_HEADERS) include/clap/clap.h
	@mkdir -p "$(CLAP_BUNDLE)/Contents/MacOS"
	$(CXX) $(OBJCXXFLAGS) -bundle -DPHS_VIEW_CLASS_NAME=PhsEditorView_CLAP \
		src/formats/clap_entry.mm $(GUI_SOURCES) \
		$(FRAMEWORKS) -o "$(CLAP_BUNDLE)/Contents/MacOS/$(PLUGIN_NAME)"
	@printf '%s\n' \
		'<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0">' \
		'<dict>' \
		'  <key>CFBundleDevelopmentRegion</key><string>English</string>' \
		'  <key>CFBundleExecutable</key><string>PyPHS Electric Piano</string>' \
		'  <key>CFBundleIdentifier</key><string>org.pyphs.pianoelectro.clap</string>' \
		'  <key>CFBundleName</key><string>PyPHS Electric Piano</string>' \
		'  <key>CFBundlePackageType</key><string>BNDL</string>' \
		'  <key>CFBundleShortVersionString</key><string>1.0.0</string>' \
		'  <key>CFBundleVersion</key><string>1.0.0</string>' \
		'  <key>CSResourcesFileMapped</key><true/>' \
		'</dict>' \
		'</plist>' > "$(CLAP_BUNDLE)/Contents/Info.plist"
	@printf 'BNDL????' > "$(CLAP_BUNDLE)/Contents/PkgInfo"
	@codesign --force --sign - "$(CLAP_BUNDLE)" 2>/dev/null || true

# 2. VST3 Plugin Bundle (.vst3)
vst3: src/formats/vst3_entry.mm $(GUI_SOURCES) $(DSP_HEADERS) $(GUI_HEADERS) include/vst3/vst3_c_api.h
	@mkdir -p "$(VST3_BUNDLE)/Contents/MacOS"
	$(CXX) $(OBJCXXFLAGS) -bundle -DPHS_VIEW_CLASS_NAME=PhsEditorView_VST3 \
		src/formats/vst3_entry.mm $(GUI_SOURCES) \
		$(FRAMEWORKS) -o "$(VST3_BUNDLE)/Contents/MacOS/$(PLUGIN_NAME)"
	@printf '%s\n' \
		'<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0">' \
		'<dict>' \
		'  <key>CFBundleDevelopmentRegion</key><string>English</string>' \
		'  <key>CFBundleExecutable</key><string>PyPHS Electric Piano</string>' \
		'  <key>CFBundleIdentifier</key><string>org.pyphs.pianoelectro.vst3</string>' \
		'  <key>CFBundleName</key><string>PyPHS Electric Piano</string>' \
		'  <key>CFBundlePackageType</key><string>BNDL</string>' \
		'  <key>CFBundleShortVersionString</key><string>1.0.0</string>' \
		'  <key>CFBundleVersion</key><string>1.0.0</string>' \
		'  <key>CSResourcesFileMapped</key><true/>' \
		'</dict>' \
		'</plist>' > "$(VST3_BUNDLE)/Contents/Info.plist"
	@printf 'BNDL????' > "$(VST3_BUNDLE)/Contents/PkgInfo"
	@codesign --force --sign - "$(VST3_BUNDLE)" 2>/dev/null || true

# 3. AudioUnit v2 Plugin Bundle (.component)
au: src/formats/au_entry.mm $(GUI_SOURCES) $(DSP_HEADERS) $(GUI_HEADERS)
	@mkdir -p "$(AU_BUNDLE)/Contents/MacOS"
	$(CXX) $(OBJCXXFLAGS) -bundle -DPHS_VIEW_CLASS_NAME=PhsEditorView_AU \
		src/formats/au_entry.mm $(GUI_SOURCES) \
		$(FRAMEWORKS) -o "$(AU_BUNDLE)/Contents/MacOS/$(PLUGIN_NAME)"
	@printf '%s\n' \
		'<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0">' \
		'<dict>' \
		'  <key>CFBundleDevelopmentRegion</key><string>English</string>' \
		'  <key>CFBundleExecutable</key><string>PyPHS Electric Piano</string>' \
		'  <key>CFBundleIdentifier</key><string>org.pyphs.pianoelectro.audiounit</string>' \
		'  <key>CFBundleName</key><string>PyPHS Electric Piano</string>' \
		'  <key>CFBundlePackageType</key><string>BNDL</string>' \
		'  <key>CFBundleShortVersionString</key><string>1.0.0</string>' \
		'  <key>CFBundleVersion</key><string>1.0.0</string>' \
		'  <key>AudioComponents</key>' \
		'  <array>' \
		'    <dict>' \
		'      <key>name</key><string>PyPHS: Electric Piano</string>' \
		'      <key>description</key><string>Port-Hamiltonian Rhodes and Wurlitzer Electric Piano</string>' \
		'      <key>factoryFunction</key><string>PhsElectricPianoAUFactory</string>' \
		'      <key>manufacturer</key><string>PyPH</string>' \
		'      <key>subtype</key><string>PhsE</string>' \
		'      <key>type</key><string>aumu</string>' \
		'      <key>version</key><integer>65536</integer>' \
		'      <key>sandboxSafe</key><true/>' \
		'    </dict>' \
		'  </array>' \
		'</dict>' \
		'</plist>' > "$(AU_BUNDLE)/Contents/Info.plist"
	@printf 'BNDL????' > "$(AU_BUNDLE)/Contents/PkgInfo"
	@codesign --force --sign - "$(AU_BUNDLE)" 2>/dev/null || true

# 4. Standalone macOS App (.app)
app: src/formats/standalone_app.mm $(GUI_SOURCES) $(DSP_HEADERS) $(GUI_HEADERS)
	@mkdir -p "$(APP_BUNDLE)/Contents/MacOS"
	$(CXX) $(OBJCXXFLAGS) -DPHS_VIEW_CLASS_NAME=PhsEditorView_Standalone \
		src/formats/standalone_app.mm $(GUI_SOURCES) \
		$(FRAMEWORKS) -o "$(APP_BUNDLE)/Contents/MacOS/$(PLUGIN_NAME)"
	@printf '%s\n' \
		'<?xml version="1.0" encoding="UTF-8"?>' \
		'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
		'<plist version="1.0">' \
		'<dict>' \
		'  <key>CFBundleDevelopmentRegion</key><string>English</string>' \
		'  <key>CFBundleExecutable</key><string>PyPHS Electric Piano</string>' \
		'  <key>CFBundleIdentifier</key><string>org.pyphs.pianoelectro.standalone</string>' \
		'  <key>CFBundleName</key><string>PyPHS Electric Piano</string>' \
		'  <key>CFBundlePackageType</key><string>APPL</string>' \
		'  <key>CFBundleShortVersionString</key><string>1.0.0</string>' \
		'  <key>CFBundleVersion</key><string>1.0.0</string>' \
		'  <key>NSHighResolutionCapable</key><true/>' \
		'</dict>' \
		'</plist>' > "$(APP_BUNDLE)/Contents/Info.plist"
	@printf 'APPL????' > "$(APP_BUNDLE)/Contents/PkgInfo"
	@codesign --force --sign - "$(APP_BUNDLE)" 2>/dev/null || true

# 5. Test & Audio Verification Harness
$(TEST_BIN): tools/test_phs_piano.cpp $(DSP_HEADERS) include/clap/clap.h include/vst3/vst3_c_api.h
	@mkdir -p $(BUILD_DIR) renders
	$(CXX) $(CXXFLAGS) tools/test_phs_piano.cpp $(FRAMEWORKS) -o $(TEST_BIN)

test: all
	./$(TEST_BIN)

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
