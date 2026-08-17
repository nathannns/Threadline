cmake_minimum_required(VERSION 3.22)
project(Threadline VERSION 0.1.0 LANGUAGES C CXX)

# --- JUCE ---
# Pinned to 8.0.15 to match the version the reused Rockalizer modules
# (Chorus/Echo/Spring/Tremolo) were written against.
#
# You said you already have JUCE installed locally. If you'd rather point at
# that instead of letting CMake fetch its own copy, comment out the
# FetchContent block below and use this instead:
#   list(APPEND CMAKE_PREFIX_PATH "/path/to/your/JUCE")
#   find_package(JUCE CONFIG REQUIRED)
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(ThreadlineData SOURCES
    Resources/Images/tweed_amp.png
    Resources/Images/plugin_background_subtle.png
    Resources/Images/knob_pointer.png
    Resources/Images/knob_effects.png
    Resources/Images/eq_slider_cap.png
    Resources/Images/led_effect_off.png
    Resources/Images/led_effect_on.png
    Resources/Images/Buttons/button_off.png
    Resources/Images/Buttons/button_on.png
    Resources/Images/RackPlates/plate_compressor.png
    Resources/Images/RackPlates/plate_klon.png
    Resources/Images/RackPlates/plate_ts9.png
    Resources/Images/RackPlates/plate_tremolo.png
    Resources/Images/RackPlates/plate_chorus.png
    Resources/Images/RackPlates/plate_delay.png
    Resources/Images/RackPlates/plate_reverb.png
    Resources/Images/RackPlates/plate_eq.png
    Resources/Reference/threadline_logo.png
    Resources/ImpulseResponses/HallRoom/large_hall.wav
    Resources/ImpulseResponses/HallRoom/large_stage.wav
    Resources/ImpulseResponses/HallRoom/small_room.wav
    Resources/CabIR/Tweed_Combo_1x12_Bright_Mix.wav
    Resources/CabIR/Tweed_Combo_1x12_Dark_Mix.wav
    Resources/CabIR/Tweed_Combo_1x12_Medium_Mix.wav
    Resources/CabIR/Tweed_Combo_1x12_Medium_57.wav
    Resources/CabIR/Tweed_Combo_1x12_Medium_87.wav
    Resources/CabIR/Tweed_Combo_1x12_Medium_160.wav
)

juce_add_plugin(Threadline
    COMPANY_NAME "Nathan Aji"
    BUNDLE_ID "com.nathanaji.threadline"
    PLUGIN_MANUFACTURER_CODE Nath
    PLUGIN_CODE Thrd
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Threadline"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Reference/threadline_app_icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Reference/threadline_app_icon.png"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Threadline needs audio input access to process your guitar signal."
    COPY_PLUGIN_AFTER_BUILD TRUE
)

juce_generate_juce_header(Threadline)

target_sources(Threadline
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PresetManager.h
        Source/UI/ThreadlineComponents.h
        Source/UI/SectionBuilder.h
        Source/UI/Page1Component.h
        Source/UI/Page1Component.cpp
        Source/UI/Page2Component.h
        Source/UI/Page2Component.cpp
        Source/UI/Page3Component.h
        Source/UI/Page3Component.cpp
        Source/UI/Page4Component.h
        Source/UI/Page4Component.cpp
        Source/DSP/GraphicEQModule.h
        Source/DSP/ChorusModule.cpp
        Source/DSP/ChorusModule.h
        Source/DSP/EchoModule.cpp
        Source/DSP/EchoModule.h
        Source/DSP/HallRoomReverbModule.cpp
        Source/DSP/HallRoomReverbModule.h
        Source/DSP/TremoloModule.cpp
        Source/DSP/TremoloModule.h
        Source/DSP/NoiseGateModule.h
        Source/DSP/CompressorModule.h
        Source/DSP/PeakLevel.h
        Source/DSP/KlonModule.h
        Source/DSP/TS9Module.h
        Source/DSP/AmpModule.h
        Source/DSP/CabModule.h
)

target_compile_features(Threadline PRIVATE cxx_std_17)

target_compile_definitions(Threadline
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Threadline
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        ThreadlineData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
