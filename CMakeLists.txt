cmake_minimum_required(VERSION 3.22)
project(Nightshade VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Locate JUCE
#
# Option A (recommended): pass your local clone on the command line:
#   cmake -B build -G Xcode -DJUCE_PATH=/path/to/JUCE
#
# Option B: let CMake fetch JUCE 7.0.12 automatically via FetchContent.
# ============================================================
if(DEFINED JUCE_PATH)
    message(STATUS "Using local JUCE at: ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
    message(STATUS "JUCE_PATH not set – fetching JUCE 7.0.12 via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        7.0.12
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# ============================================================
# Plugin target
# ============================================================
juce_add_plugin(Nightshade
    COMPANY_NAME            "HarrisonFX"
    COMPANY_WEBSITE         "https://harrisonfx.com"
    BUNDLE_ID               "com.harrisonfx.nightshade"

    PLUGIN_MANUFACTURER_CODE Hrfx
    PLUGIN_CODE             Ngsh

    FORMATS                 VST3 AU

    PRODUCT_NAME            "Nightshade"
    PLUGIN_NAME             "Nightshade"
    DESCRIPTION             "Overdrive / distortion pedal plug-in"

    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    AU_MAIN_TYPE            kAudioUnitType_Effect
    AU_EXPORT_PREFIX        NightshadeAU

    VST3_CATEGORIES         "Fx" "Distortion"
)

# Generates JuceHeader.h (required for #include <JuceHeader.h>)
juce_generate_juce_header(Nightshade)

# ============================================================
# Binary data (embedded assets)
# ============================================================
juce_add_binary_data(NightshadeData SOURCES
    Resources/GWasher.jpg.svg
)

# ============================================================
# Source files
# ============================================================
target_sources(Nightshade PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/GWasherButton.cpp
    Source/GrindstoneKnob.cpp
    Source/GrindstoneLogoType.cpp
    Source/GrindstonePedalFace.cpp
)

# ============================================================
# Compile definitions
# ============================================================
target_compile_definitions(Nightshade
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

# ============================================================
# Link libraries
# ============================================================
target_link_libraries(Nightshade
    PRIVATE
        NightshadeData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ============================================================
# Xcode-specific tweaks
# ============================================================
if(APPLE)
    set_target_properties(Nightshade PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    )
endif()
