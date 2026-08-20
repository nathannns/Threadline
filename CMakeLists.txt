cmake_minimum_required(VERSION 3.22)
project(Threadline VERSION 0.1.0 LANGUAGES C CXX)

# --- JUCE ---
# Pinned to 8.0.15 to match the version the reused Rockalizer modules
# (Chorus/Echo/Tremolo) were written against.
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
    Resources/Images/tweed_main.png
    Resources/Images/plugin_background_subtle.png
    Resources/Images/knob_pointer.png
    Resources/Images/knob_effects.png
    Resources/Images/knob_eq.png
    Resources/Images/knob_compressor.png
    Resources/Images/knob_klon.png
    Resources/Images/knob_breaker.png
    Resources/Images/knob_tremolo.png
    Resources/Images/knob_chorus.png
    Resources/Images/knob_delay.png
    Resources/Images/knob_reverb.png
    Resources/Images/knob_gold.png
    Resources/Images/eq_slider_cap.png
    Resources/Images/led_effect_off.png
    Resources/Images/led_effect_on.png
    Resources/Images/Buttons/button_off.png
    Resources/Images/Buttons/button_on.png
    Resources/Images/Buttons/rocker_off.png
    Resources/Images/Buttons/rocker_on.png
    Resources/Images/RackPlates/plate_compressor.png
    Resources/Images/RackPlates/plate_klon.png
    Resources/Images/RackPlates/plate_ts9.png
    Resources/Images/RackPlates/plate_tremolo.png
    Resources/Images/RackPlates/plate_chorus.png
    Resources/Images/RackPlates/plate_delay.png
    Resources/Images/RackPlates/plate_reverb.png
    Resources/Images/RackPlates/plate_eq.png
    Resources/Reference/threadline_logo.png
    Resources/Fonts/IBMPlexSans-Regular.ttf
    Resources/Fonts/IBMPlexSans-Medium.ttf
    Resources/Fonts/IBMPlexSans-SemiBold.ttf
    Resources/Fonts/IBMPlexSans-Bold.ttf
    Resources/ImpulseResponses/HallRoom/large_hall.wav
    Resources/ImpulseResponses/HallRoom/large_stage.wav
    Resources/ImpulseResponses/HallRoom/small_room.wav
    Resources/ImpulseResponses/Spring/spring_space_clean.wav
    Resources/ImpulseResponses/Spring/spring_9100_clean.wav
    Resources/ImpulseResponses/Spring/spring_echomixer_clean.wav
    Resources/CabIR/BLUE.wav
    Resources/CabIR/BOUTIQUE.wav
    Resources/CabIR/BRITISH.wav
    Resources/CabIR/CLASSIC.wav
    Resources/CabIR/DELUXE.wav
    Resources/CabIR/KING.wav
    Resources/CabIR/MODERN.wav
    Resources/CabIR/RECT.wav
    Resources/CabIR/ROCK.wav
    Resources/CabIR/TWEED.wav
    Resources/CabIR/TWIN.wav
    "Resources/CabIR/VC 30.wav"
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
        Source/UI/Pedalboard/PedalDisplayNames.h
        Source/UI/Pedalboard/PedalTile.h
        Source/UI/Pedalboard/PedalTiles.h
        Source/UI/Pedalboard/PedalTileFactory.h
        Source/UI/Pedalboard/PedalboardComponent.h
        Source/UI/Pedalboard/PedalboardComponent.cpp
        Source/DSP/GraphicEQModule.h
        Source/DSP/Antialiasing.h
        Source/DSP/ChorusModule.cpp
        Source/DSP/ChorusModule.h
        Source/DSP/DimensionChorusModule.cpp
        Source/DSP/DimensionChorusModule.h
        Source/DSP/DimensionDBBDModule.cpp
        Source/DSP/DimensionDBBDModule.h
        Source/DSP/EchoModule.cpp
        Source/DSP/EchoModule.h
        Source/DSP/SpaceEchoModule.cpp
        Source/DSP/SpaceEchoModule.h
        Source/DSP/CarbonCopyModule.cpp
        Source/DSP/CarbonCopyModule.h
        Source/DSP/HallRoomReverbModule.cpp
        Source/DSP/HallRoomReverbModule.h
        Source/DSP/TremoloModule.cpp
        Source/DSP/TremoloModule.h
        Source/DSP/NoiseGateModule.h
        Source/DSP/CompressorModule.h
        Source/DSP/PeakLevel.h
        Source/DSP/WDFCore.h
        Source/DSP/KlonModule.h
        Source/DSP/TS9Module.h
        Source/DSP/ComplementaryToneStack.h
        Source/DSP/FangsModule.h
        Source/DSP/BisonModule.h
        Source/DSP/GrowlModule.h
        Source/DSP/TapeModule.cpp
        Source/DSP/TapeModule.h
        Source/DSP/AmpModule.h
        Source/DSP/CabModule.h
        Source/DSP/SpringModule.cpp
        Source/DSP/SpringModule.h
        Source/DSP/ChannelEQModule.h
        Source/DSP/LowDynamicModule.h
        Source/DSP/DeskModule.h
        Source/DSP/PedalNode.h
        Source/DSP/PedalboardOrder.h
        Source/DSP/PedalChainRunner.h
        Source/DSP/PedalChainRunner.cpp
        Source/DSP/PedalNodes/NoiseGateNode.h
        Source/DSP/PedalNodes/InputGainNode.h
        Source/DSP/PedalNodes/CompressorNode.h
        Source/DSP/PedalNodes/LowDynamicNode.h
        Source/DSP/PedalNodes/KlonNode.h
        Source/DSP/PedalNodes/TS9Node.h
        Source/DSP/PedalNodes/FangsNode.h
        Source/DSP/PedalNodes/BisonNode.h
        Source/DSP/PedalNodes/GrowlNode.h
        Source/DSP/PedalNodes/TapeNode.h
        Source/DSP/PedalNodes/AmpNode.h
        Source/DSP/PedalNodes/CabUnitNode.h
        Source/DSP/PedalNodes/TremoloNode.h
        Source/DSP/PedalNodes/ChorusNode.h
        Source/DSP/PedalNodes/DimensionChorusNode.h
        Source/DSP/PedalNodes/DimensionDBBDNode.h
        Source/DSP/PedalNodes/DelayNode.h
        Source/DSP/PedalNodes/SpaceEchoNode.h
        Source/DSP/PedalNodes/ReverbNode.h
        Source/DSP/PedalNodes/SpringNode.h
        Source/DSP/PedalNodes/ChannelEQNode.h
        Source/DSP/PedalNodes/DeskNode.h
        Source/DSP/PedalNodes/GraphicEQNode.h
        Source/DSP/PedalNodes/OutputGainNode.h
        Source/DSP/PedalNodes/ParallelNode.h
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

# --- AmpModule CPU benchmark ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# runs the real AmpModule::process() code directly (no plugin/host
# wrapper) many times and reports actual wall-clock cost per block against
# the real-time budget. See Source/AmpBenchmark.cpp.
juce_add_console_app(AmpBenchmark PRODUCT_NAME "AmpBenchmark")
juce_generate_juce_header(AmpBenchmark)
target_sources(AmpBenchmark PRIVATE Source/AmpBenchmark.cpp)
target_compile_features(AmpBenchmark PRIVATE cxx_std_17)
target_link_libraries(AmpBenchmark
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- TS9 clipper numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real TS9Module::TS9Clipper and sweeps it for NaN/Inf/
# blow-up, then measures its small-signal frequency response to confirm the
# ported BYOD R-type scattering matrix. See Source/TS9Validation.cpp.
juce_add_console_app(TS9Validation PRODUCT_NAME "TS9Validation")
juce_generate_juce_header(TS9Validation)
target_sources(TS9Validation PRIVATE Source/TS9Validation.cpp)
target_compile_features(TS9Validation PRIVATE cxx_std_17)
target_link_libraries(TS9Validation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Klon gain-stage numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real KlonModule (the full-faithful PreAmpWDF -> AmpStage ->
# ClippingWDF -> FeedForward2WDF -> SummingAmp chain) and sweeps it for
# NaN/Inf/blow-up, then checks the IIR stages' poles and reports output levels
# to calibrate outputNormalization. See Source/KlonValidation.cpp.
juce_add_console_app(KlonValidation PRODUCT_NAME "KlonValidation")
juce_generate_juce_header(KlonValidation)
target_sources(KlonValidation PRIVATE Source/KlonValidation.cpp)
target_compile_features(KlonValidation PRIVATE cxx_std_17)
target_link_libraries(KlonValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Dimension BBD numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real DimensionDBBDModule (dual-MN3007 BBD + NE570 compander)
# and sweeps it for NaN/Inf/blow-up, then reports the per-mode delay window and
# output levels to calibrate the dry/wet blend. See
# Source/DimensionDBBDValidation.cpp.
juce_add_console_app(DimensionDBBDValidation PRODUCT_NAME "DimensionDBBDValidation")
juce_generate_juce_header(DimensionDBBDValidation)
target_sources(DimensionDBBDValidation PRIVATE
    Source/DimensionDBBDValidation.cpp
    Source/DSP/DimensionDBBDModule.cpp
)
target_compile_features(DimensionDBBDValidation PRIVATE cxx_std_17)
target_link_libraries(DimensionDBBDValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Channel EQ (Neve 1073) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real ChannelEQModule and sweeps it for NaN/Inf/blow-up,
# then measures the mid band's realised peak frequency + Q (must come out of
# the LC/R network ~2, not the old hardcoded 0.9), the shelf corners, the HPF
# slope, and output-transformer DC rejection. See Source/ChannelEQValidation.cpp.
juce_add_console_app(ChannelEQValidation PRODUCT_NAME "ChannelEQValidation")
juce_generate_juce_header(ChannelEQValidation)
target_sources(ChannelEQValidation PRIVATE Source/ChannelEQValidation.cpp)
target_compile_features(ChannelEQValidation PRIVATE cxx_std_17)
target_link_libraries(ChannelEQValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- JC Chorus (Roland JC-120 BBD chorus) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real JCChorusModule and sweeps it for NaN/Inf/blow-up,
# then confirms dry passthrough at mix=0 and stereo decorrelation at full
# wet. See Source/JCChorusValidation.cpp.
juce_add_console_app(JCChorusValidation PRODUCT_NAME "JCChorusValidation")
juce_generate_juce_header(JCChorusValidation)
target_sources(JCChorusValidation PRIVATE Source/JCChorusValidation.cpp)
target_compile_features(JCChorusValidation PRIVATE cxx_std_17)
target_link_libraries(JCChorusValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Fangs (op-amp diode-clip fuzz) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real FangsModule and sweeps gain x filter x level x mix
# x amplitude x frequency at 48/96/192k for NaN/Inf/blow-up. See
# Source/FangsValidation.cpp.
juce_add_console_app(FangsValidation PRODUCT_NAME "FangsValidation")
juce_generate_juce_header(FangsValidation)
target_sources(FangsValidation PRIVATE Source/FangsValidation.cpp)
target_compile_features(FangsValidation PRIVATE cxx_std_17)
target_link_libraries(FangsValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Bison (two-stage cascaded fuzz) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real BisonModule and sweeps sustain x tone x level x mix
# x amplitude x frequency at 48/96/192k for NaN/Inf/blow-up. See
# Source/BisonValidation.cpp.
juce_add_console_app(BisonValidation PRODUCT_NAME "BisonValidation")
juce_generate_juce_header(BisonValidation)
target_sources(BisonValidation PRIVATE Source/BisonValidation.cpp)
target_compile_features(BisonValidation PRIVATE cxx_std_17)
target_link_libraries(BisonValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Growl (germanium two-transistor fuzz) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real GrowlModule and sweeps bias x fuzz x level x mix x
# amplitude x frequency at 48/96/192k for NaN/Inf/blow-up, specifically
# re-exercising Q1's per-sample Newton-Raphson base-voltage solve. See
# Source/GrowlValidation.cpp.
juce_add_console_app(GrowlValidation PRODUCT_NAME "GrowlValidation")
juce_generate_juce_header(GrowlValidation)
target_sources(GrowlValidation PRIVATE Source/GrowlValidation.cpp)
target_compile_features(GrowlValidation PRIVATE cxx_std_17)
target_link_libraries(GrowlValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
