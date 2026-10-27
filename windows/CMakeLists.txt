# SpatialEQ for Windows: WASAPI routing + the shared DSP core + a Dear ImGui (Direct3D 11) interface.
#
#   cmake -S windows -B build/windows
#   cmake --build build/windows --config Release
#   ctest --test-dir build/windows -C Release
cmake_minimum_required(VERSION 3.21)
project(SpatialEQ VERSION 1.2.0 LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "The Windows app builds on Windows only; use DSP/ for the portable core.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: SpatialEQ.exe runs without the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.8
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

set(DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../DSP)

add_library(spatialeq_core STATIC
  ${DSP_DIR}/src/Engine.cpp
  ${DSP_DIR}/src/Fifo.cpp
  ${DSP_DIR}/src/Spectrum.cpp
  src/Settings.cpp
  src/AudioRouter.cpp
  src/Ducker.cpp
  src/Devices.cpp
  src/App.cpp)
target_include_directories(spatialeq_core PUBLIC ${DSP_DIR}/include src)
target_compile_features(spatialeq_core PUBLIC cxx_std_17)
target_compile_definitions(spatialeq_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(spatialeq_core PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(spatialeq_core PUBLIC ole32 avrt mmdevapi shell32 advapi32)

add_library(imgui_dx11 STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui_dx11 PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui_dx11 PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui_dx11 PUBLIC d3d11 dxgi d3dcompiler dwmapi)

add_executable(SpatialEQ WIN32
  src/main.cpp
  src/Ui.cpp
  src/Scene.cpp
  resources/SpatialEQ.rc
  resources/SpatialEQ.manifest)
target_link_libraries(SpatialEQ PRIVATE spatialeq_core imgui_dx11 comdlg32)
target_compile_options(SpatialEQ PRIVATE /W4 /permissive- /utf-8)
set_source_files_properties(resources/SpatialEQ.rc PROPERTIES COMPILE_DEFINITIONS
  "SQ_VERSION_MAJOR=${PROJECT_VERSION_MAJOR};SQ_VERSION_MINOR=${PROJECT_VERSION_MINOR};SQ_VERSION_PATCH=${PROJECT_VERSION_PATCH};SQ_VERSION_STRING=\"${PROJECT_VERSION}\"")

enable_testing()
add_executable(windows_tests tests/windows_tests.cpp)
target_link_libraries(windows_tests PRIVATE spatialeq_core)
target_compile_options(windows_tests PRIVATE /W4 /permissive- /utf-8)
add_test(NAME windows_tests COMMAND windows_tests)
