cmake_minimum_required(VERSION 3.16)
# The version lives in src/dsp/PhsVersion.hpp
file(STRINGS src/dsp/PhsVersion.hpp PHS_VERSION_LINE REGEX "^#define PHS_VERSION_STRING")
string(REGEX MATCH "\"([0-9.]+)\"" _ "${PHS_VERSION_LINE}")
project(electropiano VERSION ${CMAKE_MATCH_1} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    add_compile_options(/O2 /fp:fast /W4 /utf-8 /permissive-)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -D_USE_MATH_DEFINES -DWIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-O3 -ffast-math -Wall -Wextra -fvisibility=hidden)
endif()

# Common DSP & Include Headers
set(DSP_HEADERS
    src/dsp/PhsParameters.hpp
    src/dsp/PhsVoice.hpp
    src/dsp/PhsEffects.hpp
    src/dsp/PhsSynthEngine.hpp
    src/dsp/PhsPresetManager.hpp
    src/dsp/wurli/WurliVoice.hpp
    src/dsp/wurli/WurliAmp.hpp
    src/dsp/wurli/WurliPickupTables.hpp
)

include_directories(
    include
    src/dsp
    src/gui
)

# Platform-specific GUI Sources and Libraries
if(APPLE)
    enable_language(OBJCXX)
    set(GUI_SOURCES
        src/gui/PhsCocoaView.mm
        src/gui/PhsEditor.cpp
    )
    set(PLATFORM_LIBS
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework AudioUnit"
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework CoreMIDI"
        "-framework CoreFoundation"
    )
elseif(WIN32)
    set(GUI_SOURCES
        src/gui/PhsWin32View.cpp
        src/gui/PhsEditor.cpp
    )
    set(PLATFORM_LIBS
        gdi32
        user32
        shell32
        winmm
        comdlg32
    )
else() # Linux
    find_package(X11 REQUIRED)
    set(GUI_SOURCES
        src/gui/PhsX11View.cpp
        src/gui/PhsEditor.cpp
    )
    set(PLATFORM_LIBS
        ${X11_LIBRARIES}
        pthread
        dl
        m
    )
endif()

# The format entry points are shared Objective-C++/C++ sources: everywhere but macOS
# they are compiled as plain C++.
if(NOT APPLE)
    set_source_files_properties(src/formats/clap_entry.mm src/formats/vst3_entry.mm PROPERTIES LANGUAGE CXX)
    if(MSVC)
        set_source_files_properties(src/formats/clap_entry.mm src/formats/vst3_entry.mm PROPERTIES COMPILE_OPTIONS "/TP")
    else()
        set_source_files_properties(src/formats/clap_entry.mm src/formats/vst3_entry.mm PROPERTIES COMPILE_OPTIONS "-xc++")
    endif()
endif()

# ==============================================================================
# macOS bundles: Info.plist from one template, the GUI images as resources, PkgInfo and an
# ad-hoc signature (release signing replaces it)
# ==============================================================================
# Audio Unit component version: major << 16 | minor << 8 | patch
math(EXPR PHS_AU_VERSION "(${PROJECT_VERSION_MAJOR} << 16) | (${PROJECT_VERSION_MINOR} << 8) | 0")
file(GLOB PHS_GUI_RESOURCES ${CMAKE_SOURCE_DIR}/resources/gui/*.png)

function(phs_macos_bundle target bundle_id package_type extra_plist)
    set(PHS_BUNDLE_ID ${bundle_id})
    set(PHS_PACKAGE_TYPE ${package_type})
    set(PHS_EXTRA_PLIST "${extra_plist}")
    configure_file(${CMAKE_SOURCE_DIR}/resources/macos/Bundle-Info.plist.in ${CMAKE_BINARY_DIR}/plist/${target}-Info.plist @ONLY)
    set_target_properties(${target} PROPERTIES MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/plist/${target}-Info.plist)
    target_sources(${target} PRIVATE ${PHS_GUI_RESOURCES})
    set_source_files_properties(${PHS_GUI_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND /bin/sh -c "printf '${package_type}????' > \"$<TARGET_BUNDLE_CONTENT_DIR:${target}>/PkgInfo\""
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${target}>" || true
        VERBATIM)
endfunction()

# ==============================================================================
# 1. CLAP Plugin Target
# ==============================================================================
add_library(electropiano_CLAP MODULE
    src/formats/clap_entry.mm
    ${GUI_SOURCES}
    ${DSP_HEADERS}
)
set_target_properties(electropiano_CLAP PROPERTIES
    OUTPUT_NAME "electropiano"
    PREFIX ""
    SUFFIX ".clap"
)
target_compile_definitions(electropiano_CLAP PRIVATE PHS_VIEW_CLASS_NAME=PhsEditorView_CLAP)
# Both plugins are called "electropiano": keep their import libraries apart on Windows
set_target_properties(electropiano_CLAP PROPERTIES ARCHIVE_OUTPUT_NAME "electropiano_clap")
target_link_libraries(electropiano_CLAP PRIVATE ${PLATFORM_LIBS})

if(APPLE)
    set_target_properties(electropiano_CLAP PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION "clap")
    phs_macos_bundle(electropiano_CLAP "com.mtyas.electropiano.clap" "BNDL" "")
endif()

# ==============================================================================
# 2. VST3 Plugin Target
# ==============================================================================
add_library(electropiano_VST3 MODULE
    src/formats/vst3_entry.mm
    ${GUI_SOURCES}
    ${DSP_HEADERS}
)
set_target_properties(electropiano_VST3 PROPERTIES
    OUTPUT_NAME "electropiano"
    PREFIX ""
    SUFFIX ".vst3"
)
target_compile_definitions(electropiano_VST3 PRIVATE PHS_VIEW_CLASS_NAME=PhsEditorView_VST3)
set_target_properties(electropiano_VST3 PROPERTIES ARCHIVE_OUTPUT_NAME "electropiano_vst3")
target_link_libraries(electropiano_VST3 PRIVATE ${PLATFORM_LIBS})

if(APPLE)
    set_target_properties(electropiano_VST3 PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION "vst3")
    phs_macos_bundle(electropiano_VST3 "com.mtyas.electropiano.vst3" "BNDL" "")
endif()

# ==============================================================================
# 3. AudioUnit v2 (macOS only)
# ==============================================================================
if(APPLE)
    add_library(electropiano_AU MODULE
        src/formats/au_entry.mm
        ${GUI_SOURCES}
        ${DSP_HEADERS}
    )
    set_target_properties(electropiano_AU PROPERTIES
        OUTPUT_NAME "electropiano"
        PREFIX ""
        BUNDLE TRUE
        BUNDLE_EXTENSION "component"
    )
    target_compile_definitions(electropiano_AU PRIVATE PHS_VIEW_CLASS_NAME=PhsEditorView_AU)
    target_link_libraries(electropiano_AU PRIVATE ${PLATFORM_LIBS})
    phs_macos_bundle(electropiano_AU "com.mtyas.electropiano.audiounit" "BNDL"
"  <key>AudioComponents</key>
  <array>
    <dict>
      <key>name</key><string>mtyas: electropiano</string>
      <key>description</key><string>electropiano by mtyas - Port-Hamiltonian Rhodes &amp; Wurlitzer</string>
      <key>factoryFunction</key><string>PhsElectricPianoAUFactory</string>
      <key>manufacturer</key><string>mtya</string>
      <key>subtype</key><string>ePno</string>
      <key>type</key><string>aumu</string>
      <key>version</key><integer>${PHS_AU_VERSION}</integer>
      <key>sandboxSafe</key><true/>
    </dict>
  </array>
")
endif()

# ==============================================================================
# 4. Standalone Application
# ==============================================================================
if(APPLE)
    add_executable(electropiano_App MACOSX_BUNDLE
        src/formats/standalone_app.mm
        ${GUI_SOURCES}
        ${DSP_HEADERS}
    )
    set_target_properties(electropiano_App PROPERTIES OUTPUT_NAME "electropiano")
    target_compile_definitions(electropiano_App PRIVATE PHS_VIEW_CLASS_NAME=PhsEditorView_Standalone)
    target_link_libraries(electropiano_App PRIVATE ${PLATFORM_LIBS})
    phs_macos_bundle(electropiano_App "com.mtyas.electropiano.standalone" "APPL" "")
endif()

# ==============================================================================
# 4b. LV2 bundle: plugin and UI in one binary, metadata generated from the parameter table
# ==============================================================================
add_executable(gen_lv2_ttl tools/gen_lv2_ttl.cpp)
add_library(electropiano_LV2 MODULE
    src/formats/lv2_entry.cpp
    ${GUI_SOURCES}
    ${DSP_HEADERS}
)
set(PHS_LV2_DIR ${CMAKE_BINARY_DIR}/electropiano.lv2)
set_target_properties(electropiano_LV2 PROPERTIES
    OUTPUT_NAME "electropiano"
    PREFIX ""
    ARCHIVE_OUTPUT_NAME "electropiano_lv2"
    LIBRARY_OUTPUT_DIRECTORY ${PHS_LV2_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${PHS_LV2_DIR}
)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set_target_properties(electropiano_LV2 PROPERTIES LIBRARY_OUTPUT_DIRECTORY_${CFG} ${PHS_LV2_DIR} RUNTIME_OUTPUT_DIRECTORY_${CFG} ${PHS_LV2_DIR})
endforeach()
target_compile_definitions(electropiano_LV2 PRIVATE PHS_VIEW_CLASS_NAME=PhsEditorView_LV2)
target_link_libraries(electropiano_LV2 PRIVATE ${PLATFORM_LIBS})
if(APPLE)
    # The UI calls the Cocoa view, so this file must be Objective-C++ like the views
    set_source_files_properties(src/formats/lv2_entry.cpp PROPERTIES LANGUAGE OBJCXX)
    set_target_properties(electropiano_LV2 PROPERTIES SUFFIX ".dylib")
    set(PHS_LV2_UI CocoaUI)
elseif(WIN32)
    set(PHS_LV2_UI WindowsUI)
else()
    set(PHS_LV2_UI X11UI)
endif()
add_dependencies(electropiano_LV2 gen_lv2_ttl)
add_custom_command(TARGET electropiano_LV2 POST_BUILD
    COMMAND $<TARGET_FILE:gen_lv2_ttl> ${PHS_LV2_DIR} $<TARGET_FILE_NAME:electropiano_LV2> ${PHS_LV2_UI}
    VERBATIM)

# ==============================================================================
# 5. Automated Test & Passivity Suite
# ==============================================================================
add_executable(test_phs_piano
    tools/test_phs_piano.cpp
    ${DSP_HEADERS}
)
target_link_libraries(test_phs_piano PRIVATE ${PLATFORM_LIBS})
if(UNIX AND NOT APPLE)
    target_link_libraries(test_phs_piano PRIVATE dl pthread)
endif()

# OpenWurli port verification (SPICE-derived acceptance targets)
add_executable(test_wurli_port tools/test_wurli_port.cpp ${DSP_HEADERS})

# CPU benchmark and output-regression check (not a test: timings depend on the machine)
add_executable(bench_engine tools/bench_engine.cpp ${DSP_HEADERS})

enable_testing()
add_test(NAME PassivityAndSpectralAnalysis COMMAND test_phs_piano)
add_test(NAME OpenWurliPort COMMAND test_wurli_port)
