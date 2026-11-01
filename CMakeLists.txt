cmake_minimum_required(VERSION 3.16)
project(PyPHSElectricPiano VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    add_compile_options(/O2 /fp:fast /W4 /utf-8)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
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
    )
    set(PLATFORM_LIBS
        gdi32
        user32
        winmm
    )
else() # Linux
    find_package(X11 REQUIRED)
    set(GUI_SOURCES
        src/gui/PhsX11View.cpp
    )
    set(PLATFORM_LIBS
        ${X11_LIBRARIES}
        pthread
        dl
        m
    )
endif()

# ==============================================================================
# 1. CLAP Plugin Target
# ==============================================================================
add_library(PyPHS_CLAP MODULE
    src/formats/clap_entry.mm
    ${GUI_SOURCES}
    ${DSP_HEADERS}
)
set_target_properties(PyPHS_CLAP PROPERTIES
    OUTPUT_NAME "PyPHS Electric Piano"
    PREFIX ""
    SUFFIX ".clap"
)
target_compile_definitions(PyPHS_CLAP PRIVATE PHS_VIEW_CLASS_NAME=PhsEditorView_CLAP)
target_link_libraries(PyPHS_CLAP PRIVATE ${PLATFORM_LIBS})

if(APPLE)
    set_target_properties(PyPHS_CLAP PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.pyphs.pianoelectro.clap"
        MACOSX_BUNDLE_BUNDLE_NAME "PyPHS Electric Piano"
    )
endif()

# ==============================================================================
# 2. VST3 Plugin Target
# ==============================================================================
add_library(PyPHS_VST3 MODULE
    src/formats/vst3_entry.mm
    ${GUI_SOURCES}
    ${DSP_HEADERS}
)
set_target_properties(PyPHS_VST3 PROPERTIES
    OUTPUT_NAME "PyPHS Electric Piano"
    PREFIX ""
    SUFFIX ".vst3"
)
target_compile_definitions(PyPHS_VST3 PRIVATE PHS_VIEW_CLASS_NAME=PhsEditorView_VST3)
target_link_libraries(PyPHS_VST3 PRIVATE ${PLATFORM_LIBS})

if(APPLE)
    set_target_properties(PyPHS_VST3 PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "vst3"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.pyphs.pianoelectro.vst3"
        MACOSX_BUNDLE_BUNDLE_NAME "PyPHS Electric Piano"
    )
endif()

# ==============================================================================
# 3. AudioUnit v2 (macOS only)
# ==============================================================================
if(APPLE)
    add_library(PyPHS_AU MODULE
        src/formats/au_entry.mm
        ${GUI_SOURCES}
        ${DSP_HEADERS}
    )
    set_target_properties(PyPHS_AU PROPERTIES
        OUTPUT_NAME "PyPHS Electric Piano"
        PREFIX ""
        BUNDLE TRUE
        BUNDLE_EXTENSION "component"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.pyphs.pianoelectro.audiounit"
        MACOSX_BUNDLE_BUNDLE_NAME "PyPHS Electric Piano"
    )
    target_compile_definitions(PyPHS_AU PRIVATE PHS_VIEW_CLASS_NAME=PhsEditorView_AU)
    target_link_libraries(PyPHS_AU PRIVATE ${PLATFORM_LIBS})
endif()

# ==============================================================================
# 4. Standalone Application
# ==============================================================================
if(APPLE)
    add_executable(PyPHS_App MACOSX_BUNDLE
        src/formats/standalone_app.mm
        ${GUI_SOURCES}
        ${DSP_HEADERS}
    )
    set_target_properties(PyPHS_App PROPERTIES
        OUTPUT_NAME "PyPHS Electric Piano"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.pyphs.pianoelectro.standalone"
        MACOSX_BUNDLE_BUNDLE_NAME "PyPHS Electric Piano"
    )
    target_compile_definitions(PyPHS_App PRIVATE PHS_VIEW_CLASS_NAME=PhsEditorView_Standalone)
    target_link_libraries(PyPHS_App PRIVATE ${PLATFORM_LIBS})
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

enable_testing()
add_test(NAME PassivityAndSpectralAnalysis COMMAND test_phs_piano)
