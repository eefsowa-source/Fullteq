cmake_minimum_required(VERSION 3.22)
project(Fullteq VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

juce_add_plugin(Fullteq
  COMPANY_NAME "Fullteq Audio"
  BUNDLE_ID com.fullteq.audio
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE
  PLUGIN_MANUFACTURER_CODE Fteq
  PLUGIN_CODE Fteq
  FORMATS VST3 AU Standalone
  PRODUCT_NAME "Fullteq")

juce_generate_juce_header(Fullteq)

target_sources(Fullteq PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_compile_definitions(Fullteq PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Fullteq PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)

# Offline DSP verification (not part of the default build):
#   cmake --build build --target FullteqTests && build/FullteqTests_artefacts/FullteqTests
juce_add_console_app(FullteqTests PRODUCT_NAME "FullteqTests")
juce_generate_juce_header(FullteqTests)
target_sources(FullteqTests PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Tests/DspTests.cpp)
target_compile_definitions(FullteqTests PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JucePlugin_Name="Fullteq")
target_link_libraries(FullteqTests PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
  juce::juce_recommended_config_flags)
set_target_properties(FullteqTests PROPERTIES EXCLUDE_FROM_ALL TRUE)
