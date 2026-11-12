cmake_minimum_required(VERSION 3.22)

# macOS : binaire universel (Apple Silicon + Intel), macOS 10.13 minimum (a fixer AVANT project())
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

project(FM9 VERSION 1.3.14)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Windows : runtime C++ statique (pas de redistribuable Visual C++ a installer)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE : dossier local si JUCE_DIR est fourni, sinon telechargement automatique
set(JUCE_DIR "" CACHE PATH "Copie locale de JUCE (optionnel)")
if(JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
  add_subdirectory(${JUCE_DIR} JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(JUCE)
endif()

# visuel integre au plugin (aucun fichier externe a l'execution)
file(GLOB FM9_ASSET_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/plugin/*.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/plugin/*.jpg"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/plugin/*.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/plugin/*.svg"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/plugin/*.ttf"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/plugin/*.wasm")
juce_add_binary_data(FM9Assets SOURCES ${FM9_ASSET_FILES})
set_target_properties(FM9Assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

juce_add_plugin(FM9
  COMPANY_NAME "NINE HANDS | DSP"
  PLUGIN_MANUFACTURER_CODE Wiga
  PLUGIN_CODE WgF9
  BUNDLE_ID com.ninehandsdsp.fm9
  FORMATS VST3
  VST3_CATEGORIES Instrument Synth
  PRODUCT_NAME "FM-9"
  VERSION 1.3.14
  DESCRIPTION "6-operator FM synthesizer in the spirit of the M-VAVE FM-1 (DX7 SysEx compatible)"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(FM9)

target_sources(FM9 PRIVATE
  PluginProcessor.cpp PluginEditor.cpp FirmwareHost.cpp HwFirmware.cpp
  wasm3/m3_bind.c wasm3/m3_code.c wasm3/m3_compile.c wasm3/m3_core.c wasm3/m3_deterministic.c wasm3/m3_env.c wasm3/m3_exec.c
  wasm3/m3_function.c wasm3/m3_info.c wasm3/m3_module.c wasm3/m3_parse.c wasm3/m3_snapshot.c wasm3/m3_validate.c wasm3/m3_xxh64.c
  msfa/dx7note.cc msfa/env.cc msfa/exp2.cc msfa/fm_core.cc msfa/fm_op_kernel.cc msfa/freqlut.cc
  msfa/lfo.cc msfa/pitchenv.cc msfa/porta.cpp msfa/sin.cc msfa/tuning.cc)
target_include_directories(FM9 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/msfa)

file(GLOB WASM3_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/wasm3/*.c")
if(MSVC)
  set_source_files_properties(${WASM3_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0;/O2;/Zc:preprocessor")   # ##__VA_ARGS__ de wasm3
else()
  set_source_files_properties(${WASM3_SOURCES} PROPERTIES COMPILE_OPTIONS "-w;-O3")
endif()

if(MSVC)
  target_compile_options(FM9 PRIVATE /utf-8)
  set_source_files_properties(msfa/dx7note.cc msfa/env.cc msfa/exp2.cc msfa/fm_core.cc msfa/fm_op_kernel.cc msfa/freqlut.cc
    msfa/lfo.cc msfa/pitchenv.cc msfa/porta.cpp msfa/sin.cc msfa/tuning.cc PROPERTIES COMPILE_OPTIONS "/W0")
else()
  set_source_files_properties(msfa/dx7note.cc msfa/env.cc msfa/exp2.cc msfa/fm_core.cc msfa/fm_op_kernel.cc msfa/freqlut.cc
    msfa/lfo.cc msfa/pitchenv.cc msfa/porta.cpp msfa/sin.cc msfa/tuning.cc PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_compile_definitions(FM9 PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

include(cmake/Fm1hw.cmake)

target_link_libraries(FM9
  PRIVATE
    FM9Assets
    fm1hw
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
