cmake_minimum_required(VERSION 3.22)
project(VocalDoubler VERSION 1.7.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Download JUCE automatically ──────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ── Plugin target ─────────────────────────────────────────────────────────────
juce_add_plugin(VocalDoubler
    COMPANY_NAME                "Omegadarren"
    PLUGIN_MANUFACTURER_CODE    Omgd
    PLUGIN_CODE                 VDbl
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "Vocal Doubler"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             "Fx|Modulation"
    COPY_PLUGIN_AFTER_BUILD     TRUE   # copies VST3 to %CommonProgramFiles%\VST3
)

juce_generate_juce_header(VocalDoubler)

target_sources(VocalDoubler
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Ui/PlateLookAndFeel.cpp
)

target_compile_definitions(VocalDoubler
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(VocalDoubler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
