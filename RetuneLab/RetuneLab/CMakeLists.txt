cmake_minimum_required(VERSION 3.22)
project(RetuneLab VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
option(COPY_AFTER_BUILD "Copy VST3 to the system folder after building" ON)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(RetuneLab
    COMPANY_NAME "YourName"
    PLUGIN_MANUFACTURER_CODE Yrnm
    PLUGIN_CODE Rtl1
    FORMATS VST3 AU Standalone                 # AU (Logic, GarageBand) is built on macOS only
    PRODUCT_NAME "RetuneLab"
    BUNDLE_ID com.retunelab.retunelab
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx "Pitch Shift"
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "RetuneLab records your voice so you can edit it." 
    COPY_PLUGIN_AFTER_BUILD ${COPY_AFTER_BUILD})

target_sources(RetuneLab PRIVATE Source/Dashboard.h Source/Dashboard.cpp Source/VocalFx.h Source/AirFx.h Source/PluginProcessor.h Source/PluginProcessor.cpp Source/PluginEditor.h Source/PluginEditor.cpp Source/ClipEngine.h Source/ClipEngine.cpp)
target_compile_definitions(RetuneLab PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(RetuneLab PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
