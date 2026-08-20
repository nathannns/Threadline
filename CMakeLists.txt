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

# --- Tape (tape saturation/compression) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real TapeModule and sweeps drive x compression x tone x
# age x tape-type x amplitude x frequency at 48/96/192k for NaN/Inf/blow-up.
# See Source/TapeValidation.cpp.
juce_add_console_app(TapeValidation PRODUCT_NAME "TapeValidation")
juce_generate_juce_header(TapeValidation)
target_sources(TapeValidation PRIVATE
    Source/TapeValidation.cpp
    Source/DSP/TapeModule.cpp
)
target_compile_features(TapeValidation PRIVATE cxx_std_17)
target_link_libraries(TapeValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Compressor (optical/vactrol) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real CompressorModule and sweeps compression x attack x
# tilt x mid x output-level x amplitude x frequency at 48/96/192k for
# NaN/Inf/blow-up. See Source/CompressorValidation.cpp.
juce_add_console_app(CompressorValidation PRODUCT_NAME "CompressorValidation")
juce_generate_juce_header(CompressorValidation)
target_sources(CompressorValidation PRIVATE Source/CompressorValidation.cpp)
target_compile_features(CompressorValidation PRIVATE cxx_std_17)
target_link_libraries(CompressorValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Low Dynamic (floating-centre expander) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real LowDynamicModule and sweeps up x down x fast-mode x
# mix x amplitude x frequency at 48/96/192k for NaN/Inf/blow-up. See
# Source/LowDynamicValidation.cpp.
juce_add_console_app(LowDynamicValidation PRODUCT_NAME "LowDynamicValidation")
juce_generate_juce_header(LowDynamicValidation)
target_sources(LowDynamicValidation PRIVATE Source/LowDynamicValidation.cpp)
target_compile_features(LowDynamicValidation PRIVATE cxx_std_17)
target_link_libraries(LowDynamicValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Noise gate (three-band hysteresis) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real NoiseGateModule and sweeps amount x amplitude x
# frequency at 48/96/192k for NaN/Inf/blow-up. See
# Source/NoiseGateValidation.cpp.
juce_add_console_app(NoiseGateValidation PRODUCT_NAME "NoiseGateValidation")
juce_generate_juce_header(NoiseGateValidation)
target_sources(NoiseGateValidation PRIVATE Source/NoiseGateValidation.cpp)
target_compile_features(NoiseGateValidation PRIVATE cxx_std_17)
target_link_libraries(NoiseGateValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Desk (console-summing coloration) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real DeskModule and sweeps amount x style-power x
# amplitude x frequency at 48/96/192k for NaN/Inf/blow-up. See
# Source/DeskValidation.cpp.
juce_add_console_app(DeskValidation PRODUCT_NAME "DeskValidation")
juce_generate_juce_header(DeskValidation)
target_sources(DeskValidation PRIVATE Source/DeskValidation.cpp)
target_compile_features(DeskValidation PRIVATE cxx_std_17)
target_link_libraries(DeskValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Amp (7 tube/SS amp voices) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real AmpModule and sweeps voice x drive x tone x output-
# level x amplitude x frequency at 48/96/192k for NaN/Inf/blow-up, driving
# every voice's per-sample tube solve to both extremes. See
# Source/AmpValidation.cpp.
juce_add_console_app(AmpValidation PRODUCT_NAME "AmpValidation")
juce_generate_juce_header(AmpValidation)
target_sources(AmpValidation PRIVATE Source/AmpValidation.cpp)
target_compile_features(AmpValidation PRIVATE cxx_std_17)
target_link_libraries(AmpValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Tremolo (bias + harmonic voices) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real TremoloModule and sweeps amount x rate x voice x
# amplitude x frequency at 48/96/192k for NaN/Inf/blow-up. See
# Source/TremoloValidation.cpp.
juce_add_console_app(TremoloValidation PRODUCT_NAME "TremoloValidation")
juce_generate_juce_header(TremoloValidation)
target_sources(TremoloValidation PRIVATE
    Source/TremoloValidation.cpp
    Source/DSP/TremoloModule.cpp
)
target_compile_features(TremoloValidation PRIVATE cxx_std_17)
target_link_libraries(TremoloValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Chorus (July, Julia-style BBD chorus) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real ChorusModule and sweeps rate x depth x lag x
# waveform x dry/chorus/vibrato x amplitude x frequency at 48/96/192k for
# NaN/Inf/blow-up. See Source/ChorusValidation.cpp.
juce_add_console_app(ChorusValidation PRODUCT_NAME "ChorusValidation")
juce_generate_juce_header(ChorusValidation)
target_sources(ChorusValidation PRIVATE
    Source/ChorusValidation.cpp
    Source/DSP/ChorusModule.cpp
)
target_compile_features(ChorusValidation PRIVATE cxx_std_17)
target_link_libraries(ChorusValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Dimension Chorus (Ensemble, Dimension D-style) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real DimensionChorusModule and sweeps rate x depth x
# width x tone x mix x flanger-mode x amplitude x frequency at 48/96/192k
# for NaN/Inf/blow-up, including the feedback-loop ADAA nonlinearities. See
# Source/DimensionChorusValidation.cpp.
juce_add_console_app(DimensionChorusValidation PRODUCT_NAME "DimensionChorusValidation")
juce_generate_juce_header(DimensionChorusValidation)
target_sources(DimensionChorusValidation PRIVATE
    Source/DimensionChorusValidation.cpp
    Source/DSP/DimensionChorusModule.cpp
)
target_compile_features(DimensionChorusValidation PRIVATE cxx_std_17)
target_link_libraries(DimensionChorusValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Echo (Plexer, Echoplex-style tape echo) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real EchoModule and sweeps time x sustain x volume x mode
# x amplitude x frequency at 48/96/192k for NaN/Inf/blow-up, holding the
# self-oscillating feedback loop at steady state. See Source/EchoValidation.cpp.
juce_add_console_app(EchoValidation PRODUCT_NAME "EchoValidation")
juce_generate_juce_header(EchoValidation)
target_sources(EchoValidation PRIVATE
    Source/EchoValidation.cpp
    Source/DSP/EchoModule.cpp
)
target_compile_features(EchoValidation PRIVATE cxx_std_17)
target_link_libraries(EchoValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Carbon Copy (Copier, BBD analog delay) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real CarbonCopyModule and sweeps time x regen x mix x mod
# x amplitude x frequency at 48/96/192k for NaN/Inf/blow-up, holding the
# near-self-oscillating feedback loop at steady state. See
# Source/CarbonCopyValidation.cpp.
juce_add_console_app(CarbonCopyValidation PRODUCT_NAME "CarbonCopyValidation")
juce_generate_juce_header(CarbonCopyValidation)
target_sources(CarbonCopyValidation PRIVATE
    Source/CarbonCopyValidation.cpp
    Source/DSP/CarbonCopyModule.cpp
)
target_compile_features(CarbonCopyValidation PRIVATE cxx_std_17)
target_link_libraries(CarbonCopyValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Space Echo (Satellite-201, RE-201-style 3-head tape echo) numeric
# verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real SpaceEchoModule and sweeps time x repeats x bass x
# treble x wobble x drive x pattern x amplitude x frequency at 48/96/192k for
# NaN/Inf/blow-up, holding the feedback + hysteresis-drive + wobble-modulated
# read loop at steady state. See Source/SpaceEchoValidation.cpp.
juce_add_console_app(SpaceEchoValidation PRODUCT_NAME "SpaceEchoValidation")
juce_generate_juce_header(SpaceEchoValidation)
target_sources(SpaceEchoValidation PRIVATE
    Source/SpaceEchoValidation.cpp
    Source/DSP/SpaceEchoModule.cpp
)
target_compile_features(SpaceEchoValidation PRIVATE cxx_std_17)
target_link_libraries(SpaceEchoValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Hall/Room/Plate reverb (8-line FDN) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real HallRoomReverbModule and sweeps pre-delay x decay x
# tone x mix x width x model x amplitude x frequency at 48/96/192k for
# NaN/Inf/blow-up, holding the orthogonal-mixed FDN at steady state. See
# Source/HallRoomReverbValidation.cpp.
juce_add_console_app(HallRoomReverbValidation PRODUCT_NAME "HallRoomReverbValidation")
juce_generate_juce_header(HallRoomReverbValidation)
target_sources(HallRoomReverbValidation PRIVATE
    Source/HallRoomReverbValidation.cpp
    Source/DSP/HallRoomReverbModule.cpp
)
target_compile_features(HallRoomReverbValidation PRIVATE cxx_std_17)
target_link_libraries(HallRoomReverbValidation
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Spring (Rockalizer, spring reverb) numeric verification ---
# A throwaway diagnostic console app, not part of the shipping plugin --
# instantiates the real SpringModule and sweeps decay x dwell x tone x drip x
# mix x impulse-model x amplitude x frequency at 48/96/192k for NaN/Inf/blow-up,
# holding the dispersion-line and orthogonal tail-FDN feedback loops at steady
# state. The module loads its IR asynchronously via AsyncUpdater, so the
# harness drives the message-manager dispatch loop after prepare/setParameters;
# it links ThreadlineData (the juce_add_binary_data target holding the three
# spring WAVs) and juce_audio_formats (WavAudioFormat used by loadImpulse).
# See Source/SpringValidation.cpp.
juce_add_console_app(SpringValidation PRODUCT_NAME "SpringValidation")
juce_generate_juce_header(SpringValidation)
target_sources(SpringValidation PRIVATE
    Source/SpringValidation.cpp
    Source/DSP/SpringModule.cpp
)
target_compile_features(SpringValidation PRIVATE cxx_std_17)
# runDispatchLoopUntil() is guarded off by default in JUCE 8 (JUCE_MODAL_LOOPS_PERMITTED=0);
# re-enable it so this headless console app can drain the module's async IR load on its
# own thread without a dedicated message thread.
target_compile_definitions(SpringValidation PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(SpringValidation
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_formats
        juce::juce_events
        ThreadlineData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
