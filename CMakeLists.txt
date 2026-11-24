cmake_minimum_required(VERSION 3.22)
project(stringmodel LANGUAGES C CXX)   # C (and OBJC/OBJCXX on macOS) for the plugin build
if(APPLE)
  enable_language(OBJC OBJCXX)
endif()

add_subdirectory(synthkit)
include(CTest)

synthkit_add_synth(stringmodel SOURCES src/Synth.cpp)
# The browser build: WebAssembly SIMD (every current browser has it) lets the
# radiation's 120 modes and other plain loops run four at a time.
if(SYNTHKIT_WASM)
  target_compile_options(stringmodel_synth PRIVATE -msimd128)
endif()

# Offline model experiments (no synthkit): one string, and the whole
# instrument driven by a note list. See docs/MODEL.md.
if(NOT SYNTHKIT_WASM)
  add_executable(string_lab tools/string_lab.cpp)
  add_executable(instrument_lab tools/instrument_lab.cpp)
  add_executable(radiation_lab tools/radiation_lab.cpp)
  add_executable(bowing_lab tools/bowing_lab.cpp)
  add_executable(performer_fuzz tools/performer_fuzz.cpp)
  target_compile_features(radiation_lab PRIVATE cxx_std_20)
  target_compile_features(string_lab PRIVATE cxx_std_20)
  target_compile_features(instrument_lab PRIVATE cxx_std_20)
  target_compile_features(bowing_lab PRIVATE cxx_std_20)
  target_compile_features(performer_fuzz PRIVATE cxx_std_20)
endif()
