# Standalone build of the DSP core for unit tests and benchmarks.
# The app itself compiles these sources directly through the Xcode project.
cmake_minimum_required(VERSION 3.20)
project(spatialeq_dsp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(spatialeq_dsp STATIC src/Engine.cpp)
target_include_directories(spatialeq_dsp PUBLIC include)
target_compile_options(spatialeq_dsp PRIVATE -Wall -Wextra -O2)

add_executable(dsp_tests tests/dsp_tests.cpp)
target_link_libraries(dsp_tests PRIVATE spatialeq_dsp)
target_include_directories(dsp_tests PRIVATE src)

enable_testing()
add_test(NAME dsp_tests COMMAND dsp_tests)
