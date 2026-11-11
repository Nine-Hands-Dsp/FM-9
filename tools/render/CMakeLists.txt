# Outil hors plugin : compile le processeur + la fenetre de FM-9 sous Linux, lance des tests audio
# et exporte des captures PNG (apercu, notice)
cmake_minimum_required(VERSION 3.22)
project(FM9Render VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
add_subdirectory(${JUCE_DIR} JUCE)
set(W ${CMAKE_CURRENT_SOURCE_DIR}/../..)
file(GLOB FM9_ASSET_FILES "${W}/assets/plugin/*.png" "${W}/assets/plugin/*.svg" "${W}/assets/plugin/*.ttf")
juce_add_binary_data(FM9Assets SOURCES ${FM9_ASSET_FILES})
juce_add_console_app(FM9Render PRODUCT_NAME "FM9Render")
juce_generate_juce_header(FM9Render)
file(GLOB MSFA ${W}/msfa/*.cc ${W}/msfa/*.cpp)
target_sources(FM9Render PRIVATE main.cpp ${W}/PluginProcessor.cpp ${W}/PluginEditor.cpp ${MSFA})
set_source_files_properties(${MSFA} PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(FM9Render PRIVATE ${W} ${W}/msfa)
target_compile_definitions(FM9Render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_ALSA=0 JUCE_JACK=0
  JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XSHM=0 JUCE_USE_XRENDER=0 JucePlugin_Name="FM-9" JucePlugin_Build_VST3=0
  JucePlugin_VersionString="1.0.0" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(FM9Render PRIVATE FM9Assets juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
