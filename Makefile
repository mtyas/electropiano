# Convenience wrapper: CMakeLists.txt is the one build definition (bundles, Info.plists, LV2
# metadata, tests). These targets configure and drive it in ./build.
#
#   make                      all formats + tests, for this Mac's architecture
#   make ARCHS="arm64;x86_64" universal binaries
#   make test | bench | install | clean

CMAKE ?= cmake
BUILD_DIR := build
ARCHS ?=
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
CONFIG_ARGS := -DCMAKE_BUILD_TYPE=Release $(if $(ARCHS),-DCMAKE_OSX_ARCHITECTURES="$(ARCHS)")

.PHONY: all configure clap vst3 au app lv2 test bench install clean

all: configure
	$(CMAKE) --build $(BUILD_DIR) -j$(JOBS)

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) $(CONFIG_ARGS)

clap: configure ; $(CMAKE) --build $(BUILD_DIR) --target electropiano_CLAP -j$(JOBS)
vst3: configure ; $(CMAKE) --build $(BUILD_DIR) --target electropiano_VST3 -j$(JOBS)
au:   configure ; $(CMAKE) --build $(BUILD_DIR) --target electropiano_AU -j$(JOBS)
app:  configure ; $(CMAKE) --build $(BUILD_DIR) --target electropiano_App -j$(JOBS)
lv2:  configure ; $(CMAKE) --build $(BUILD_DIR) --target electropiano_LV2 -j$(JOBS)

# The format tests load the bundles from ./build, so they run from the repository root
test: all
	./$(BUILD_DIR)/test_wurli_port
	./$(BUILD_DIR)/test_phs_piano

# CPU benchmark and output-regression check (see tools/bench_engine.cpp)
bench: configure
	$(CMAKE) --build $(BUILD_DIR) --target bench_engine -j$(JOBS)
	./$(BUILD_DIR)/bench_engine

install: all
	@mkdir -p "$$HOME/Library/Audio/Plug-Ins/CLAP" "$$HOME/Library/Audio/Plug-Ins/VST3" \
	          "$$HOME/Library/Audio/Plug-Ins/Components" "$$HOME/Library/Audio/Plug-Ins/LV2"
	cp -R "$(BUILD_DIR)/electropiano.clap" "$$HOME/Library/Audio/Plug-Ins/CLAP/"
	cp -R "$(BUILD_DIR)/electropiano.vst3" "$$HOME/Library/Audio/Plug-Ins/VST3/"
	cp -R "$(BUILD_DIR)/electropiano.component" "$$HOME/Library/Audio/Plug-Ins/Components/"
	cp -R "$(BUILD_DIR)/electropiano.lv2" "$$HOME/Library/Audio/Plug-Ins/LV2/"
	@echo "Installed CLAP, VST3, AU and LV2 to ~/Library/Audio/Plug-Ins/"

clean:
	rm -rf $(BUILD_DIR) renders/*.wav
