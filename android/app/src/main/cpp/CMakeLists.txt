cmake_minimum_required(VERSION 3.22.1)
project(spatialeq_jni CXX)

# Shared real-time DSP core (same sources as the macOS / iOS / Windows apps).
set(DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../DSP)

add_library(spatialeq SHARED
    jni_bridge.cpp
    ${DSP_DIR}/src/Engine.cpp
    ${DSP_DIR}/src/Spectrum.cpp)
target_include_directories(spatialeq PRIVATE ${DSP_DIR}/include ${DSP_DIR}/src)
target_compile_features(spatialeq PRIVATE cxx_std_17)
target_compile_options(spatialeq PRIVATE -O3 -Wall)
target_link_libraries(spatialeq log)
