cmake_minimum_required(VERSION 3.16)
project(electropiano VERSION 0.77 LANGUAGES C CXX)

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
    set_target_properties(electropiano_CLAP PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.mtyas.electropiano.clap"
        MACOSX_BUNDLE_BUNDLE_NAME "electropiano"
    )
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
    set_target_properties(electropiano_VST3 PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "vst3"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.mtyas.electropiano.vst3"
        MACOSX_BUNDLE_BUNDLE_NAME "electropiano"
    )
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
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.mtyas.electropiano.audiounit"
        MACOSX_BUNDLE_BUNDLE_NAME "electropiano"
    )
    target_compile_definitions(electropiano_AU PRIVATE PHS_VIEW_CLASS_NAME=PhsEditorView_AU)
    target_link_libraries(electropiano_AU PRIVATE ${PLATFORM_LIBS})
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
    set_target_properties(electropiano_App PROPERTIES
        OUTPUT_NAME "electropiano"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.mtyas.electropiano.standalone"
        MACOSX_BUNDLE_BUNDLE_NAME "electropiano"
    )
    target_compile_definitions(electropiano_App PRIVATE PHS_VIEW_CLASS_NAME=PhsEditorView_Standalone)
    target_link_libraries(electropiano_App PRIVATE ${PLATFORM_LIBS})
endif()

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

enable_testing()
add_test(NAME PassivityAndSpectralAnalysis COMMAND test_phs_piano)
add_test(NAME OpenWurliPort COMMAND test_wurli_port)
