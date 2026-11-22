cmake_minimum_required(VERSION 3.22)
project(stringmodel LANGUAGES C CXX)   # C (and OBJC/OBJCXX on macOS) for the plugin build
if(APPLE)
  enable_language(OBJC OBJCXX)
endif()

add_subdirectory(synthkit)
include(CTest)

synthkit_add_synth(stringmodel SOURCES src/Synth.cpp)

# Offline model experiments (no synthkit): one string, and the whole
# instrument driven by a note list. See docs/MODEL.md.
if(NOT SYNTHKIT_WASM)
  add_executable(string_lab tools/string_lab.cpp)
  add_executable(instrument_lab tools/instrument_lab.cpp)
  target_compile_features(string_lab PRIVATE cxx_std_20)
  target_compile_features(instrument_lab PRIVATE cxx_std_20)
endif()
